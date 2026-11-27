#include "Cafe/HW/Latte/Renderer/GamepadRTPStream.h"

#include "config/CemuConfig.h"
#include "util/helpers/helpers.h"
#include "util/Socket/Socket.h"
#include "util/highresolutiontimer/HighResolutionTimer.h"

#include <random>
#include <opus/opus.h>

#if BOOST_OS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <codecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>
#endif

#if BOOST_OS_MACOS
#include "Cafe/HW/Latte/Renderer/Metal/MetalRenderer.h"
#include <CoreMedia/CMBlockBuffer.h>
#include <CoreMedia/CMSampleBuffer.h>
#include <CoreVideo/CVPixelBuffer.h>
#include <IOSurface/IOSurfaceRef.h>
#include <VideoToolbox/VideoToolbox.h>
#endif

#if BOOST_OS_LINUX
#include <fcntl.h>
#include <linux/fb.h>
#include <va/va.h>
#include <va/va_drm.h>
#include <va/va_enc_h264.h>
#include <unistd.h>
#endif

namespace
{
constexpr sint32 kRtpPayloadSize = 1200;
constexpr uint8 kRtpPayloadType = 96;

void WriteU16BigEndian(uint8* destination, uint16 value)
{
	destination[0] = static_cast<uint8>(value >> 8);
	destination[1] = static_cast<uint8>(value);
}

void WriteU32BigEndian(uint8* destination, uint32 value)
{
	destination[0] = static_cast<uint8>(value >> 24);
	destination[1] = static_cast<uint8>(value >> 16);
	destination[2] = static_cast<uint8>(value >> 8);
	destination[3] = static_cast<uint8>(value);
}

struct CaptureFrame
{
	std::vector<uint8> rgbData;
	sint32 width;
	sint32 height;
};

#if BOOST_OS_MACOS
class MetalVideoToolboxStream
{
public:
	MetalVideoToolboxStream()
		: m_ssrc(CreateSsrc()),
		  m_address(ResolveAddress()),
		  m_port(static_cast<uint16>(GetConfig().rtp_port)),
		  m_width(static_cast<sint32>(GetConfig().rtp_width)),
		  m_height(static_cast<sint32>(GetConfig().rtp_height)),
		  m_fps(static_cast<sint32>(GetConfig().rtp_fps)),
		  m_bitrateKbps(static_cast<sint32>(GetConfig().rtp_bitrate_kbps))
	{
	}

	~MetalVideoToolboxStream()
	{
		{
			std::unique_lock lock(m_mutex);
			m_stopping = true;
			m_condition.wait(lock, [this]() { return m_activeCallbacks == 0; });
		}
		if (m_session)
		{
			VTCompressionSessionCompleteFrames(m_session, kCMTimeInvalid);
			VTCompressionSessionInvalidate(m_session);
			CFRelease(m_session);
		}
		if (m_computePipeline)
			m_computePipeline->release();
		if (m_computeFunction)
			m_computeFunction->release();
		if (m_computeLibrary)
			m_computeLibrary->release();
	}

	void Submit(MetalRenderer* renderer, MTL::Texture* sourceTexture)
	{
		const uint64 submitTimestampUs = HighResolutionTimer::ticksToMicroseconds(
			HighResolutionTimer::now().getTick());
		bool expected = false;
		if (!m_pendingCapture.compare_exchange_strong(expected, true))
			return;
		if (!InitializeComputePipeline(renderer->GetDevice()) ||
			!InitializeSession())
		{
			m_pendingCapture = false;
			return;
		}

		CFMutableDictionaryRef pixelAttributes = CFDictionaryCreateMutable(
			kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
			&kCFTypeDictionaryValueCallBacks);
		CFNumberRef pixelFormat = CreateNumber(kCVPixelFormatType_32BGRA);
		CFNumberRef width = CreateNumber(m_width);
		CFNumberRef height = CreateNumber(m_height);
		CFDictionaryRef iosurfaceProperties =
			CFDictionaryCreate(kCFAllocatorDefault, nullptr, nullptr, 0,
				&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
		CFDictionarySetValue(pixelAttributes, kCVPixelBufferPixelFormatTypeKey, pixelFormat);
		CFDictionarySetValue(pixelAttributes, kCVPixelBufferWidthKey, width);
		CFDictionarySetValue(pixelAttributes, kCVPixelBufferHeightKey, height);
		CFDictionarySetValue(pixelAttributes, kCVPixelBufferIOSurfacePropertiesKey,
			iosurfaceProperties);
		CFDictionarySetValue(pixelAttributes, kCVPixelBufferMetalCompatibilityKey, kCFBooleanTrue);
		CFRelease(pixelFormat);
		CFRelease(width);
		CFRelease(height);
		CFRelease(iosurfaceProperties);

		CVPixelBufferRef pixelBuffer = nullptr;
		const CVReturn cvResult = CVPixelBufferCreate(
			kCFAllocatorDefault, m_width, m_height,
			kCVPixelFormatType_32BGRA, pixelAttributes, &pixelBuffer);
		CFRelease(pixelAttributes);
		if (cvResult != kCVReturnSuccess || !pixelBuffer)
		{
			m_pendingCapture = false;
			return;
		}

		auto* descriptor = MTL::TextureDescriptor::alloc()->init();
		descriptor->setTextureType(MTL::TextureType2D);
		descriptor->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
		descriptor->setWidth(m_width);
		descriptor->setHeight(m_height);
		descriptor->setMipmapLevelCount(1);
		descriptor->setUsage(MTL::TextureUsageShaderWrite);
		descriptor->setStorageMode(MTL::StorageModeShared);
		MTL::Texture* destinationTexture = renderer->GetDevice()->newTexture(
			descriptor, CVPixelBufferGetIOSurface(pixelBuffer), 0);
		descriptor->release();
		if (!destinationTexture)
		{
			CVPixelBufferRelease(pixelBuffer);
			m_pendingCapture = false;
			return;
		}

		MTL::ComputeCommandEncoder* encoder = renderer->GetComputeCommandEncoder();
		encoder->setComputePipelineState(m_computePipeline);
		encoder->setTexture(sourceTexture, 0);
		encoder->setTexture(destinationTexture, 1);
		const uint32 applySRGBEncoding = LatteGPUState.drcBufferUsesSRGB ? 1 : 0;
		encoder->setBytes(&applySRGBEncoding, sizeof(applySRGBEncoding), 0);
		const MTL::Size threadsPerThreadgroup = {
			m_computePipeline->threadExecutionWidth(),
			std::max<NS::UInteger>(1, m_computePipeline->maxTotalThreadsPerThreadgroup() /
				m_computePipeline->threadExecutionWidth()),
			1
		};
		encoder->dispatchThreads({(NS::UInteger)m_width, (NS::UInteger)m_height, 1}, threadsPerThreadgroup);
		renderer->EndEncoding();

		MTL::CommandBuffer* commandBuffer = renderer->GetCurrentCommandBuffer()->retain();
		{
			std::scoped_lock lock(m_mutex);
			++m_activeCallbacks;
		}
		commandBuffer->addCompletedHandler([this, pixelBuffer, commandBuffer,
											 submitTimestampUs](MTL::CommandBuffer* completedBuffer) {
			if (completedBuffer->status() == MTL::CommandBufferStatusCompleted && !m_stopping)
				EncodeCompletedFrame(pixelBuffer, submitTimestampUs);
			commandBuffer->release();
			CVPixelBufferRelease(pixelBuffer);
			{
				std::scoped_lock lock(m_mutex);
				--m_activeCallbacks;
			}
			m_pendingCapture = false;
			m_condition.notify_all();
		});
		renderer->CommitCommandBuffer();
		destinationTexture->release();
	}

	void SubmitRGB(CaptureFrame&& frame)
	{
		if (!InitializeSession())
			return;

		CVPixelBufferRef pixelBuffer = nullptr;
		const CVReturn cvResult = CVPixelBufferCreate(
			kCFAllocatorDefault, m_width, m_height, kCVPixelFormatType_32BGRA,
			nullptr, &pixelBuffer);
		if (cvResult != kCVReturnSuccess || !pixelBuffer)
			return;

		if (CVPixelBufferLockBaseAddress(pixelBuffer, 0) != kCVReturnSuccess)
		{
			CVPixelBufferRelease(pixelBuffer);
			return;
		}
		auto* destination = static_cast<uint8*>(CVPixelBufferGetBaseAddress(pixelBuffer));
		const size_t stride = CVPixelBufferGetBytesPerRow(pixelBuffer);
		const uint8* source = frame.rgbData.data();
		for (sint32 y = 0; y < m_height; ++y)
		{
			const sint32 sourceY = y * frame.height / m_height;
			uint8* row = destination + y * stride;
			for (sint32 x = 0; x < m_width; ++x)
			{
				const sint32 sourceX = x * frame.width / m_width;
				const sint32 sourceOffset = (sourceY * frame.width + sourceX) * 3;
				row[x * 4] = source[sourceOffset + 2];
				row[x * 4 + 1] = source[sourceOffset + 1];
				row[x * 4 + 2] = source[sourceOffset];
				row[x * 4 + 3] = 255;
			}
		}
		CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);

		const uint64 timestampUs = HighResolutionTimer::ticksToMicroseconds(
			HighResolutionTimer::now().getTick());
		const CMTime timestamp = CMTimeMake(timestampUs, 1000000);
		CVPixelBufferRetain(pixelBuffer);
		{
			std::scoped_lock lock(m_encodeMutex);
			const OSStatus result = VTCompressionSessionEncodeFrame(
				m_session, pixelBuffer, timestamp, kCMTimeInvalid,
				nullptr, pixelBuffer, nullptr);
			if (result != noErr)
				CVPixelBufferRelease(pixelBuffer);
		}
		CVPixelBufferRelease(pixelBuffer);
	}

private:
	static uint32 CreateSsrc()
	{
		std::random_device randomDevice;
		return static_cast<uint32>(randomDevice());
	}

