#pragma once
#include "config/CemuConfig.h"
#include "util/Socket/Socket.h"
#include <cstdint>
#include <vector>

namespace MTL { class Texture; }
class MetalRenderer;

namespace
{
constexpr sint32 kRtpPayloadSize = 1200;
constexpr uint8 kRtpPayloadType = 96;

inline void WriteU16BigEndian(uint8* destination, uint16 value)
{ destination[0] = static_cast<uint8>(value >> 8); destination[1] = static_cast<uint8>(value); }

inline void WriteU32BigEndian(uint8* destination, uint32 value)
{ destination[0] = static_cast<uint8>(value >> 24); destination[1] = static_cast<uint8>(value >> 16); destination[2] = static_cast<uint8>(value >> 8); destination[3] = static_cast<uint8>(value); }

struct CaptureFrame
{
	std::vector<uint8> rgbData;
	sint32 width;
	sint32 height;
};
}

namespace GamepadRTPStream::Platform
{
bool LinuxNVIDIAAvailable();
std::string GetH264SpropParameterSets();
void Shutdown();
void SubmitFrame(std::vector<uint8>&& rgbData, sint32 width, sint32 height);
#if BOOST_OS_MACOS
void SubmitMetalFrame(class MetalRenderer* renderer, MTL::Texture* texture);
#endif
}
