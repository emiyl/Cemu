#include "Common/precompiled.h"

#include "gui/swiftui/canvas/RenderCanvas.h"

#include "Cafe/CafeSystem.h"
#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include "interface/WindowSystem.h"

#import <Cocoa/Cocoa.h>

namespace {
class VulkanCanvas final : public RenderCanvas {
public:
  bool Initialize(NSView *windowSurfaceView, NSView *canvasView, int width,
                  int height, std::string &errorOut) override {
    if (!windowSurfaceView || !canvasView) {
      errorOut = "SwiftUI Vulkan canvas views are not available.";
      return false;
    }

    auto &windowInfo = WindowSystem::GetWindowInfo();
    windowInfo.window_main.backend =
        WindowSystem::WindowHandleInfo::Backend::Cocoa;
    windowInfo.window_main.display = nullptr;
    windowInfo.window_main.surface = (__bridge void *)windowSurfaceView;

    windowInfo.canvas_main.backend =
        WindowSystem::WindowHandleInfo::Backend::Cocoa;
    windowInfo.canvas_main.display = nullptr;
    windowInfo.canvas_main.surface = (__bridge void *)canvasView;

    const Vector2i size{width, height};
    if (!g_renderer)
      g_renderer = std::make_unique<VulkanRenderer>();

    VulkanRenderer::GetInstance()->InitializeSurface(size, true);
    return true;
  }

  void Resize(NSView *canvasView, int width, int height) override {
    (void)canvasView;
    (void)width;
    (void)height;
  }
};

} // namespace

std::unique_ptr<RenderCanvas> CreateVulkanCanvas() {
  return std::make_unique<VulkanCanvas>();
}