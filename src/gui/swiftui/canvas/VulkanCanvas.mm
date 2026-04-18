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
    ~VulkanCanvas() override {
        if (!m_main_window) {
            auto *renderer = VulkanRenderer::GetInstance();
            if (renderer)
                renderer->StopUsingPadAndWait();
        }
    }
    
    bool Initialize(NSView *windowSurfaceView, NSView *canvasView, int width,
                    int height, bool mainWindow, std::string &errorOut) override {
        if (!windowSurfaceView || !canvasView) {
            errorOut = "SwiftUI Vulkan canvas views are not available.";
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
            g_renderer = std::make_unique<VulkanRenderer>();
        
        VulkanRenderer::GetInstance()->InitializeSurface(size, mainWindow);
        return true;
    }
    
    void Resize(NSView *canvasView, int width, int height,
                bool mainWindow) override {
        (void)canvasView;
        (void)width;
        (void)height;
        (void)mainWindow;
    }
    
private:
    bool m_main_window{true};
};

} // namespace

std::unique_ptr<RenderCanvas> CreateVulkanCanvas() {
    return std::make_unique<VulkanCanvas>();
}