	static CFNumberRef CreateNumber(sint32 value)
	{
		return CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
	}

	static SocketIPv4 ResolveAddress()
	{
		const auto address = UDPSocket::Resolve(GetConfig().rtp_address.GetValue());
		if (address)
			return *address;
		cemuLog_log(LogType::Force, "RTP: unable to resolve destination {}",
			GetConfig().rtp_address.GetValue());
		return 0;
	}

	void EncodeCompletedFrame(CVPixelBufferRef pixelBuffer, uint64 submitTimestampUs)
	{
		const uint64 nowUs = HighResolutionTimer::ticksToMicroseconds(
			HighResolutionTimer::now().getTick());
		m_lastMetalCompletionUs = nowUs > submitTimestampUs ? nowUs - submitTimestampUs : 0;
		const CMTime timestamp = CMTimeMake(submitTimestampUs, 1000000);
		CVPixelBufferRetain(pixelBuffer);
		std::scoped_lock lock(m_encodeMutex);
		const OSStatus result = VTCompressionSessionEncodeFrame(
			m_session, pixelBuffer, timestamp, kCMTimeInvalid,
			nullptr, pixelBuffer, nullptr);
		if (result != noErr)
			CVPixelBufferRelease(pixelBuffer);
	}

	bool InitializeComputePipeline(MTL::Device* device)
	{
		if (m_computePipeline && m_device == device)
			return true;
		if (m_computePipeline)
		{
			m_computePipeline->release();
			m_computeFunction->release();
			m_computeLibrary->release();
			m_computePipeline = nullptr;
			m_computeFunction = nullptr;
			m_computeLibrary = nullptr;
		}

		const char* source = R"(
			#include <metal_stdlib>
			using namespace metal;
			kernel void cemuRtpScale(
				texture2d<float, access::read> source [[texture(0)]],
				texture2d<float, access::write> destination [[texture(1)]],
				constant uint& applySRGBEncoding [[buffer(0)]],
				uint2 gid [[thread_position_in_grid]]) {
				const uint2 size = uint2(destination.get_width(), destination.get_height());
				if (gid.x >= size.x || gid.y >= size.y)
					return;
				const uint2 sample = min(
					uint2(gid.x * source.get_width() / size.x,
						gid.y * source.get_height() / size.y),
					uint2(source.get_width() - 1, source.get_height() - 1));
				float4 color = source.read(sample);
				if (applySRGBEncoding) {
					for (uint channel = 0; channel < 3; ++channel) {
						float linear = color[channel];
						color[channel] = linear <= 0.0031308f
							? 12.92f * linear
							: 1.055f * pow(linear, 1.0f / 2.4f) - 0.055f;
					}
				}
				destination.write(color, gid);
			}
		)";
		NS::String* sourceString = NS::String::string(source, NS::UTF8StringEncoding);
		NS::Error* error = nullptr;
		m_computeLibrary = device->newLibrary(sourceString, nullptr, &error);
		if (!m_computeLibrary)
		{
			cemuLog_log(LogType::Force, "RTP: failed to compile Metal capture shader{}",
				error ? error->localizedDescription()->utf8String() : "");
			return false;
		}
		m_computeFunction = m_computeLibrary->newFunction(
			NS::String::string("cemuRtpScale", NS::UTF8StringEncoding));
		if (!m_computeFunction)
			return false;
		m_computePipeline = device->newComputePipelineState(m_computeFunction, &error);
		if (!m_computePipeline)
			return false;
		m_device = device;
		return true;
	}

	bool InitializeSession()
	{
		if (m_session)
			return true;

		CFMutableDictionaryRef sourceAttributes = CFDictionaryCreateMutable(
			kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
			&kCFTypeDictionaryValueCallBacks);
		auto SetNumber = [&](CFStringRef key, sint32 value) {
			CFNumberRef number = CreateNumber(value);
			CFDictionarySetValue(sourceAttributes, key, number);
			CFRelease(number);
		};
		SetNumber(kCVPixelBufferPixelFormatTypeKey, kCVPixelFormatType_32BGRA);
		SetNumber(kCVPixelBufferWidthKey, m_width);
		SetNumber(kCVPixelBufferHeightKey, m_height);
		CFDictionarySetValue(sourceAttributes, kCVPixelBufferMetalCompatibilityKey, kCFBooleanTrue);

		const OSStatus status = VTCompressionSessionCreate(
			kCFAllocatorDefault, m_width, m_height,
			kCMVideoCodecType_H264, nullptr, sourceAttributes,
			nullptr, &OnEncodedFrame, this, &m_session);
		CFRelease(sourceAttributes);
		if (status != noErr || !m_session)
		{
			cemuLog_log(LogType::Force, "RTP: failed to create VideoToolbox session: {}", status);
			return false;
		}

		auto SetSessionProperty = [&](CFStringRef key, CFTypeRef value) {
			const OSStatus result = VTSessionSetProperty(m_session, key, value);
			if (result != noErr)
				cemuLog_log(LogType::Force, "RTP: failed to set VideoToolbox property");
		};
		SetSessionProperty(kVTCompressionPropertyKey_RealTime, kCFBooleanTrue);
		SetSessionProperty(kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse);
		SetSessionProperty(kVTCompressionPropertyKey_ProfileLevel,
			kVTProfileLevel_H264_Baseline_AutoLevel);
		CFNumberRef bitRate = CreateNumber(m_bitrateKbps * 1000);
		SetSessionProperty(kVTCompressionPropertyKey_AverageBitRate, bitRate);
		CFRelease(bitRate);
		CFNumberRef fps = CreateNumber(m_fps);
		SetSessionProperty(kVTCompressionPropertyKey_ExpectedFrameRate, fps);
		CFRelease(fps);
		CFNumberRef keyframeInterval = CreateNumber(m_fps);
		SetSessionProperty(kVTCompressionPropertyKey_MaxKeyFrameInterval, keyframeInterval);
		CFRelease(keyframeInterval);
		CFNumberRef zeroDelay = CreateNumber(0);
		SetSessionProperty(kVTCompressionPropertyKey_MaxFrameDelayCount, zeroDelay);
		CFRelease(zeroDelay);

		if (VTCompressionSessionPrepareToEncodeFrames(m_session) != noErr)
		{
			VTCompressionSessionInvalidate(m_session);
			CFRelease(m_session);
			m_session = nullptr;
			return false;
		}
		cemuLog_log(LogType::Force, "RTP: using direct Metal-to-VideoToolbox capture");
		return true;
	}

