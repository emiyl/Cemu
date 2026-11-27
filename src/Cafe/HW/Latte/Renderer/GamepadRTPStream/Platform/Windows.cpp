#if BOOST_OS_WINDOWS
#include "Cafe/HW/Latte/Renderer/GamepadRTPStream/Platform.h"
#define WIN32_LEAN_AND_MEAN
#include <codecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>
#include <random>

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

#if BOOST_OS_WINDOWS
namespace GamepadRTPStream::Platform
{
static std::unique_ptr<GamepadRTPWorker> s_worker;
void Shutdown() { s_worker.reset(); }
void SubmitFrame(std::vector<uint8>&& rgbData, sint32 width, sint32 height)
{
	if (!s_worker) s_worker = std::make_unique<GamepadRTPWorker>();
	CaptureFrame frame{std::move(rgbData), width, height};
	s_worker->Submit(std::move(frame));
}
}
#endif


