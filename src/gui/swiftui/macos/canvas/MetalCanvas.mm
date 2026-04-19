#include "Common/precompiled.h"

#include "gui/swiftui/macos/canvas/RenderCanvas.h"

#include "Cafe/HW/Latte/Renderer/Metal/MetalRenderer.h"
#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "interface/WindowSystem.h"

#import <Cocoa/Cocoa.h>

namespace {
class MetalCanvas final : public RenderCanvas {
public:
  ~MetalCanvas() override {
    auto *renderer = MetalRenderer::GetInstance();
    if (renderer)
      renderer->ShutdownLayer(m_main_window);
  }

  bool Initialize(NSView *windowSurfaceView, NSView *canvasView, int width,
                  int height, bool mainWindow, std::string &errorOut) override {
    if (!windowSurfaceView || !canvasView) {
      errorOut = "SwiftUI Metal canvas views are not available.";
      return false;
    }

    m_main_window = mainWindow;

    auto &windowInfo = WindowSystem::GetWindowInfo();
    auto &windowHandle =
        mainWindow ? windowInfo.window_main : windowInfo.window_pad;
    auto &canvasHandle =
        mainWindow ? windowInfo.canvas_main : windowInfo.canvas_pad;

    windowHandle.backend = WindowSystem::WindowHandleInfo::Backend::Cocoa;
    windowHandle.display = nullptr;
    windowHandle.surface = (__bridge void *)windowSurfaceView;

    canvasHandle.backend = WindowSystem::WindowHandleInfo::Backend::Cocoa;
    canvasHandle.display = nullptr;
    canvasHandle.surface = (__bridge void *)canvasView;

    const Vector2i size{width, height};
    if (!g_renderer)
      g_renderer = std::make_unique<MetalRenderer>();

    MetalRenderer::GetInstance()->InitializeLayer(size, mainWindow);
    return true;
  }

  void Resize(NSView *canvasView, int width, int height,
              bool mainWindow) override {
    (void)canvasView;
    const Vector2i size{width, height};
    if (g_renderer && g_renderer->GetType() == RendererAPI::Metal)
      MetalRenderer::GetInstance()->ResizeLayer(size, mainWindow);
  }

private:
  bool m_main_window{true};
};

} // namespace

std::unique_ptr<RenderCanvas> CreateMetalCanvas() {
  return std::make_unique<MetalCanvas>();
}
