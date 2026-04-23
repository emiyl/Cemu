#include "Common/precompiled.h"
#include "gui/interface/WindowSystem.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalRenderer.h"
#include "Cafe/HW/Latte/Renderer/Renderer.h"

#import <UIKit/UIKit.h>

static UIView* s_renderView = nil;

static UIWindow* GetKeyWindow()
{
    for (UIScene* scene in [UIApplication sharedApplication].connectedScenes)
    {
        if (scene.activationState == UISceneActivationStateForegroundActive &&
            [scene isKindOfClass:[UIWindowScene class]])
        {
            UIWindowScene* windowScene = (UIWindowScene*)scene;
            for (UIWindow* window in windowScene.windows)
            {
                if (window.isKeyWindow)
                    return window;
            }
            return windowScene.windows.firstObject;
        }
    }
    return nil;
}

void WindowSystem::NotifyGameLoaded()
{
    UIWindow* window = GetKeyWindow();
    if (!window)
        return;

    UIView* renderView = [[UIView alloc] initWithFrame:window.bounds];
    renderView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    renderView.backgroundColor = [UIColor blackColor];
    [window addSubview:renderView];
    s_renderView = renderView;

    const CGFloat scale = window.screen.scale;
    const int pointWidth  = (int)window.bounds.size.width;
    const int pointHeight = (int)window.bounds.size.height;

    auto& windowInfo = WindowSystem::GetWindowInfo();
    windowInfo.window_main.backend = WindowSystem::WindowHandleInfo::Backend::Cocoa;
    windowInfo.window_main.display = nullptr;
    windowInfo.window_main.surface = (__bridge void*)renderView;
    windowInfo.canvas_main.backend = WindowSystem::WindowHandleInfo::Backend::Cocoa;
    windowInfo.canvas_main.display = nullptr;
    windowInfo.canvas_main.surface = (__bridge void*)renderView;
    windowInfo.width        = pointWidth;
    windowInfo.height       = pointHeight;
    windowInfo.phys_width   = (int)(pointWidth  * scale);
    windowInfo.phys_height  = (int)(pointHeight * scale);
    windowInfo.dpi_scale    = (double)scale;

    if (!g_renderer)
        g_renderer = std::make_unique<MetalRenderer>();

    const Vector2i size{pointWidth, pointHeight};
    MetalRenderer::GetInstance()->InitializeLayer(size, true);
}

void WindowSystem::NotifyGameExited()
{
    if (s_renderView)
    {
        [s_renderView removeFromSuperview];
        s_renderView = nil;
    }

    if (g_renderer && g_renderer->GetType() == RendererAPI::Metal)
        MetalRenderer::GetInstance()->ShutdownLayer(true);

    g_renderer.reset();
}
