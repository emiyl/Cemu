#include "Cafe/HW/Latte/Renderer/GamepadRTPStream.h"

#include "config/CemuConfig.h"
#include "util/helpers/helpers.h"
#include "util/Socket/Socket.h"
#include "util/highresolutiontimer/HighResolutionTimer.h"

#include <random>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace
{
constexpr sint32 kVideoWidth = 854;
constexpr sint32 kVideoHeight = 480;
constexpr sint32 kVideoFps = 60;
constexpr sint32 kFrameIntervalUs = 1000000 / kVideoFps - 1000;
constexpr sint32 kRtpPayloadSize = 1200;
constexpr uint8 kRtpPayloadType = 96;
constexpr uint16 kRtpPort = 5004;
constexpr SocketIPv4 kRtpMulticastAddress = 0xEFFF0001; // 239.255.0.1

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

class GamepadRTPWorker
{
public:
	GamepadRTPWorker()
		: m_socket{},
		  m_ssrc(CreateSsrc())
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

	static const AVCodec* FindEncoder()
	{
#if BOOST_OS_MACOS
		if (const AVCodec* codec = avcodec_find_encoder_by_name("h264_videotoolbox"))
			return codec;
#elif BOOST_OS_WINDOWS
		if (const AVCodec* codec = avcodec_find_encoder_by_name("h264_mf"))
			return codec;
#endif
		if (const AVCodec* codec = avcodec_find_encoder_by_name("libx264"))
			return codec;
		return avcodec_find_encoder(AV_CODEC_ID_H264);
	}

	bool InitializeEncoder()
	{
		const AVCodec* codec = FindEncoder();
		if (!codec)
		{
			cemuLog_log(LogType::Force, "RTP: no H.264 encoder is available");
			return false;
		}

		m_codecContext = avcodec_alloc_context3(codec);
		m_frame = av_frame_alloc();
		m_packet = av_packet_alloc();
		if (!m_codecContext || !m_frame || !m_packet)
		{
			ReleaseEncoder();
			return false;
		}

		m_codecContext->width = kVideoWidth;
		m_codecContext->height = kVideoHeight;
		const char* codecName = codec->name ? codec->name : "";
		const AVPixelFormat outputFormat =
			std::string_view(codecName) == "h264_videotoolbox" ||
			std::string_view(codecName) == "h264_mf"
				? AV_PIX_FMT_NV12
				: AV_PIX_FMT_YUV420P;
		m_codecContext->pix_fmt = outputFormat;
		m_codecContext->time_base = {1, kVideoFps};
		m_codecContext->framerate = {kVideoFps, 1};
		m_codecContext->bit_rate = 6'000'000;
		m_codecContext->gop_size = kVideoFps;
		m_codecContext->max_b_frames = 0;
		m_codecContext->flags |= AV_CODEC_FLAG_LOW_DELAY;
		m_codecContext->refs = 1;
		m_codecContext->thread_count = 1;

		if (std::string_view(codecName) == "h264_videotoolbox")
		{
			av_opt_set(m_codecContext->priv_data, "realtime", "1", 0);
			av_opt_set(m_codecContext->priv_data, "allow_sw", "1", 0);
			av_opt_set(m_codecContext->priv_data, "prio_speed", "1", 0);
			av_opt_set(m_codecContext->priv_data, "max_ref_frames", "1", 0);
			av_opt_set(m_codecContext->priv_data, "profile", "baseline", 0);
		}
		else if (std::string_view(codecName) == "libx264")
		{
			av_opt_set(m_codecContext->priv_data, "preset", "ultrafast", 0);
			av_opt_set(m_codecContext->priv_data, "tune", "zerolatency", 0);
			av_opt_set(m_codecContext->priv_data, "repeat-headers", "1", 0);
			av_opt_set(m_codecContext->priv_data, "bframes", "0", 0);
			av_opt_set(m_codecContext->priv_data, "rc-lookahead", "0", 0);
			av_opt_set(m_codecContext->priv_data, "sync-lookahead", "0", 0);
			av_opt_set(m_codecContext->priv_data, "refs", "1", 0);
			av_opt_set(m_codecContext->priv_data, "intra-refresh", "1", 0);
		}

		const sint32 error = avcodec_open2(m_codecContext, codec, nullptr);
		if (error < 0)
		{
			LogCodecError("failed to initialize encoder", error);
			ReleaseEncoder();
			return false;
		}

		m_frame->format = m_codecContext->pix_fmt;
		m_frame->width = m_codecContext->width;
		m_frame->height = m_codecContext->height;
		if (av_frame_get_buffer(m_frame, 32) < 0)
		{
			ReleaseEncoder();
			return false;
		}

		cemuLog_log(LogType::Force, "RTP: streaming gamepad screen at 854x480, 60 fps to 239.255.0.1:5004 using {}", codecName);
		return true;
	}

	void ReleaseEncoder()
	{
		av_packet_free(&m_packet);
		av_frame_free(&m_frame);
		avcodec_free_context(&m_codecContext);
		sws_freeContext(m_scaler);
		m_scaler = nullptr;
	}

	void LogCodecError(std::string_view message, sint32 error)
	{
		char errorBuffer[AV_ERROR_MAX_STRING_SIZE]{};
		av_strerror(error, errorBuffer, sizeof(errorBuffer));
		cemuLog_log(LogType::Force, "RTP: {}: {}", message, errorBuffer);
	}

	bool EncodeFrame(const CaptureFrame& frame)
	{
		if (!m_scaler ||
			m_sourceWidth != frame.width ||
			m_sourceHeight != frame.height)
		{
			sws_freeContext(m_scaler);
			m_scaler = sws_getContext(
				frame.width, frame.height, AV_PIX_FMT_RGB24,
				kVideoWidth, kVideoHeight, m_codecContext->pix_fmt,
				SWS_BILINEAR, nullptr, nullptr, nullptr);
			m_sourceWidth = frame.width;
			m_sourceHeight = frame.height;
		}

		if (!m_scaler || av_frame_make_writable(m_frame) < 0)
			return false;

		const uint8* source[] = {frame.rgbData.data(), nullptr, nullptr, nullptr};
		const sint32 sourceStride[] = {frame.width * 3, 0, 0, 0};
		if (sws_scale(m_scaler, source, sourceStride, 0, frame.height,
					  m_frame->data, m_frame->linesize) != kVideoHeight)
		{
			return false;
		}

		m_frame->pts = m_frameNumber++;
		sint32 error = avcodec_send_frame(m_codecContext, m_frame);
		if (error < 0)
		{
			LogCodecError("failed to submit frame to encoder", error);
			return false;
		}

		while (true)
		{
			error = avcodec_receive_packet(m_codecContext, m_packet);
			if (error == AVERROR(EAGAIN) || error == AVERROR_EOF)
				return true;
			if (error < 0)
			{
				LogCodecError("failed to receive encoded frame", error);
				return false;
			}

			const sint64 pts = m_packet->pts != AV_NOPTS_VALUE ? m_packet->pts : m_frameNumber - 1;
			if ((m_packet->flags & AV_PKT_FLAG_KEY) && m_codecContext->extradata &&
				m_codecContext->extradata_size > 0)
			{
				SendH264Packet(m_codecContext->extradata,
					static_cast<size_t>(m_codecContext->extradata_size), pts, false);
			}
			SendH264Packet(m_packet->data, static_cast<size_t>(m_packet->size), pts, true);
			av_packet_unref(m_packet);
		}
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

		const uint32 timestamp = static_cast<uint32>(std::max<sint64>(pts, 0) * 90000 / kVideoFps);
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
		if (!m_socket.SendTo(kRtpMulticastAddress, kRtpPort, packet, size) && !m_sendErrorLogged)
		{
			m_sendErrorLogged = true;
			cemuLog_log(LogType::Force, "RTP: failed to send to 239.255.0.1:5004");
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

	AVCodecContext* m_codecContext{};
	AVFrame* m_frame{};
	AVPacket* m_packet{};
	SwsContext* m_scaler{};
	sint32 m_sourceWidth{};
	sint32 m_sourceHeight{};
	sint64 m_frameNumber{};
	uint16 m_sequence{};
	uint32 m_ssrc{};
	bool m_sendErrorLogged{};
};

std::unique_ptr<GamepadRTPWorker> s_worker;
std::mutex s_workerMutex;
std::atomic<uint64> s_lastCaptureUs{};

GamepadRTPWorker& GetWorker()
{
	std::scoped_lock lock(s_workerMutex);
	if (!s_worker)
		s_worker = std::make_unique<GamepadRTPWorker>();
	return *s_worker;
}
}

namespace GamepadRTPStream
{
bool ShouldCaptureFrame()
{
	if (!GetConfig().host_gamepad_screen_rtp)
		return false;

	const uint64 now = HighResolutionTimer::ticksToMicroseconds(HighResolutionTimer::now().getTick());
	uint64 previous = s_lastCaptureUs.load(std::memory_order_relaxed);
	if (now < previous || now - previous < kFrameIntervalUs)
		return false;

	return s_lastCaptureUs.compare_exchange_strong(
		previous, now, std::memory_order_relaxed, std::memory_order_relaxed);
}

void SubmitFrame(std::vector<uint8>&& rgbData, sint32 width, sint32 height)
{
	if (!GetConfig().host_gamepad_screen_rtp || rgbData.empty() || width <= 0 || height <= 0)
		return;
	GetWorker().Submit({std::move(rgbData), width, height});
}
}
