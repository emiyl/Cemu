#if BOOST_OS_LINUX
#include "Cafe/HW/Latte/Renderer/GamepadRTPStream/Platform.h"
#include <dlfcn.h>
#include <fcntl.h>
#include <va/va.h>
#include <va/va_drm.h>
#include <va/va_enc_h264.h>
#include <dynlink_cuda.h>
#include <nvEncodeAPI.h>
#include <unistd.h>
#include <random>

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
		if (InitializeNVENC())
		{
			m_nvenc = true;
			return true;
		}
		ReleaseNVENC();
		return InitializeVAAPI();
	}

	bool InitializeNVENC()
	{
		m_cudaLibrary = dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
		m_nvencLibrary = dlopen("libnvidia-encode.so.1", RTLD_NOW | RTLD_LOCAL);
		if (!m_cudaLibrary || !m_nvencLibrary)
			return false;

		auto Load = [](void* library, const char* name) {
			return dlsym(library, name);
		};
		m_cuInit = reinterpret_cast<tcuInit*>(Load(m_cudaLibrary, "cuInit"));
		m_cuDeviceGet = reinterpret_cast<tcuDeviceGet*>(
			Load(m_cudaLibrary, "cuDeviceGet"));
		m_cuCtxCreate = reinterpret_cast<tcuCtxCreate_v2*>(
			Load(m_cudaLibrary, "cuCtxCreate_v2"));
		m_cuCtxDestroy = reinterpret_cast<tcuCtxDestroy_v2*>(
			Load(m_cudaLibrary, "cuCtxDestroy_v2"));
		auto createInstance = reinterpret_cast<NVENCSTATUS (*)(NV_ENCODE_API_FUNCTION_LIST*)>(
			Load(m_nvencLibrary, "NvEncodeAPICreateInstance"));
		if (!m_cuInit || !m_cuDeviceGet || !m_cuCtxCreate || !m_cuCtxDestroy ||
			!createInstance || m_cuInit(0) != CUDA_SUCCESS)
			return false;

		CUdevice device = 0;
		if (m_cuDeviceGet(&device, 0) != CUDA_SUCCESS ||
			m_cuCtxCreate(&m_cudaContext, 0, device) != CUDA_SUCCESS)
			return false;

		m_nvencFunctions = {};
		m_nvencFunctions.version = NV_ENCODE_API_FUNCTION_LIST_VER;
		if (createInstance(&m_nvencFunctions) != NV_ENC_SUCCESS)
			return false;

		NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS session{};
		session.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
		session.device = m_cudaContext;
		session.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
		session.apiVersion = NVENCAPI_VERSION;
		if (m_nvencFunctions.nvEncOpenEncodeSessionEx(
				&session, &m_nvencEncoder) != NV_ENC_SUCCESS)
			return false;

		NV_ENC_PRESET_CONFIG preset{};
		preset.version = NV_ENC_PRESET_CONFIG_VER;
		preset.presetCfg.version = NV_ENC_CONFIG_VER;
		if (m_nvencFunctions.nvEncGetEncodePresetConfigEx(m_nvencEncoder,
				NV_ENC_CODEC_H264_GUID, NV_ENC_PRESET_P1_GUID,
				NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY, &preset) != NV_ENC_SUCCESS)
			return false;

		NV_ENC_CONFIG config = preset.presetCfg;
		config.profileGUID = NV_ENC_H264_PROFILE_BASELINE_GUID;
		config.gopLength = m_fps;
		config.frameIntervalP = 1;
		config.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
		config.rcParams.averageBitRate = m_bitrateKbps * 1000;
		config.rcParams.maxBitRate = m_bitrateKbps * 1000;
		config.rcParams.vbvBufferSize =
			config.rcParams.averageBitRate / m_fps;
		config.rcParams.vbvInitialDelay = config.rcParams.vbvBufferSize;
		config.rcParams.enableAQ = 1;
		config.encodeCodecConfig.h264Config.idrPeriod = m_fps;
		config.encodeCodecConfig.h264Config.disableSPSPPS = 0;
		config.encodeCodecConfig.h264Config.repeatSPSPPS = 1;
		config.encodeCodecConfig.h264Config.outputBufferingPeriodSEI = 0;
		config.encodeCodecConfig.h264Config.outputPictureTimingSEI = 0;

		NV_ENC_INITIALIZE_PARAMS init{};
		init.version = NV_ENC_INITIALIZE_PARAMS_VER;
		init.encodeGUID = NV_ENC_CODEC_H264_GUID;
		init.presetGUID = NV_ENC_PRESET_P1_GUID;
		init.encodeWidth = m_width;
		init.encodeHeight = m_height;
		init.darWidth = m_width;
		init.darHeight = m_height;
		init.frameRateNum = m_fps;
		init.frameRateDen = 1;
		init.enablePTD = 1;
		init.encodeConfig = &config;
		init.tuningInfo = NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;
		if (m_nvencFunctions.nvEncInitializeEncoder(
				m_nvencEncoder, &init) != NV_ENC_SUCCESS)
			return false;

		NV_ENC_CREATE_INPUT_BUFFER input{};
		input.version = NV_ENC_CREATE_INPUT_BUFFER_VER;
		input.width = m_width;
		input.height = m_height;
		input.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12;
		if (m_nvencFunctions.nvEncCreateInputBuffer(
				m_nvencEncoder, &input) != NV_ENC_SUCCESS)
			return false;
		m_nvencInput = input.inputBuffer;

		NV_ENC_CREATE_BITSTREAM_BUFFER output{};
		output.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
		if (m_nvencFunctions.nvEncCreateBitstreamBuffer(
				m_nvencEncoder, &output) != NV_ENC_SUCCESS)
			return false;
		m_nvencOutput = output.bitstreamBuffer;
		cemuLog_log(LogType::Force,
			"RTP: streaming gamepad screen at {}x{}, {} fps to {}:{} using NVENC",
			m_width, m_height, m_fps, GetConfig().rtp_address.GetValue(), m_port);
		return true;
	}

	bool InitializeVAAPI()
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
		m_vaapi = true;
		return true;
	}

	void ReleaseEncoder()
	{
		ReleaseVAAPI();
		ReleaseNVENC();
	}

	void ReleaseVAAPI()
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
		m_display = nullptr;
		m_drmFd = -1;
	}

	void ReleaseNVENC()
	{
		if (m_nvencEncoder)
		{
			if (m_nvencOutput)
				m_nvencFunctions.nvEncDestroyBitstreamBuffer(
					m_nvencEncoder, m_nvencOutput);
			if (m_nvencInput)
				m_nvencFunctions.nvEncDestroyInputBuffer(
					m_nvencEncoder, m_nvencInput);
			m_nvencFunctions.nvEncDestroyEncoder(m_nvencEncoder);
		}
		m_nvencOutput = m_nvencInput = m_nvencEncoder = nullptr;
		if (m_cudaContext && m_cuCtxDestroy)
			m_cuCtxDestroy(m_cudaContext);
		m_cudaContext = nullptr;
		if (m_nvencLibrary)
			dlclose(m_nvencLibrary);
		if (m_cudaLibrary)
			dlclose(m_cudaLibrary);
		m_nvencLibrary = m_cudaLibrary = nullptr;
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
		if (m_nvenc)
			return EncodeNVENC(frame);
		return EncodeVAAPI(frame);
	}

	bool EncodeNVENC(const CaptureFrame& frame)
	{
		NV_ENC_LOCK_INPUT_BUFFER lock{};
		lock.version = NV_ENC_LOCK_INPUT_BUFFER_VER;
		lock.inputBuffer = m_nvencInput;
		if (m_nvencFunctions.nvEncLockInputBuffer(
				m_nvencEncoder, &lock) != NV_ENC_SUCCESS)
			return false;

		auto* destination = static_cast<uint8*>(lock.bufferDataPtr);
		const auto* source = frame.rgbData.data();
		for (sint32 y = 0; y < m_height; ++y)
		{
			const sint32 sy = y * frame.height / m_height;
			for (sint32 x = 0; x < m_width; ++x)
			{
				const sint32 i = (sy * frame.width + x * frame.width / m_width) * 3;
				const sint32 r = source[i];
				const sint32 g = source[i + 1];
				const sint32 b = source[i + 2];
				destination[y * lock.pitch + x] = static_cast<uint8>(
					(66 * r + 129 * g + 25 * b + 128) / 256 + 16);
			}
		}
		uint8* uv = destination + lock.pitch * m_height;
		for (sint32 y = 0; y < m_height / 2; ++y)
		{
			const sint32 sy = y * 2 * frame.height / m_height;
			for (sint32 x = 0; x < m_width / 2; ++x)
			{
				const sint32 i =
					(sy * frame.width + x * 2 * frame.width / m_width) * 3;
				const sint32 r = source[i];
				const sint32 g = source[i + 1];
				const sint32 b = source[i + 2];
				uv[y * lock.pitch + x * 2] = static_cast<uint8>(
					(-38 * r - 74 * g + 112 * b + 128) / 256 + 128);
				uv[y * lock.pitch + x * 2 + 1] = static_cast<uint8>(
					(112 * r - 94 * g - 18 * b + 128) / 256 + 128);
			}
		}
		m_nvencFunctions.nvEncUnlockInputBuffer(m_nvencEncoder, m_nvencInput);

		NV_ENC_PIC_PARAMS picture{};
		picture.version = NV_ENC_PIC_PARAMS_VER;
		picture.inputWidth = m_width;
		picture.inputHeight = m_height;
		picture.inputPitch = lock.pitch;
		picture.inputBuffer = m_nvencInput;
		picture.outputBitstream = m_nvencOutput;
		picture.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12;
		picture.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
		picture.pictureType = NV_ENC_PIC_TYPE_UNKNOWN;
		picture.frameIdx = static_cast<uint32>(m_frameNumber);
		picture.inputTimeStamp = m_frameNumber * 1000000 / m_fps;
		picture.inputDuration = 1000000 / m_fps;
		if (m_frameNumber % m_fps == 0)
			picture.encodePicFlags |= NV_ENC_PIC_FLAG_FORCEIDR;
		if (m_nvencFunctions.nvEncEncodePicture(
				m_nvencEncoder, &picture) != NV_ENC_SUCCESS)
			return false;

		NV_ENC_LOCK_BITSTREAM output{};
		output.version = NV_ENC_LOCK_BITSTREAM_VER;
		output.outputBitstream = m_nvencOutput;
		if (m_nvencFunctions.nvEncLockBitstream(
				m_nvencEncoder, &output) != NV_ENC_SUCCESS)
			return false;
		SendH264Packet(static_cast<const uint8*>(output.bitstreamBufferPtr),
			output.bitstreamSizeInBytes,
			static_cast<sint64>(output.outputTimeStamp), true);
		m_nvencFunctions.nvEncUnlockBitstream(
			m_nvencEncoder, m_nvencOutput);
		++m_frameNumber;
		return true;
	}

	bool EncodeVAAPI(const CaptureFrame& frame)
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
	bool m_nvenc{};
	bool m_vaapi{};
	void* m_cudaLibrary{};
	void* m_nvencLibrary{};
	void* m_nvencEncoder{};
	void* m_nvencInput{};
	void* m_nvencOutput{};
	CUcontext m_cudaContext{};
	NV_ENCODE_API_FUNCTION_LIST m_nvencFunctions{};
	tcuInit* m_cuInit{};
	tcuDeviceGet* m_cuDeviceGet{};
	tcuCtxCreate_v2* m_cuCtxCreate{};
	tcuCtxDestroy_v2* m_cuCtxDestroy{};
};
#endif

#if BOOST_OS_LINUX
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