	static void OnEncodedFrame(void* context, void* sourceFrameRefCon,
		OSStatus status, VTEncodeInfoFlags, CMSampleBufferRef sampleBuffer)
	{
		auto* stream = static_cast<MetalVideoToolboxStream*>(context);
		if (sourceFrameRefCon)
			CVPixelBufferRelease(static_cast<CVPixelBufferRef>(sourceFrameRefCon));
		if (status != noErr || !sampleBuffer)
		{
			if (!stream->m_encodeErrorLogged)
			{
				stream->m_encodeErrorLogged = true;
				cemuLog_log(LogType::Force, "RTP: VideoToolbox encode failed: {}", status);
			}
			return;
		}
		stream->SendSampleBuffer(sampleBuffer);
	}

	void SendSampleBuffer(CMSampleBufferRef sampleBuffer)
	{
		CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sampleBuffer);
		if (!block)
			return;

		const bool keyframe = [](CMSampleBufferRef sample) {
			CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sample, false);
			if (!attachments || CFArrayGetCount(attachments) == 0)
				return true;
			const CFDictionaryRef attachment =
				static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(attachments, 0));
			return !CFDictionaryContainsKey(attachment,
				kCMSampleAttachmentKey_NotSync);
		}(sampleBuffer);
		const CMTime presentationTime = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
		const uint64 timestampUs = static_cast<uint64>(
			CMTimeGetSeconds(presentationTime) * 1000000.0);
		const uint64 nowUs = HighResolutionTimer::ticksToMicroseconds(
			HighResolutionTimer::now().getTick());
		const uint64 encodeLatencyUs = nowUs > timestampUs ? nowUs - timestampUs : 0;
		if (++m_encodedFrames == 1 || m_encodedFrames % m_fps == 0)
		{
			cemuLog_log(LogType::Force,
				"RTP: sender latency GPU {} us, VideoToolbox {} us",
				m_lastMetalCompletionUs.load(), encodeLatencyUs);
		}

		if (keyframe)
		{
			CMFormatDescriptionRef format = CMSampleBufferGetFormatDescription(sampleBuffer);
			size_t parameterSetCount = 0;
			CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
				format, 0, nullptr, nullptr, &parameterSetCount, nullptr);
			for (size_t index = 0; index < parameterSetCount; ++index)
			{
				const uint8* parameterSet = nullptr;
				size_t parameterSetSize = 0;
				CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
					format, index, &parameterSet, &parameterSetSize,
					nullptr, nullptr);
				SendNal(parameterSet, parameterSetSize, timestampUs, false);
			}
		}

		size_t length = 0;
		char* data = nullptr;
		if (CMBlockBufferGetDataPointer(block, 0, &length, nullptr, &data) != noErr)
			return;
		size_t offset = 0;
		while (offset + 4 <= length)
		{
			const auto* bytes = reinterpret_cast<const uint8*>(data + offset);
			const size_t nalSize = (static_cast<size_t>(bytes[0]) << 24) |
				(static_cast<size_t>(bytes[1]) << 16) |
				(static_cast<size_t>(bytes[2]) << 8) | bytes[3];
			offset += 4;
			if (nalSize == 0 || nalSize > length - offset)
				break;
			const bool marker = offset + nalSize == length;
			SendNal(bytes + 4, nalSize, timestampUs, marker);
			offset += nalSize;
		}
	}

	void SendNal(const uint8* nal, size_t size, uint64 timestampUs, bool marker)
	{
		const uint32 timestamp = static_cast<uint32>(timestampUs * 90000 / 1000000);
		std::scoped_lock lock(m_sendMutex);
		if (size <= kRtpPayloadSize)
		{
			std::array<uint8, 12 + kRtpPayloadSize> packet{};
			WriteHeader(packet.data(), timestamp, marker);
			std::memcpy(packet.data() + 12, nal, size);
			m_socket.SendTo(m_address, m_port, packet.data(), 12 + size);
			return;
		}

		const uint8 fuIndicator = (nal[0] & 0xE0) | 28;
		const uint8 nalType = nal[0] & 0x1F;
		const uint8* fragment = nal + 1;
		size_t remaining = size - 1;
		bool first = true;
		while (remaining)
		{
			const size_t fragmentSize = std::min<size_t>(remaining, kRtpPayloadSize - 2);
			const bool last = fragmentSize == remaining;
			std::array<uint8, 12 + kRtpPayloadSize> packet{};
			WriteHeader(packet.data(), timestamp, marker && last);
			packet[12] = fuIndicator;
			packet[13] = nalType | (first ? 0x80 : 0) | (last ? 0x40 : 0);
			std::memcpy(packet.data() + 14, fragment, fragmentSize);
			m_socket.SendTo(m_address, m_port, packet.data(), 14 + fragmentSize);
			fragment += fragmentSize;
			remaining -= fragmentSize;
			first = false;
		}
	}

	void WriteHeader(uint8* header, uint32 timestamp, bool marker)
	{
		header[0] = 0x80;
		header[1] = kRtpPayloadType | (marker ? 0x80 : 0);
		WriteU16BigEndian(header + 2, m_sequence++);
		WriteU32BigEndian(header + 4, timestamp);
		WriteU32BigEndian(header + 8, m_ssrc);
	}

	UDPSocket m_socket;
	uint32 m_ssrc{};
	uint16 m_sequence{};
	SocketIPv4 m_address;
	uint16 m_port;
	sint32 m_width;
	sint32 m_height;
	sint32 m_fps;
	sint32 m_bitrateKbps;
	std::mutex m_sendMutex;
	std::mutex m_mutex;
	std::condition_variable m_condition;
	std::atomic_bool m_stopping{};
	uint32 m_activeCallbacks{};
	std::atomic_bool m_pendingCapture{};
	std::mutex m_encodeMutex;
	std::atomic<uint64> m_lastMetalCompletionUs{};
	std::atomic<uint64> m_encodedFrames{};

	VTCompressionSessionRef m_session{};
	MTL::Device* m_device{};
	MTL::Library* m_computeLibrary{};
	MTL::Function* m_computeFunction{};
	MTL::ComputePipelineState* m_computePipeline{};
	bool m_encodeErrorLogged{};
};
#endif

#if BOOST_OS_LINUX
class GamepadRTPWorker
{
public:
	GamepadRTPWorker()
		: m_socket{},
		  m_ssrc(CreateSsrc()),
		  m_address(ResolveAddress()),
		  m_port(static_cast<uint16>(GetConfig().rtp_port)),
		  m_width(static_cast<sint32>(GetConfig().rtp_width)),
		  m_height(static_cast<sint32>(GetConfig().rtp_height)),
		  m_fps(static_cast<sint32>(GetConfig().rtp_fps)),
		  m_bitrateKbps(static_cast<sint32>(GetConfig().rtp_bitrate_kbps))
	{
		m_thread = std::thread(&GamepadRTPWorker::Run, this);
	}

	~GamepadRTPWorker()
	{
		{
			std::scoped_lock lock(m_mutex);
			m_stopping = true;
		}
		m_condition.notify_all();
		m_thread.join();
	}

	void Submit(CaptureFrame&& frame)
	{
		{
			std::scoped_lock lock(m_mutex);
			m_pendingFrame = std::move(frame);
		}
		m_condition.notify_one();
	}

private:
	static uint32 CreateSsrc()
	{
		std::random_device randomDevice;
		return static_cast<uint32>(randomDevice());
	}

	static SocketIPv4 ResolveAddress()
	{
		const auto address = UDPSocket::Resolve(GetConfig().rtp_address.GetValue());
		if (address)
			return *address;
		cemuLog_log(LogType::Force, "RTP: unable to resolve destination {}",
			GetConfig().rtp_address.GetValue());
		return 0;
	}

