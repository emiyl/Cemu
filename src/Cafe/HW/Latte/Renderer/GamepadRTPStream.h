#pragma once

namespace GamepadRTPStream
{
	bool ShouldCaptureFrame();
	void SubmitFrame(std::vector<uint8>&& rgbData, sint32 width, sint32 height);
}
