#pragma once

namespace MTL
{
	class Texture;
}

class MetalRenderer;

namespace GamepadRTPStream
{
	bool ShouldCaptureFrame();
	void Shutdown();
	void SubmitFrame(std::vector<uint8>&& rgbData, sint32 width, sint32 height);
	void SubmitAudio(const sint16* samples, sint32 sampleCount, sint32 channels);
#if BOOST_OS_MACOS
	void SubmitMetalFrame(::MetalRenderer* renderer, MTL::Texture* texture);
#endif
}