	bool InitializeEncoder()
	{
		const char* renderNodes[] = {
			"/dev/dri/renderD128",
			"/dev/dri/renderD129",
			"/dev/dri/card0",
		};
		for (const char* node : renderNodes)
		{
			m_drmFd = open(node, O_RDWR | O_CLOEXEC);
			if (m_drmFd >= 0)
			{
				m_display = vaGetDisplayDRM(m_drmFd);
				break;
			}
		}
		if (!m_display || !CheckVA(vaInitialize(m_display, &m_majorVersion, &m_minorVersion),
				"initialize display"))
		{
			return false;
		}

		const VAProfile profiles[] = {
			VAProfileH264ConstrainedBaseline,
			VAProfileH264Main,
		};
		int profileCount = 0;
		std::vector<VAProfile> availableProfiles(vaMaxNumProfiles(m_display));
		if (!CheckVA(vaQueryConfigProfiles(m_display, availableProfiles.data(),
				&profileCount), "query profiles"))
			return false;
		bool profileFound = false;
		for (const VAProfile candidate : profiles)
		{
			if (std::find(availableProfiles.begin(),
					availableProfiles.begin() + profileCount, candidate) !=
				availableProfiles.begin() + profileCount)
			{
				m_profile = candidate;
				profileFound = true;
				break;
			}
		}
		if (!profileFound)
		{
			cemuLog_log(LogType::Force, "RTP: VA-API has no H.264 encoder profile");
			return false;
		}

		std::vector<VAEntrypoint> entrypoints(vaMaxNumEntrypoints(m_display));
		int entrypointCount = 0;
		vaQueryConfigEntrypoints(m_display, m_profile,
			entrypoints.data(), &entrypointCount);
		if (std::find(entrypoints.begin(), entrypoints.begin() + entrypointCount,
				VAEntrypointEncSlice) == entrypoints.begin() + entrypointCount)
		{
			cemuLog_log(LogType::Force, "RTP: VA-API H.264 encoding is unavailable");
			return false;
		}

		VAConfigAttrib attributes[] = {
			{VAConfigAttribRTFormat, 0},
			{VAConfigAttribRateControl, 0},
			{VAConfigAttribEncPackedHeaders, 0},
		};
		vaGetConfigAttributes(m_display, m_profile, VAEntrypointEncSlice,
			attributes, std::size(attributes));
		for (auto& attribute : attributes)
		{
			if (attribute.type == VAConfigAttribRTFormat)
				attribute.value = VA_RT_FORMAT_YUV420;
			else if (attribute.type == VAConfigAttribRateControl)
				attribute.value = VA_RC_CBR;
			else if (attribute.type == VAConfigAttribEncPackedHeaders)
				attribute.value = VA_ENC_PACKED_HEADER_NONE;
		}
		if (!CheckVA(vaCreateConfig(m_display, m_profile, VAEntrypointEncSlice,
				attributes, std::size(attributes), &m_config), "create config"))
			return false;
		for (auto& surface : m_surfaces)
		{
			if (!CheckVA(vaCreateSurfaces(m_display, VA_RT_FORMAT_YUV420,
					m_width, m_height, &surface, 1, nullptr, 0),
					"create surface"))
				return false;
		}
		if (!CheckVA(vaCreateContext(m_display, m_config, m_width, m_height,
				VA_PROGRESSIVE, m_surfaces.data(), m_surfaces.size(), &m_context),
				"create context"))
			return false;

		VAImageFormat imageFormat{.fourcc = VA_FOURCC_NV12};
		if (!CheckVA(vaCreateImage(m_display, &imageFormat, m_width, m_height,
				&m_image), "create NV12 image"))
			return false;
		constexpr uint32 codedBufferSize =
			4 * 1024 * 1024;
		for (auto& codedBuffer : m_codedBuffers)
		{
			if (!CheckVA(vaCreateBuffer(m_display, m_context, VAEncCodedBufferType,
					codedBufferSize, 1, nullptr, &codedBuffer),
					"create coded buffer"))
				return false;
		}

		cemuLog_log(LogType::Force,
			"RTP: streaming gamepad screen at {}x{}, {} fps to {}:{} using VA-API {}.{}",
			m_width, m_height, m_fps, GetConfig().rtp_address.GetValue(), m_port,
			m_majorVersion, m_minorVersion);
		return true;
	}

	void ReleaseEncoder()
	{
		for (VABufferID buffer : m_codedBuffers)
			if (buffer != VA_INVALID_ID)
				vaDestroyBuffer(m_display, buffer);
		if (m_image.image_id != VA_INVALID_ID)
			vaDestroyImage(m_display, m_image.image_id);
		if (m_context != VA_INVALID_ID)
			vaDestroyContext(m_display, m_context);
		for (VASurfaceID surface : m_surfaces)
			if (surface != VA_INVALID_ID)
				vaDestroySurfaces(m_display, &surface, 1);
		if (m_config != VA_INVALID_ID)
			vaDestroyConfig(m_display, m_config);
		if (m_display)
			vaTerminate(m_display);
		if (m_drmFd >= 0)
			close(m_drmFd);
	}

	static bool CheckVA(VAStatus status, std::string_view operation)
	{
		if (status == VA_STATUS_SUCCESS)
			return true;
		cemuLog_log(LogType::Force, "RTP: VA-API {} failed: {}", operation,
			vaErrorStr(status));
		return false;
	}

	bool EncodeFrame(const CaptureFrame& frame)
	{
		void* imageData = nullptr;
		if (!CheckVA(vaMapBuffer(m_display, m_image.buf, &imageData), "map image"))
			return false;
		const auto* source = frame.rgbData.data();
		auto* destination = static_cast<uint8*>(imageData);
		uint8* yPlane = destination + m_image.offsets[0];
		uint8* uvPlane = destination + m_image.offsets[1];
		for (sint32 y = 0; y < m_height; ++y)
		{
			const sint32 sourceY = y * frame.height / m_height;
			for (sint32 x = 0; x < m_width; ++x)
			{
				const sint32 i = (sourceY * frame.width + x * frame.width / m_width) * 3;
				const sint32 r = source[i];
				const sint32 g = source[i + 1];
				const sint32 b = source[i + 2];
				yPlane[y * m_image.pitches[0] + x] =
					static_cast<uint8>((66 * r + 129 * g + 25 * b + 128) / 256 + 16);
			}
		}
		for (sint32 y = 0; y < m_height / 2; ++y)
		{
			const sint32 sourceY = y * 2 * frame.height / m_height;
			for (sint32 x = 0; x < m_width / 2; ++x)
			{
				const sint32 i =
					(sourceY * frame.width + x * 2 * frame.width / m_width) * 3;
				const sint32 r = source[i];
				const sint32 g = source[i + 1];
				const sint32 b = source[i + 2];
				uvPlane[y * m_image.pitches[1] + x * 2] =
					static_cast<uint8>((-38 * r - 74 * g + 112 * b + 128) / 256 + 128);
				uvPlane[y * m_image.pitches[1] + x * 2 + 1] =
					static_cast<uint8>((112 * r - 94 * g - 18 * b + 128) / 256 + 128);
			}
		}
		vaUnmapBuffer(m_display, m_image.buf);

		const VASurfaceID surface = m_surfaces[m_surfaceIndex];
		if (!CheckVA(vaPutImage(m_display, surface, m_image.image_id,
				0, 0, m_width, m_height, 0, 0, m_width, m_height), "upload image"))
			return false;

		const bool idr = m_frameNumber % m_fps == 0;
		std::vector<VABufferID> buffers;
		VABufferID buffer = VA_INVALID_ID;
		if (idr)
		{
			VAEncSequenceParameterBufferH264 seq{};
			seq.level_idc = 40;
			seq.intra_period = m_fps;
			seq.intra_idr_period = m_fps;
			seq.ip_period = 1;
			seq.bits_per_second = m_bitrateKbps * 1000;
			seq.max_num_ref_frames = 1;
			seq.picture_width_in_mbs = (m_width + 15) / 16;
			seq.picture_height_in_mbs = (m_height + 15) / 16;
			seq.seq_fields.bits.chroma_format_idc = 1;
			seq.seq_fields.bits.frame_mbs_only_flag = 1;
			seq.seq_fields.bits.direct_8x8_inference_flag = 1;
			seq.seq_fields.bits.pic_order_cnt_type = 2;
			seq.vui_parameters_present_flag = 1;
			seq.vui_fields.bits.timing_info_present_flag = 1;
			seq.vui_fields.bits.fixed_frame_rate_flag = 1;
			seq.num_units_in_tick = 1;
			seq.time_scale = m_fps * 2;
			seq.frame_cropping_flag =
				(seq.picture_width_in_mbs * 16 != m_width) ||
				(seq.picture_height_in_mbs * 16 != m_height);
			seq.frame_crop_right_offset =
				(seq.picture_width_in_mbs * 16 - m_width) / 2;
			seq.frame_crop_bottom_offset =
				(seq.picture_height_in_mbs * 16 - m_height) / 2;
			if (!CheckVA(vaCreateBuffer(m_display, m_context,
					VAEncSequenceParameterBufferType, sizeof(seq), 1, &seq, &buffer),
					"create sequence buffer"))
				return false;
			buffers.push_back(buffer);
		}

		VAEncPictureParameterBufferH264 picture{};
		picture.CurrPic.picture_id = surface;
		picture.CurrPic.frame_idx = m_frameNumber % 16;
		picture.coded_buf = m_codedBuffers[m_codedBufferIndex];
		picture.frame_num = m_frameNumber % 16;
		picture.pic_init_qp = 26;
		picture.pic_fields.bits.idr_pic_flag = idr;
		picture.pic_fields.bits.reference_pic_flag = 1;
		picture.pic_fields.bits.deblocking_filter_control_present_flag = 1;
		for (auto& reference : picture.ReferenceFrames)
			reference.picture_id = VA_INVALID_ID;
		if (!idr && m_previousSurface != VA_INVALID_ID)
		{
			picture.ReferenceFrames[0].picture_id = m_previousSurface;
			picture.ReferenceFrames[0].frame_idx = (m_frameNumber - 1) % 16;
		}
		if (!CheckVA(vaCreateBuffer(m_display, m_context,
				VAEncPictureParameterBufferType, sizeof(picture), 1, &picture, &buffer),
				"create picture buffer"))
			return false;
		buffers.push_back(buffer);

		VAEncSliceParameterBufferH264 slice{};
		slice.num_macroblocks =
			((m_width + 15) / 16) * ((m_height + 15) / 16);
		slice.macroblock_info = VA_INVALID_ID;
		slice.slice_type = idr ? 2 : 0;
		slice.idr_pic_id = m_frameNumber % 65536;
		for (auto& reference : slice.RefPicList0)
			reference.picture_id = VA_INVALID_ID;
		if (!idr)
			slice.RefPicList0[0] = picture.ReferenceFrames[0];
		if (!CheckVA(vaCreateBuffer(m_display, m_context,
				VAEncSliceParameterBufferType, sizeof(slice), 1, &slice, &buffer),
				"create slice buffer"))
			return false;
		buffers.push_back(buffer);

		VAStatus status = vaBeginPicture(m_display, m_context, surface);
		if (!CheckVA(status, "begin picture") ||
			!CheckVA(vaRenderPicture(m_display, m_context,
				buffers.data(), buffers.size()), "render picture") ||
			!CheckVA(vaEndPicture(m_display, m_context), "end picture") ||
			!CheckVA(vaSyncSurface(m_display, surface), "sync surface"))
		{
			for (VABufferID id : buffers)
				vaDestroyBuffer(m_display, id);
			return false;
		}

		void* codedData = nullptr;
		const VABufferID codedBuffer = m_codedBuffers[m_codedBufferIndex];
		if (CheckVA(vaMapBuffer(m_display, codedBuffer, &codedData), "map coded buffer"))
		{
			const auto* segment = static_cast<const VACodedBufferSegment*>(codedData);
			while (segment && segment->size)
			{
				SendH264Packet(static_cast<const uint8*>(segment->buf),
					segment->size, m_frameNumber, segment->next == nullptr);
				segment = static_cast<const VACodedBufferSegment*>(segment->next);
			}
			vaUnmapBuffer(m_display, codedBuffer);
		}
		for (VABufferID id : buffers)
			vaDestroyBuffer(m_display, id);
		m_previousSurface = surface;
		m_surfaceIndex = (m_surfaceIndex + 1) % m_surfaces.size();
		m_codedBufferIndex = (m_codedBufferIndex + 1) % m_codedBuffers.size();
		++m_frameNumber;
		return true;
	}

