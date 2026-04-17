#include "Common/precompiled.h"

#include "gui/swiftui/CemuApp.h"
#include "gui/swiftui/MainWindow.h"
#include "gui/swiftui/WindowSystemSwiftUI/WindowSystemSwiftUIInternal.h"

#include "Cafe/TitleList/TitleList.h"

using namespace WindowSystemSwiftUIInternal;

extern "C" bool CemuSwiftUILaunchTitleById(uint64_t titleId) {
  std::string errorMessage;
  if (!swiftui::MainWindow::RequestLaunchGameByTitleId(titleId, errorMessage)) {
    WindowSystem::ShowErrorDialog(errorMessage, "Failed to launch game");
    return false;
  }

  UpdateTitleFromGame();
  return true;
}

void WindowSystem::ShowErrorDialog(
    std::string_view message, std::string_view title,
    std::optional<WindowSystem::ErrorCategory> /*errorCategory*/) {
  @autoreleasepool {
    NSAlert *alert = [[NSAlert alloc] init];
    NSString *alertTitle =
        title.empty()
            ? @"Error"
            : [NSString stringWithUTF8String:std::string(title).c_str()];
    NSString *alertMessage =
        [NSString stringWithUTF8String:std::string(message).c_str()];
    [alert setAlertStyle:NSAlertStyleCritical];
    [alert setMessageText:alertTitle];
    [alert setInformativeText:alertMessage ?: @""];
    [alert addButtonWithTitle:@"OK"];
    [alert runModal];
  }
}

