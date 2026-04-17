#pragma once

#include <memory>
#include <string>

#ifdef __OBJC__
@class NSView;
#else
class NSView;
#endif

class RenderCanvas
{
public:
	virtual ~RenderCanvas() = default;

	virtual bool Initialize(NSView* windowSurfaceView, NSView* canvasView,
							int width, int height, std::string& errorOut) = 0;
	virtual void Resize(NSView* canvasView, int width, int height) = 0;
};

std::unique_ptr<RenderCanvas> CreateVulkanCanvas();

#if ENABLE_METAL
std::unique_ptr<RenderCanvas> CreateMetalCanvas();
#endif