	static size_t FindStartCode(const uint8* data, size_t size, size_t& startCodeSize)
	{
		for (size_t i = 0; i + 3 <= size; ++i)
		{
			if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1)
			{
				startCodeSize = 3;
				return i;
			}
			if (i + 4 <= size && data[i] == 0 && data[i + 1] == 0 &&
				data[i + 2] == 0 && data[i + 3] == 1)
			{
				startCodeSize = 4;
				return i;
			}
		}
		startCodeSize = 0;
		return size;
	}

	static bool CollectAnnexBNals(const uint8* data, size_t size,
		std::vector<std::pair<const uint8*, size_t>>& nals)
	{
		size_t startCodeSize;
		size_t start = FindStartCode(data, size, startCodeSize);
		if (startCodeSize == 0)
			return false;

		while (start < size)
		{
			const size_t nalStart = start + startCodeSize;
			size_t endCodeSize;
			const size_t end = FindStartCode(data + nalStart, size - nalStart, endCodeSize);
			const size_t nalEnd = end == size - nalStart ? size : nalStart + end;
			if (nalEnd > nalStart)
				nals.emplace_back(data + nalStart, nalEnd - nalStart);
			start = nalEnd;
			startCodeSize = endCodeSize;
			if (startCodeSize == 0)
				break;
		}
		return !nals.empty();
	}

	static bool CollectAvccNals(const uint8* data, size_t size,
		std::vector<std::pair<const uint8*, size_t>>& nals)
	{
		size_t offset = 0;
		if (size >= 7 && data[0] == 1)
		{
			const uint8 nalLengthSize = (data[4] & 3) + 1;
			offset = 6;
			for (sint32 group = 0; group < 2 && offset < size; ++group)
			{
				const uint8 count = group == 0 ? data[offset - 1] & 0x1F : data[offset++];
				for (uint8 index = 0; index < count && offset + 2 <= size; ++index)
				{
					const size_t length = (static_cast<size_t>(data[offset]) << 8) | data[offset + 1];
					offset += 2;
					if (length == 0 || length > size - offset)
						return false;
					nals.emplace_back(data + offset, length);
					offset += length;
				}
			}
			return !nals.empty() && nalLengthSize >= 1;
		}

		while (offset + 4 <= size)
		{
			const size_t length = (static_cast<size_t>(data[offset]) << 24) |
				(static_cast<size_t>(data[offset + 1]) << 16) |
				(static_cast<size_t>(data[offset + 2]) << 8) |
				data[offset + 3];
			offset += 4;
			if (length == 0 || length > size - offset)
				return false;
			nals.emplace_back(data + offset, length);
			offset += length;
		}
		return offset == size && !nals.empty();
	}

	void SendH264Packet(const uint8* data, size_t size, sint64 pts, bool marker)
	{
		std::vector<std::pair<const uint8*, size_t>> nals;
		if (!CollectAnnexBNals(data, size, nals) && !CollectAvccNals(data, size, nals))
		{
			nals.clear();
			nals.emplace_back(data, size);
		}
		for (size_t index = 0; index < nals.size(); ++index)
		{
			const auto& [nal, nalSize] = nals[index];
			SendNalUnit(nal, nalSize, pts, marker && index + 1 == nals.size());
		}
	}

	void SendNalUnit(const uint8* nal, size_t nalSize, sint64 pts, bool marker)
	{
		if (nalSize == 0)
			return;

		const uint32 timestamp = static_cast<uint32>(std::max<sint64>(pts, 0) * 90000 / m_fps);
		if (nalSize <= kRtpPayloadSize)
		{
			std::array<uint8, 12 + kRtpPayloadSize> packet{};
			WriteRtpHeader(packet.data(), timestamp, marker);
			std::memcpy(packet.data() + 12, nal, nalSize);
			SendPacket(packet.data(), 12 + nalSize);
			return;
		}

		constexpr size_t fragmentPayloadSize = kRtpPayloadSize - 2;
		const uint8 fuIndicator = (nal[0] & 0xE0) | 28;
		const uint8 nalType = nal[0] & 0x1F;
		const uint8* fragment = nal + 1;
		size_t remaining = nalSize - 1;
		bool first = true;
		while (remaining > 0)
		{
			const size_t fragmentSize = std::min(remaining, fragmentPayloadSize);
			const bool last = fragmentSize == remaining;
			std::array<uint8, 12 + kRtpPayloadSize> packet{};
			WriteRtpHeader(packet.data(), timestamp, marker && last);
			packet[12] = fuIndicator;
			packet[13] = nalType | (first ? 0x80 : 0) | (last ? 0x40 : 0);
			std::memcpy(packet.data() + 14, fragment, fragmentSize);
			SendPacket(packet.data(), 14 + fragmentSize);
			fragment += fragmentSize;
			remaining -= fragmentSize;
			first = false;
		}
	}

	void WriteRtpHeader(uint8* header, uint32 timestamp, bool marker)
	{
		header[0] = 0x80;
		header[1] = kRtpPayloadType | (marker ? 0x80 : 0);
		WriteU16BigEndian(header + 2, m_sequence++);
		WriteU32BigEndian(header + 4, timestamp);
		WriteU32BigEndian(header + 8, m_ssrc);
	}

	void SendPacket(const uint8* packet, size_t size)
	{
		if (!m_socket.SendTo(m_address, m_port, packet, size) && !m_sendErrorLogged)
		{
			m_sendErrorLogged = true;
			cemuLog_log(LogType::Force, "RTP: failed to send to {}:{}",
				GetConfig().rtp_address.GetValue(), m_port);
		}
	}

	void Run()
	{
		SetThreadName("RTP-Encoder");
		if (!InitializeEncoder())
			return;

		while (true)
		{
			CaptureFrame frame;
			{
				std::unique_lock lock(m_mutex);
				m_condition.wait(lock, [this]() { return m_stopping || m_pendingFrame.has_value(); });
				if (m_stopping)
					break;
				frame = std::move(*m_pendingFrame);
				m_pendingFrame.reset();
			}

			if (GetConfig().host_gamepad_screen_rtp)
				EncodeFrame(frame);
		}

		ReleaseEncoder();
	}

	UDPSocket m_socket;
	std::thread m_thread;
	std::mutex m_mutex;
	std::condition_variable m_condition;
	std::optional<CaptureFrame> m_pendingFrame;
	bool m_stopping = false;

	int m_drmFd = -1;
	VADisplay m_display{};
	int m_majorVersion{};
	int m_minorVersion{};
	VAProfile m_profile{};
	VAConfigID m_config = VA_INVALID_ID;
	VAContextID m_context = VA_INVALID_ID;
	std::array<VASurfaceID, 2> m_surfaces{VA_INVALID_ID, VA_INVALID_ID};
	std::array<VABufferID, 2> m_codedBuffers{VA_INVALID_ID, VA_INVALID_ID};
	VAImage m_image{.image_id = VA_INVALID_ID};
	uint32 m_surfaceIndex{};
	uint32 m_codedBufferIndex{};
	VASurfaceID m_previousSurface = VA_INVALID_ID;
	uint64 m_frameNumber{};
	uint16 m_sequence{};
	uint32 m_ssrc{};
	SocketIPv4 m_address;
	uint16 m_port;
	sint32 m_width;
	sint32 m_height;
	sint32 m_fps;
	sint32 m_bitrateKbps;
	bool m_sendErrorLogged{};
};
#endif

