// ObjC imports come first so UIViewController is defined before any C++ headers
#import <UIKit/UIKit.h>

// Forward-declare the Swift @objc factory class.
// At link time Xcode's Swift-ObjC bridge resolves this automatically;
// we avoid depending on the build-time generated CemuSwiftUiIosApp-Swift.h.
@interface OnScreenGamepadFactory : NSObject
+ (id)makeHostingController;
@end

#include "Common/precompiled.h"

#include "Cafe/HW/Latte/Renderer/Metal/MetalRenderer.h"
#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "gui/interface/WindowSystem.h"

static UIView *s_renderView = nil;
static UIViewController *s_gamepadController = nil;

static UIWindow *GetKeyWindow() {
  for (UIScene *scene in [UIApplication sharedApplication].connectedScenes) {
    if (scene.activationState == UISceneActivationStateForegroundActive &&
        [scene isKindOfClass:[UIWindowScene class]]) {
      UIWindowScene *windowScene = (UIWindowScene *)scene;
      for (UIWindow *window in windowScene.windows) {
        if (window.isKeyWindow)
          return window;
      }
      return windowScene.windows.firstObject;
    }
  }
  return nil;
}

void WindowSystem::NotifyGameLoaded() {
  UIWindow *window = GetKeyWindow();
  if (!window)
    return;

  UIView *renderView = [[UIView alloc] initWithFrame:window.bounds];
  renderView.autoresizingMask =
      UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
  renderView.backgroundColor = [UIColor blackColor];
  [window addSubview:renderView];
  s_renderView = renderView;

  const CGFloat scale = window.screen.scale;
  const int pointWidth = (int)window.bounds.size.width;
  const int pointHeight = (int)window.bounds.size.height;

  auto &windowInfo = WindowSystem::GetWindowInfo();
  windowInfo.window_main.backend =
      WindowSystem::WindowHandleInfo::Backend::Cocoa;
  windowInfo.window_main.display = nullptr;
  windowInfo.window_main.surface = (__bridge void *)renderView;
  windowInfo.canvas_main.backend =
      WindowSystem::WindowHandleInfo::Backend::Cocoa;
  windowInfo.canvas_main.display = nullptr;
  windowInfo.canvas_main.surface = (__bridge void *)renderView;
  windowInfo.width = pointWidth;
  windowInfo.height = pointHeight;
  windowInfo.phys_width = (int)(pointWidth * scale);
  windowInfo.phys_height = (int)(pointHeight * scale);
  windowInfo.dpi_scale = (double)scale;

  if (!g_renderer)
    g_renderer = std::make_unique<MetalRenderer>();

  const Vector2i size{pointWidth, pointHeight};
  MetalRenderer::GetInstance()->InitializeLayer(size, true);

  // Add on-screen gamepad overlay using the Swift factory
  UIViewController *gamepadVC = [OnScreenGamepadFactory makeHostingController];
  gamepadVC.view.frame = window.bounds;
  gamepadVC.view.autoresizingMask =
      UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
  [window addSubview:gamepadVC.view];
  s_gamepadController = gamepadVC;
}

void WindowSystem::NotifyGameExited() {
  if (s_gamepadController) {
    [s_gamepadController.view removeFromSuperview];
    s_gamepadController = nil;
  }

  if (s_renderView) {
    [s_renderView removeFromSuperview];
    s_renderView = nil;
  }

  if (g_renderer && g_renderer->GetType() == RendererAPI::Metal)
    MetalRenderer::GetInstance()->ShutdownLayer(true);

  g_renderer.reset();
}
