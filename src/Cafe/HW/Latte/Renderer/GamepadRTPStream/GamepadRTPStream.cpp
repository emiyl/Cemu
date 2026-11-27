#include "Cafe/HW/Latte/Renderer/GamepadRTPStream/GamepadRTPStream.h"
#include "Cafe/HW/Latte/Renderer/GamepadRTPStream/Platform.h"
#include "config/CemuConfig.h"
#include "util/helpers/helpers.h"
#include "util/Socket/Socket.h"
#include "util/highresolutiontimer/HighResolutionTimer.h"
#include <opus/opus.h>
#include <random>

namespace
{
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

}

std::unique_ptr<GamepadAudioRTPWorker> s_audioWorker;
std::mutex s_audioWorkerMutex;
std::atomic<uint64> s_lastCaptureUs{};
namespace GamepadRTPStream
{
void Shutdown()
{
	GamepadRTPStream::Platform::Shutdown();
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
	GamepadRTPStream::Platform::SubmitFrame(std::move(rgbData), width, height);
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
	GamepadRTPStream::Platform::SubmitMetalFrame(renderer, texture);
}
#endif
}