#if BOOST_OS_WINDOWS
class GamepadRTPWorker
{
public:
	GamepadRTPWorker()
		: m_ssrc(CreateSsrc()),
		  m_address(ResolveAddress()),
		  m_port(static_cast<uint16>(GetConfig().rtp_port)),
		  m_width(static_cast<sint32>(GetConfig().rtp_width)),
		  m_height(static_cast<sint32>(GetConfig().rtp_height)),
		  m_fps(static_cast<sint32>(GetConfig().rtp_fps)),
		  m_bitrateKbps(static_cast<sint32>(GetConfig().rtp_bitrate_kbps))
	{
		m_thread = std::thread(&GamepadRTPWorker::Run, this);
	}

	~GamepadRTPWorker()
	{
		{
			std::scoped_lock lock(m_mutex);
			m_stopping = true;
		}
		m_condition.notify_all();
		m_thread.join();
	}

	void Submit(CaptureFrame&& frame)
	{
		{
			std::scoped_lock lock(m_mutex);
			m_pendingFrame = std::move(frame);
		}
		m_condition.notify_one();
	}

private:
	static uint32 CreateSsrc()
	{
		std::random_device randomDevice;
		return static_cast<uint32>(randomDevice());
	}

	static SocketIPv4 ResolveAddress()
	{
		return UDPSocket::Resolve(GetConfig().rtp_address.GetValue()).value_or(0);
	}

	static void LogHRESULT(std::string_view operation, HRESULT result)
	{
		cemuLog_log(LogType::Force, "RTP: Media Foundation {} failed: 0x{:08x}",
			operation, static_cast<uint32>(result));
	}

	static HRESULT SetProperty(IMFAttributes* attributes, const GUID& key, uint32 value)
	{
		return attributes->SetUINT32(key, value);
	}

	bool InitializeEncoder()
	{
		HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		m_comInitialized = SUCCEEDED(result);
		result = MFStartup(MF_VERSION);
		if (FAILED(result))
		{
			LogHRESULT("startup", result);
			return false;
		}
		m_mfInitialized = true;

		result = CoCreateInstance(CLSID_CMSH264EncoderMFT, nullptr, CLSCTX_INPROC_SERVER,
			__uuidof(IMFTransform), reinterpret_cast<void**>(m_transform.GetAddressOf()));
		if (FAILED(result))
		{
			LogHRESULT("H.264 encoder creation", result);
			return false;
		}

		Microsoft::WRL::ComPtr<IMFMediaType> outputType;
		MFCreateMediaType(&outputType);
		outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
		outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
		MFSetAttributeSize(outputType.Get(), MF_MT_FRAME_SIZE, m_width, m_height);
		MFSetAttributeRatio(outputType.Get(), MF_MT_FRAME_RATE, m_fps, 1);
		outputType->SetUINT32(MF_MT_AVG_BITRATE, m_bitrateKbps * 1000);
		outputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
		outputType->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_Base);
		outputType->SetUINT32(MF_MT_AVG_BITRATE, m_bitrateKbps * 1000);
		MFSetAttributeRatio(outputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
		Microsoft::WRL::ComPtr<ICodecAPI> codec;
		if (SUCCEEDED(m_transform.As(&codec)) && codec)
		{
			VARIANT value{};
			value.vt = VT_BOOL;
			value.boolVal = VARIANT_TRUE;
			codec->SetValue(&CODECAPI_AVLowLatencyMode, &value);
			value.vt = VT_UI4;
			value.ulVal = eAVEncCommonRateControlMode_CBR;
			codec->SetValue(&CODECAPI_AVEncCommonRateControlMode, &value);
			value.ulVal = static_cast<ULONG>(m_bitrateKbps * 1000);
			codec->SetValue(&CODECAPI_AVEncCommonMeanBitRate, &value);
			value.ulVal = 0;
			codec->SetValue(&CODECAPI_AVEncMPVDefaultBPictureCount, &value);
		}
		result = m_transform->SetOutputType(0, outputType.Get(), 0);
		if (FAILED(result))
		{
			LogHRESULT("H.264 output configuration", result);
			return false;
		}

		Microsoft::WRL::ComPtr<IMFMediaType> inputType;
		MFCreateMediaType(&inputType);
		inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
		inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
		MFSetAttributeSize(inputType.Get(), MF_MT_FRAME_SIZE, m_width, m_height);
		MFSetAttributeRatio(inputType.Get(), MF_MT_FRAME_RATE, m_fps, 1);
		MFSetAttributeRatio(inputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
		SetProperty(inputType.Get(), MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
		result = m_transform->SetInputType(0, inputType.Get(), 0);
		if (FAILED(result))
		{
			LogHRESULT("NV12 input configuration", result);
			return false;
		}

		m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
		m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
		m_nv12.resize(static_cast<size_t>(m_width) * m_height * 3 / 2);
		cemuLog_log(LogType::Force,
			"RTP: streaming gamepad screen at {}x{}, {} fps to {}:{} using Media Foundation H.264",
			m_width, m_height, m_fps, GetConfig().rtp_address.GetValue(), m_port);
		return true;
	}

	void ConvertRGBToNV12(const CaptureFrame& frame)
	{
		uint8* yPlane = m_nv12.data();
		uint8* uvPlane = yPlane + static_cast<size_t>(m_width) * m_height;
		const auto rgb = frame.rgbData.data();
		for (sint32 y = 0; y < m_height; ++y)
		{
			const sint32 sy = y * frame.height / m_height;
			for (sint32 x = 0; x < m_width; ++x)
			{
				const sint32 sx = x * frame.width / m_width;
				const sint32 index = (sy * frame.width + sx) * 3;
				const sint32 red = rgb[index];
				const sint32 green = rgb[index + 1];
				const sint32 blue = rgb[index + 2];
				yPlane[y * m_width + x] = static_cast<uint8>(std::clamp(
					(66 * red + 129 * green + 25 * blue + 128) / 256 + 16, 0, 255));
			}
		}
		for (sint32 y = 0; y < m_height / 2; ++y)
		{
			const sint32 sy = y * 2 * frame.height / m_height;
			for (sint32 x = 0; x < m_width / 2; ++x)
			{
				const sint32 sx = x * 2 * frame.width / m_width;
				const sint32 index = (sy * frame.width + sx) * 3;
				const sint32 red = rgb[index];
				const sint32 green = rgb[index + 1];
				const sint32 blue = rgb[index + 2];
				const sint32 offset = (y * (m_width / 2) + x) * 2;
				uvPlane[offset] = static_cast<uint8>(std::clamp(
					(-38 * red - 74 * green + 112 * blue + 128) / 256 + 128, 0, 255));
				uvPlane[offset + 1] = static_cast<uint8>(std::clamp(
					(112 * red - 94 * green - 18 * blue + 128) / 256 + 128, 0, 255));
			}
		}
	}

	void ProcessOutput(sint64 timestamp)
	{
		MFT_OUTPUT_STREAM_INFO outputInfo{};
		if (FAILED(m_transform->GetOutputStreamInfo(0, &outputInfo)))
			return;
		Microsoft::WRL::ComPtr<IMFSample> sample;
		MFCreateSample(&sample);
		Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
		MFCreateMemoryBuffer(std::max<DWORD>(outputInfo.cbSize, 1), &buffer);
		sample->AddBuffer(buffer.Get());

		MFT_OUTPUT_DATA_BUFFER output{};
		output.pSample = sample.Get();
		DWORD status = 0;
		HRESULT result = m_transform->ProcessOutput(0, 1, &output, &status);
		if (result == MF_E_TRANSFORM_NEED_MORE_INPUT)
			return;
		if (FAILED(result))
		{
			if (!m_outputErrorLogged)
			{
				m_outputErrorLogged = true;
				LogHRESULT("frame output", result);
			}
			return;
		}

		IMFMediaBuffer* encodedBuffer = nullptr;
		if (SUCCEEDED(sample->ConvertToContiguousBuffer(&encodedBuffer)))
		{
			BYTE* data = nullptr;
			DWORD size = 0;
			if (SUCCEEDED(encodedBuffer->Lock(&data, nullptr, nullptr)))
			{
				SendH264(data, sample->GetTotalLength(&size) == S_OK ? size : 0, timestamp);
				encodedBuffer->Unlock();
			}
			encodedBuffer->Release();
		}
		if (output.pEvents)
			output.pEvents->Release();
	}

	bool EncodeFrame(const CaptureFrame& frame)
	{
		ConvertRGBToNV12(frame);
		Microsoft::WRL::ComPtr<IMFSample> sample;
		if (FAILED(MFCreateSample(&sample)))
			return false;
		Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
		if (FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(m_nv12.size()), &buffer)))
			return false;
		BYTE* destination = nullptr;
		if (FAILED(buffer->Lock(&destination, nullptr, nullptr)))
			return false;
		std::memcpy(destination, m_nv12.data(), m_nv12.size());
		buffer->Unlock();
		buffer->SetCurrentLength(static_cast<DWORD>(m_nv12.size()));
		sample->AddBuffer(buffer.Get());
		const LONGLONG timestamp = m_frameNumber * 10000000LL / m_fps;
		sample->SetSampleTime(timestamp);
		sample->SetSampleDuration(10000000LL / m_fps);
		const HRESULT result = m_transform->ProcessInput(0, sample.Get(), 0);
		if (FAILED(result))
			return false;
		++m_frameNumber;
		ProcessOutput(timestamp / 111);
		return true;
	}

