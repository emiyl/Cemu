#pragma once

namespace MTL
{
	class Texture;
}

class MetalRenderer;

namespace GamepadRTPStream
{
	bool ShouldCaptureFrame();
	void SubmitFrame(std::vector<uint8>&& rgbData, sint32 width, sint32 height);
#if BOOST_OS_MACOS
	void SubmitMetalFrame(::MetalRenderer* renderer, MTL::Texture* texture);
#endif
}