void WindowSystem::Create() {
  g_cemu_app = new CemuApp();
  g_cemu_app->OnInit();

  @autoreleasepool {
    NSApplication *app = [NSApplication sharedApplication];
    [app setActivationPolicy:NSApplicationActivationPolicyRegular];

    g_app_delegate = [[CemuAppDelegate alloc] init];
    [app setDelegate:g_app_delegate];
    [g_app_delegate setupMenuBar];

    const NSRect frame = NSMakeRect(120.0, 120.0, 960.0, 540.0);
    const NSWindowStyleMask style =
        NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
        NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable |
        NSWindowStyleMaskUnifiedTitleAndToolbar;

    g_main_window = [[NSWindow alloc] initWithContentRect:frame
                                                styleMask:style
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
    [g_main_window setTitle:@"Cemu"];
    [g_main_window setTitlebarAppearsTransparent:NO];
    [g_main_window setDelegate:g_app_delegate];
    [g_main_window center];
    [g_main_window setContentSize:NSMakeSize(960, 490)];
    [g_main_window setContentMinSize:NSMakeSize(960, 490)];

    NSViewController *rootViewController = nil;
    if (void *swiftControllerPtr = CemuCreateSwiftUIRootViewController()) {
      id swiftControllerObj = (__bridge id)swiftControllerPtr;
      if ([swiftControllerObj isKindOfClass:[NSViewController class]])
        rootViewController = (NSViewController *)swiftControllerObj;
    }
    if (!rootViewController) {
      rootViewController = [[NSViewController alloc] init];
      NSView *contentView = [[NSView alloc] initWithFrame:frame];
      [contentView setWantsLayer:YES];
      contentView.layer.backgroundColor = NSColor.windowBackgroundColor.CGColor;

      NSTextField *fallbackLabel = [NSTextField
          labelWithString:
              @"SwiftUI root view not found.\nUsing AppKit fallback view."];
      [fallbackLabel
          setFont:[NSFont systemFontOfSize:16 weight:NSFontWeightMedium]];
      [fallbackLabel setTextColor:NSColor.secondaryLabelColor];
      [fallbackLabel setAlignment:NSTextAlignmentCenter];
      [fallbackLabel setFrame:NSMakeRect(40, frame.size.height / 2 - 20,
                                         frame.size.width - 80, 60)];
      [fallbackLabel setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin |
                                         NSViewMaxYMargin];
      [contentView addSubview:fallbackLabel];

      rootViewController.view = contentView;
    }

    [g_main_window setContentViewController:rootViewController];

    NSView *windowContentView = [g_main_window contentView];
    if (!windowContentView) {
      WindowSystem::ShowErrorDialog(
          "Main window content view is not available.", "Window setup failed");
      [NSApp terminate:nil];
      return;
    }

    g_root_view_controller = rootViewController;
    g_swiftui_overlay_view = rootViewController.view;
    g_swiftui_overlay_view.frame = windowContentView.bounds;
    g_swiftui_overlay_view.autoresizingMask =
        NSViewWidthSizable | NSViewHeightSizable;

    g_renderer_host_view = [[NSView alloc] initWithFrame:windowContentView.bounds];
    g_renderer_host_view.autoresizingMask =
        NSViewWidthSizable | NSViewHeightSizable;
    g_renderer_host_view.wantsLayer = YES;
    g_renderer_host_view.layer.backgroundColor = NSColor.clearColor.CGColor;
    [windowContentView addSubview:g_renderer_host_view
                       positioned:NSWindowBelow
                       relativeTo:g_swiftui_overlay_view];

    [g_main_window makeKeyAndOrderFront:nil];
    [app activateIgnoringOtherApps:YES];

    [windowContentView layoutSubtreeIfNeeded];
    UpdateMainWindowMetricsFromRendererHostView();

    std::string rendererError;
    if (!InitializeRendererForMainView(
            g_renderer_host_view, (int)g_window_info.width.load(),
            (int)g_window_info.height.load(), rendererError)) {
      WindowSystem::ShowErrorDialog(rendererError,
                                    "Renderer initialization failed");
      [NSApp terminate:nil];
      return;
    }

    g_window_info.app_active = true;
    g_window_info.pad_open = false;
    g_window_info.pad_width = 0;
    g_window_info.pad_height = 0;
    g_window_info.phys_pad_width = 0;
    g_window_info.phys_pad_height = 0;
    g_window_info.pad_dpi_scale = 1.0;
    g_window_info.pad_maximized = false;
    g_window_info.restored_pad_x = -1;
    g_window_info.restored_pad_y = -1;
    g_window_info.restored_pad_width = -1;
    g_window_info.restored_pad_height = -1;
    g_window_info.is_fullscreen = false;
    g_window_info.debugger_focused = false;
    UpdatePadViewMenuState();

    [NSApp run];
  }
}

WindowSystem::WindowInfo &WindowSystem::GetWindowInfo() {
  return g_window_info;
}

void WindowSystem::UpdateWindowTitles(bool isIdle, bool isLoading, double fps) {
  if (!g_main_window)
    return;

  NSString *title = nil;
  if (isIdle)
    title = @"Cemu";
  else if (isLoading)
    title = @"Cemu - Loading...";
  else
    title = [NSString stringWithFormat:@"Cemu - FPS: %.2f", fps];

  SetMainWindowTitle(title);
}

void WindowSystem::GetWindowSize(int &w, int &h) {
  w = g_window_info.width;
  h = g_window_info.height;
}

void WindowSystem::GetPadWindowSize(int &w, int &h) {
  if (g_window_info.pad_open) {
    w = g_window_info.pad_width;
    h = g_window_info.pad_height;
  } else {
    w = 0;
    h = 0;
  }
}

void WindowSystem::GetWindowPhysSize(int &w, int &h) {
  w = g_window_info.phys_width;
  h = g_window_info.phys_height;
}

void WindowSystem::GetPadWindowPhysSize(int &w, int &h) {
  if (g_window_info.pad_open) {
    w = g_window_info.phys_pad_width;
    h = g_window_info.phys_pad_height;
  } else {
    w = 0;
    h = 0;
  }
}

double WindowSystem::GetWindowDPIScale() { return g_window_info.dpi_scale; }

double WindowSystem::GetPadDPIScale() {
  return g_window_info.pad_open ? g_window_info.pad_dpi_scale.load() : 1.0;
}

bool WindowSystem::IsPadWindowOpen() { return g_window_info.pad_open; }

bool WindowSystem::IsKeyDown(uint32 key) {
  return g_window_info.get_keystate(key);
}

bool WindowSystem::IsKeyDown(PlatformKeyCodes key) {
  switch (key) {
  case PlatformKeyCodes::LCONTROL:
    return IsKeyDown(0x3B);
  case PlatformKeyCodes::RCONTROL:
    return IsKeyDown(0x3E);
  case PlatformKeyCodes::TAB:
    return IsKeyDown(0x30);
  case PlatformKeyCodes::ESCAPE:
    return IsKeyDown(0x35);
  default:
    return false;
  }
}

std::string WindowSystem::GetKeyCodeName(uint32 key) {
  return fmt::format("key_{}", key);
}

bool WindowSystem::InputConfigWindowHasFocus() { return false; }

void WindowSystem::NotifyGameLoaded() {
  dispatch_async(dispatch_get_main_queue(), ^{
    if (!g_main_window || !g_renderer_host_view)
      return;

    if (!g_swiftui_toolbar)
      g_swiftui_toolbar = g_main_window.toolbar;
    [g_main_window setToolbar:nil];

    SetMainWindowUnifiedTitleToolbar(false);

    if (!g_game_view_controller) {
      NSViewController *controller = [[NSViewController alloc] init];
      NSView *gameView =
          [[NSView alloc] initWithFrame:g_main_window.contentView.bounds];
      gameView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
      gameView.wantsLayer = YES;
      gameView.layer.backgroundColor = NSColor.blackColor.CGColor;
      controller.view = gameView;
      g_game_view_controller = controller;
    }

    [g_renderer_host_view removeFromSuperview];
    [g_main_window setContentViewController:g_game_view_controller];

    NSView *gameContentView = [g_main_window contentView];
    if (!gameContentView)
      return;

    g_renderer_host_view.frame = gameContentView.bounds;
    [gameContentView addSubview:g_renderer_host_view
                     positioned:NSWindowAbove
                     relativeTo:nil];

    UpdateMainWindowMetricsFromRendererHostView();
    ResizeMainRendererIfNeeded();
    ApplyPadWindowRequestedState();
  });
}

void WindowSystem::NotifyGameExited() {
  dispatch_async(dispatch_get_main_queue(), ^{
    if (!g_main_window || !g_root_view_controller || !g_renderer_host_view)
      return;

    DestroyPadWindowIfExists();

    if (g_swiftui_toolbar && g_main_window.toolbar != g_swiftui_toolbar)
      [g_main_window setToolbar:g_swiftui_toolbar];

    SetMainWindowUnifiedTitleToolbar(true);

    [g_renderer_host_view removeFromSuperview];
    [g_main_window setContentViewController:g_root_view_controller];

    NSView *windowContentView = [g_main_window contentView];
    if (!windowContentView)
      return;

    g_swiftui_overlay_view = g_root_view_controller.view;
    g_swiftui_overlay_view.frame = windowContentView.bounds;
    g_swiftui_overlay_view.autoresizingMask =
        NSViewWidthSizable | NSViewHeightSizable;

    g_renderer_host_view.frame = windowContentView.bounds;
    [windowContentView addSubview:g_renderer_host_view
                       positioned:NSWindowBelow
                       relativeTo:g_swiftui_overlay_view];

    UpdateMainWindowMetricsFromRendererHostView();
    ResizeMainRendererIfNeeded();
    UpdatePadViewMenuState();
  });
}

void WindowSystem::RefreshGameList() { CafeTitleList::Refresh(); }

void WindowSystem::CaptureInput(const ControllerState & /*currentState*/,
                                const ControllerState & /*lastState*/) {}

bool WindowSystem::IsFullScreen() { return g_window_info.is_fullscreen; }