	void SendH264(const uint8* data, size_t size, sint64 timestampUs)
	{
		size_t start = 0;
		while (start + 4 <= size)
		{
			size_t code = data[start] == 0 && data[start + 1] == 0 && data[start + 2] == 1 ? 3 :
				(data[start] == 0 && data[start + 1] == 0 && data[start + 2] == 0 &&
					data[start + 3] == 1 ? 4 : 0);
			if (code == 0)
			{
				++start;
				continue;
			}
			const size_t nalStart = start + code;
			size_t nalEnd = size;
			for (size_t index = nalStart + 1; index + 3 <= size; ++index)
			{
				if (data[index] == 0 && data[index + 1] == 0 &&
					(data[index + 2] == 1 || (data[index + 2] == 0 &&
						index + 4 <= size && data[index + 3] == 1)))
				{
					nalEnd = index;
					break;
				}
			}
			SendNAL(data + nalStart, nalEnd - nalStart,
				timestampUs, nalEnd == size);
			start = nalEnd;
		}
	}

	void SendNAL(const uint8* nal, size_t size, sint64 timestampUs, bool marker)
	{
		const uint32 timestamp = static_cast<uint32>(timestampUs * 90 / 1000);
		if (size <= kRtpPayloadSize)
		{
			std::array<uint8, 12 + kRtpPayloadSize> packet{};
			WriteHeader(packet.data(), timestamp, marker);
			std::memcpy(packet.data() + 12, nal, size);
			m_socket.SendTo(m_address, m_port, packet.data(), 12 + size);
			return;
		}
		const uint8 indicator = (nal[0] & 0xE0) | 28;
		const uint8 type = nal[0] & 0x1F;
		size_t remaining = size - 1;
		const uint8* fragment = nal + 1;
		bool first = true;
		while (remaining)
		{
			const size_t count = std::min<size_t>(remaining, kRtpPayloadSize - 2);
			const bool last = count == remaining;
			std::array<uint8, 12 + kRtpPayloadSize> packet{};
			WriteHeader(packet.data(), timestamp, marker && last);
			packet[12] = indicator;
			packet[13] = type | (first ? 0x80 : 0) | (last ? 0x40 : 0);
			std::memcpy(packet.data() + 14, fragment, count);
			m_socket.SendTo(m_address, m_port, packet.data(), 14 + count);
			fragment += count;
			remaining -= count;
			first = false;
		}
	}

	void WriteHeader(uint8* header, uint32 timestamp, bool marker)
	{
		header[0] = 0x80;
		header[1] = kRtpPayloadType | (marker ? 0x80 : 0);
		WriteU16BigEndian(header + 2, m_sequence++);
		WriteU32BigEndian(header + 4, timestamp);
		WriteU32BigEndian(header + 8, m_ssrc);
	}

	void Run()
	{
		SetThreadName("RTP-MFEncoder");
		if (!InitializeEncoder())
		{
			ShutdownEncoder();
			return;
		}
		while (true)
		{
			CaptureFrame frame;
			{
				std::unique_lock lock(m_mutex);
				m_condition.wait(lock, [this]() {
					return m_stopping || m_pendingFrame.has_value();
				});
				if (m_stopping)
					break;
				frame = std::move(*m_pendingFrame);
				m_pendingFrame.reset();
			}
			EncodeFrame(frame);
		}
		ShutdownEncoder();
	}

	void ShutdownEncoder()
	{
		m_transform.Reset();
		if (m_mfInitialized)
			MFShutdown();
		if (m_comInitialized)
			CoUninitialize();
	}

	UDPSocket m_socket;
	std::thread m_thread;
	std::mutex m_mutex;
	std::condition_variable m_condition;
	std::optional<CaptureFrame> m_pendingFrame;
	std::atomic_bool m_stopping{};
	Microsoft::WRL::ComPtr<IMFTransform> m_transform;
	std::vector<uint8> m_nv12;
	uint32 m_ssrc{};
	uint16 m_sequence{};
	uint64 m_frameNumber{};
	SocketIPv4 m_address;
	uint16 m_port;
	sint32 m_width;
	sint32 m_height;
	sint32 m_fps;
	sint32 m_bitrateKbps;
	bool m_mfInitialized{};
	bool m_comInitialized{};
	bool m_outputErrorLogged{};
};
#endif

