#if BOOST_OS_MACOS
#include "Cafe/HW/Latte/Renderer/GamepadRTPStream/Platform.h"
#include "Cafe/HW/Latte/Renderer/GamepadRTPStream/GamepadRTPStream.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalRenderer.h"
#include <CoreMedia/CMBlockBuffer.h>
#include <CoreMedia/CMSampleBuffer.h>
#include <CoreVideo/CVPixelBuffer.h>
#include <IOSurface/IOSurfaceRef.h>
#include <VideoToolbox/VideoToolbox.h>
#include <random>

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
			{
				char keyName[256]{};
				if (CFStringGetCString(key, keyName, sizeof(keyName), kCFStringEncodingUTF8))
					cemuLog_log(LogType::Force,
						"RTP: failed to set VideoToolbox property '{}': {}",
						keyName, result);
				else
					cemuLog_log(LogType::Force,
						"RTP: failed to set VideoToolbox property: {}", result);
			}
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

#if BOOST_OS_MACOS
namespace GamepadRTPStream::Platform
{
static std::unique_ptr<MetalVideoToolboxStream> s_worker;
void Shutdown() { s_worker.reset(); }
void SubmitFrame(std::vector<uint8>&& rgbData, sint32 width, sint32 height)
{
	if (!s_worker) s_worker = std::make_unique<MetalVideoToolboxStream>();
	CaptureFrame frame{std::move(rgbData), width, height};
	s_worker->SubmitRGB(std::move(frame));
}
void SubmitMetalFrame(MetalRenderer* renderer, MTL::Texture* texture)
{
	if (!renderer || !texture) return;
	if (!s_worker) s_worker = std::make_unique<MetalVideoToolboxStream>();
	s_worker->Submit(renderer, texture);
}
}
#endif