class GamepadAudioRTPWorker
{
	static constexpr sint32 kSampleRate = 48000;
	static constexpr sint32 kChannels = 2;
	static constexpr sint32 kFrameSamples = 480;
	static constexpr size_t kEncodedCapacity = 1500;
	static constexpr uint8 kAudioPayloadType = 97;

public:
	GamepadAudioRTPWorker()
		: m_address(ResolveAddress()),
		  m_port(static_cast<uint16>(GetConfig().rtp_port) + 2),
		  m_ssrc(CreateSsrc())
	{
		sint32 error = 0;
		m_encoder = opus_encoder_create(kSampleRate, kChannels,
			OPUS_APPLICATION_RESTRICTED_LOWDELAY, &error);
		if (error != OPUS_OK || !m_encoder)
		{
			cemuLog_log(LogType::Force, "RTP: failed to initialize Opus encoder: {}",
				opus_strerror(error));
			return;
		}
		opus_encoder_ctl(m_encoder, OPUS_SET_BITRATE(96000));
		opus_encoder_ctl(m_encoder, OPUS_SET_COMPLEXITY(1));
		opus_encoder_ctl(m_encoder, OPUS_SET_SIGNAL(OPUS_AUTO));
		m_thread = std::thread(&GamepadAudioRTPWorker::Run, this);
	}

	~GamepadAudioRTPWorker()
	{
		{
			std::scoped_lock lock(m_mutex);
			m_stopping = true;
		}
		m_condition.notify_all();
		if (m_thread.joinable())
			m_thread.join();
		opus_encoder_destroy(m_encoder);
	}

	void Submit(const sint16* samples, sint32 sampleCount, sint32 channels)
	{
		if (!m_encoder || !samples || sampleCount <= 0 || channels <= 0)
			return;

		std::vector<sint16> stereo;
		stereo.reserve(sampleCount * kChannels);
		for (sint32 sample = 0; sample < sampleCount; ++sample)
		{
			stereo.emplace_back(samples[sample * channels]);
			stereo.emplace_back(channels > 1 ? samples[sample * channels + 1] :
				samples[sample * channels]);
		}
		{
			std::scoped_lock lock(m_mutex);
			m_pendingPcm.insert(m_pendingPcm.end(),
				stereo.begin(), stereo.end());
			constexpr size_t maxQueuedSamples = kSampleRate * kChannels / 10;
			if (m_pendingPcm.size() > maxQueuedSamples)
			{
				m_pendingPcm.erase(m_pendingPcm.begin(),
					m_pendingPcm.end() - maxQueuedSamples);
			}
		}
		m_condition.notify_one();
	}

private:
	static uint32 CreateSsrc()
	{
		std::random_device randomDevice;
		return static_cast<uint32>(randomDevice());
	}

	static SocketIPv4 ResolveAddress()
	{
		const auto address = UDPSocket::Resolve(GetConfig().rtp_address.GetValue());
		return address.value_or(0);
	}

	void Run()
	{
		SetThreadName("RTP-Opus");
		while (true)
		{
			std::vector<sint16> pcm;
			{
				std::unique_lock lock(m_mutex);
				m_condition.wait(lock, [this]() { return m_stopping || !m_pendingPcm.empty(); });
				if (m_stopping)
					break;
				pcm.swap(m_pendingPcm);
			}
			AppendAndEncode(pcm);
		}
	}

	void AppendAndEncode(const std::vector<sint16>& pcm)
	{
		const sint32 samples = static_cast<sint32>(pcm.size() / kChannels);
		for (sint32 sample = 0; sample < samples; ++sample)
		{
			m_pcm[m_pcmSamples * kChannels] = pcm[sample * kChannels];
			m_pcm[m_pcmSamples * kChannels + 1] = pcm[sample * kChannels + 1];
			++m_pcmSamples;
			if (m_pcmSamples == kFrameSamples)
				EncodeAndSend();
		}
	}

	void EncodeAndSend()
	{
		const sint32 length = opus_encode(m_encoder, m_pcm.data(), kFrameSamples,
			m_encoded.data(), m_encoded.size());
		m_pcmSamples = 0;
		if (length <= 0)
		{
			if (!m_errorLogged)
			{
				m_errorLogged = true;
				cemuLog_log(LogType::Force, "RTP: Opus encode failed: {}",
					opus_strerror(length));
			}
			return;
		}

		std::array<uint8, 12 + kEncodedCapacity> packet{};
		packet[0] = 0x80;
		packet[1] = 0x80 | kAudioPayloadType;
		WriteU16BigEndian(packet.data() + 2, m_sequence++);
		WriteU32BigEndian(packet.data() + 4, m_timestamp);
		WriteU32BigEndian(packet.data() + 8, m_ssrc);
		std::memcpy(packet.data() + 12, m_encoded.data(), length);
		m_socket.SendTo(m_address, m_port, packet.data(), 12 + length);
		m_timestamp += kFrameSamples;
	}

	UDPSocket m_socket;
	SocketIPv4 m_address;
	uint16 m_port;
	uint32 m_ssrc;
	uint16 m_sequence{};
	uint32 m_timestamp{};
	OpusEncoder* m_encoder{};
	std::mutex m_mutex;
	std::condition_variable m_condition;
	std::thread m_thread;
	bool m_stopping{};
	std::vector<sint16> m_pendingPcm;
	std::array<sint16, kFrameSamples * kChannels> m_pcm{};
	std::array<uint8, kEncodedCapacity> m_encoded{};
	sint32 m_pcmSamples{};
	bool m_errorLogged{};
};

std::unique_ptr<GamepadAudioRTPWorker> s_audioWorker;
std::mutex s_audioWorkerMutex;
std::atomic<uint64> s_lastCaptureUs{};
#if !BOOST_OS_MACOS
std::unique_ptr<GamepadRTPWorker> s_worker;
std::mutex s_workerMutex;
#endif
#if BOOST_OS_MACOS
std::unique_ptr<MetalVideoToolboxStream> s_metalWorker;
std::mutex s_metalWorkerMutex;
#endif

#if !BOOST_OS_MACOS
GamepadRTPWorker& GetWorker()
{
	std::scoped_lock lock(s_workerMutex);
	if (!s_worker)
		s_worker = std::make_unique<GamepadRTPWorker>();
	return *s_worker;
}
#endif

#if BOOST_OS_MACOS
MetalVideoToolboxStream& GetMetalWorker()
{
	std::scoped_lock lock(s_metalWorkerMutex);
	if (!s_metalWorker)
		s_metalWorker = std::make_unique<MetalVideoToolboxStream>();
	return *s_metalWorker;
}
#endif
}

namespace GamepadRTPStream
{
void Shutdown()
{
#if BOOST_OS_MACOS
	{
		std::scoped_lock lock(s_metalWorkerMutex);
		s_metalWorker.reset();
	}
#endif
#if !BOOST_OS_MACOS
	{
		std::scoped_lock lock(s_workerMutex);
		s_worker.reset();
	}
#endif
	{
		std::scoped_lock lock(s_audioWorkerMutex);
		s_audioWorker.reset();
	}
	s_lastCaptureUs = 0;
}

bool ShouldCaptureFrame()
{
	if (!GetConfig().host_gamepad_screen_rtp)
		return false;

	const uint64 frameIntervalUs =
		1000000 / std::max<sint32>(GetConfig().rtp_fps, 1) - 1000;
	const uint64 now = HighResolutionTimer::ticksToMicroseconds(HighResolutionTimer::now().getTick());
	uint64 previous = s_lastCaptureUs.load(std::memory_order_relaxed);
	if (frameIntervalUs > 0 && (now < previous || now - previous < frameIntervalUs))
		return false;

	return s_lastCaptureUs.compare_exchange_strong(
		previous, now, std::memory_order_relaxed, std::memory_order_relaxed);
}

void SubmitFrame(std::vector<uint8>&& rgbData, sint32 width, sint32 height)
{
	if (!GetConfig().host_gamepad_screen_rtp || rgbData.empty() || width <= 0 || height <= 0)
		return;
#if BOOST_OS_MACOS
	GetMetalWorker().SubmitRGB({std::move(rgbData), width, height});
#else
	GetWorker().Submit({std::move(rgbData), width, height});
#endif
}

void SubmitAudio(const sint16* samples, sint32 sampleCount, sint32 channels)
{
	if (!GetConfig().host_gamepad_screen_rtp)
		return;
	std::scoped_lock lock(s_audioWorkerMutex);
	if (!s_audioWorker)
		s_audioWorker = std::make_unique<GamepadAudioRTPWorker>();
	s_audioWorker->Submit(samples, sampleCount, channels);
}

#if BOOST_OS_MACOS
void SubmitMetalFrame(MetalRenderer* renderer, MTL::Texture* texture)
{
	if (!GetConfig().host_gamepad_screen_rtp || !renderer || !texture)
		return;
	GetMetalWorker().Submit(renderer, texture);
}
#endif
}
