#include "Common/precompiled.h"

#include "gui/swiftui/CemuApp.h"
#include "gui/swiftui/MainWindow.h"
#include "interface/WindowSystem.h"

#include "Cafe/CafeSystem.h"
#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#if ENABLE_METAL
#include "Cafe/HW/Latte/Renderer/Metal/MetalRenderer.h"
#endif
#include "Cafe/TitleList/TitleList.h"
#include "config/ActiveSettings.h"

#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

extern "C" void *CemuCreateSwiftUIRootViewController(void);

@interface CemuAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
- (void)setupMenuBar;
- (void)quitApp:(id)sender;
- (void)openGame:(id)sender;
- (void)openPreferences:(id)sender;
- (void)toggleFullscreen:(id)sender;
- (void)showHelp:(id)sender;
- (void)showAbout:(id)sender;
@end

namespace {
extern WindowSystem::WindowInfo g_window_info;
extern NSWindow *g_main_window;
extern CemuAppDelegate *g_app_delegate;
extern NSView *g_renderer_host_view;

void UpdateMainWindowMetricsFromRendererHostView() {
  if (!g_renderer_host_view)
    return;

  const NSRect points = g_renderer_host_view.bounds;
  const NSRect pixels = [g_renderer_host_view convertRectToBacking:points];

  const int width = std::max(1, (int)std::lround(points.size.width));
  const int height = std::max(1, (int)std::lround(points.size.height));
  const int physWidth = std::max(1, (int)std::lround(pixels.size.width));
  const int physHeight = std::max(1, (int)std::lround(pixels.size.height));

  g_window_info.width = width;
  g_window_info.height = height;
  g_window_info.phys_width = physWidth;
  g_window_info.phys_height = physHeight;
  g_window_info.dpi_scale = (double)physWidth / (double)width;
}

void ResizeMainRendererIfNeeded() {
  if (!g_renderer)
    return;

#if ENABLE_METAL
  if (ActiveSettings::GetGraphicsAPI() != kVulkan &&
      g_renderer->GetType() == RendererAPI::Metal) {
    const Vector2i size{(int)g_window_info.width.load(),
                        (int)g_window_info.height.load()};
    MetalRenderer::GetInstance()->ResizeLayer(size, true);
  }
#endif
}

void SetMainWindowTitle(NSString *title) {
  if (!g_main_window || !title)
    return;

  if ([NSThread isMainThread]) {
    [g_main_window setTitle:title];
    return;
  }

  dispatch_async(dispatch_get_main_queue(), ^{
    if (g_main_window)
      [g_main_window setTitle:title];
  });
}

bool InitializeRendererForMainView(NSView *mainView, int width, int height,
                                   std::string &errorOut) {
  if (!mainView) {
    errorOut = "Main window view is not available.";
    return false;
  }

  auto &windowInfo = WindowSystem::GetWindowInfo();
  windowInfo.window_main.backend =
      WindowSystem::WindowHandleInfo::Backend::Cocoa;
  windowInfo.window_main.display = nullptr;
  windowInfo.window_main.surface = (__bridge void *)mainView;
  // Reuse the main view as render canvas handle in SwiftUI mode.
  windowInfo.canvas_main = windowInfo.window_main;

  if (g_renderer)
    return true;

  const auto api = ActiveSettings::GetGraphicsAPI();
  const Vector2i size{width, height};

  try {
    if (api == kVulkan) {
      g_renderer = std::make_unique<VulkanRenderer>();
      VulkanRenderer::GetInstance()->InitializeSurface(size, true);
      return true;
    }

#if ENABLE_METAL
    g_renderer = std::make_unique<MetalRenderer>();
    MetalRenderer::GetInstance()->InitializeLayer(size, true);
    return true;
#else
    errorOut = "Only Vulkan renderer is supported in SwiftUI build.";
    return false;
#endif
  } catch (const std::exception &ex) {
    errorOut = fmt::format("Failed to initialize renderer: {}", ex.what());
    return false;
  }
}
} // namespace

CemuApp *app;

@implementation CemuAppDelegate

- (void)windowDidResize:(NSNotification *)notification {
  (void)notification;
  UpdateMainWindowMetricsFromRendererHostView();
  ResizeMainRendererIfNeeded();
}

- (void)windowDidChangeBackingProperties:(NSNotification *)notification {
  (void)notification;
  UpdateMainWindowMetricsFromRendererHostView();
  ResizeMainRendererIfNeeded();
}

- (void)setupMenuBar {
  NSMenu *mainMenu = [[NSMenu alloc] init];

  // File menu
  NSMenu *fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
  NSMenuItem *fileMenuItem =
      [mainMenu addItemWithTitle:@"File" action:nil keyEquivalent:@""];
  [mainMenu setSubmenu:fileMenu forItem:fileMenuItem];

  NSMenuItem *openGameItem = [fileMenu addItemWithTitle:@"Open Game..."
                                                 action:@selector(openGame:)
                                          keyEquivalent:@"o"];
  [openGameItem setTarget:self];
  [fileMenu addItem:[NSMenuItem separatorItem]];
  NSMenuItem *quitItem = [fileMenu addItemWithTitle:@"Quit Cemu"
                                             action:@selector(quitApp:)
                                      keyEquivalent:@"q"];
  [quitItem setTarget:self];

  // Edit menu
  NSMenu *editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
  NSMenuItem *editMenuItem =
      [mainMenu addItemWithTitle:@"Edit" action:nil keyEquivalent:@""];
  [mainMenu setSubmenu:editMenu forItem:editMenuItem];

  NSMenuItem *preferencesItem =
      [editMenu addItemWithTitle:@"Preferences..."
                          action:@selector(openPreferences:)
                   keyEquivalent:@","];
  [preferencesItem setTarget:self];

  // View menu
  NSMenu *viewMenu = [[NSMenu alloc] initWithTitle:@"View"];
  NSMenuItem *viewMenuItem =
      [mainMenu addItemWithTitle:@"View" action:nil keyEquivalent:@""];
  [mainMenu setSubmenu:viewMenu forItem:viewMenuItem];

  NSMenuItem *fullscreenItem =
      [viewMenu addItemWithTitle:@"Toggle Fullscreen"
                          action:@selector(toggleFullscreen:)
                   keyEquivalent:@"f"];
  [fullscreenItem setTarget:self];

  // Help menu
  NSMenu *helpMenu = [[NSMenu alloc] initWithTitle:@"Help"];
  NSMenuItem *helpMenuItem =
      [mainMenu addItemWithTitle:@"Help" action:nil keyEquivalent:@""];
  [mainMenu setSubmenu:helpMenu forItem:helpMenuItem];

  NSMenuItem *helpItem = [helpMenu addItemWithTitle:@"Cemu Help"
                                             action:@selector(showHelp:)
                                      keyEquivalent:@""];
  [helpItem setTarget:self];
  NSMenuItem *aboutItem = [helpMenu addItemWithTitle:@"About Cemu"
                                              action:@selector(showAbout:)
                                       keyEquivalent:@""];
  [aboutItem setTarget:self];

  [NSApp setMainMenu:mainMenu];
}

- (void)quitApp:(id)sender {
  app->OnExit();
  printf("Terminating application...\n");
  [NSApp terminate:sender];
}

- (void)openGame:(id)sender {
  NSOpenPanel *panel = [NSOpenPanel openPanel];
  [panel setCanChooseFiles:YES];
  [panel setCanChooseDirectories:NO];
  [panel setAllowsMultipleSelection:NO];
  [panel setAllowedContentTypes:@[
    [UTType typeWithFilenameExtension:@"wud"],
    [UTType typeWithFilenameExtension:@"wux"],
    [UTType typeWithFilenameExtension:@"wua"],
    [UTType typeWithFilenameExtension:@"wuhb"],
    [UTType typeWithFilenameExtension:@"iso"],
    [UTType typeWithFilenameExtension:@"rpx"],
    [UTType typeWithFilenameExtension:@"elf"],
    [UTType typeWithFilenameExtension:@"tmd"]
  ]];

  if ([panel runModal] != NSModalResponseOK || panel.URL == nil)
    return;

  NSString *pathString = panel.URL.path;
  if (pathString.length == 0)
    return;

  fs::path launchPath = _utf8ToPath(std::string(pathString.UTF8String));
  std::string errorMessage;
  if (!swiftui::MainWindow::RequestLaunchGame(
          launchPath, swiftui::MainWindow::LaunchInitiatedBy::kMenu,
          errorMessage)) {
    WindowSystem::ShowErrorDialog(errorMessage, "Failed to launch game");
    return;
  }

  const std::string titleName = CafeSystem::GetForegroundTitleName();
  if (!titleName.empty() && g_main_window) {
    SetMainWindowTitle([NSString stringWithUTF8String:titleName.c_str()]);
  }
}

- (void)openPreferences:(id)sender {
  fs::path configPath = ActiveSettings::GetConfigPath();
  std::string configPathUtf8 = _pathToUtf8(configPath);
  NSString *configNSString =
      [NSString stringWithUTF8String:configPathUtf8.c_str()];
  if (configNSString.length == 0)
    return;

  NSURL *configURL = [NSURL fileURLWithPath:configNSString isDirectory:YES];
  [[NSWorkspace sharedWorkspace] openURL:configURL];
}

- (void)toggleFullscreen:(id)sender {
  if (!g_main_window)
    return;

  [g_main_window toggleFullScreen:nil];
  g_window_info.is_fullscreen = !g_window_info.is_fullscreen.load();
}

- (void)showHelp:(id)sender {
  NSURL *helpURL = [NSURL URLWithString:@"https://wiki.cemu.info"];
  if (helpURL)
    [[NSWorkspace sharedWorkspace] openURL:helpURL];
}

- (void)showAbout:(id)sender {
  NSAlert *about = [[NSAlert alloc] init];
  [about setAlertStyle:NSAlertStyleInformational];
  [about setMessageText:@"About Cemu"];
  [about setInformativeText:@"Cemu - Wii U Emulator\nSwiftUI macOS GUI"];
  [about addButtonWithTitle:@"OK"];
  [about runModal];
}

@end

namespace {
WindowSystem::WindowInfo g_window_info{};
NSWindow *g_main_window = nil;
CemuAppDelegate *g_app_delegate = nil;
NSView *g_renderer_host_view = nil;
NSView *g_swiftui_overlay_view = nil;
NSViewController *g_root_view_controller = nil;
} // namespace

extern "C" bool CemuSwiftUILaunchTitleById(uint64_t titleId) {
  std::string errorMessage;
  if (!swiftui::MainWindow::RequestLaunchGameByTitleId(titleId, errorMessage)) {
    WindowSystem::ShowErrorDialog(errorMessage, "Failed to launch game");
    return false;
  }

  const std::string titleName = CafeSystem::GetForegroundTitleName();
  if (!titleName.empty() && g_main_window) {
    SetMainWindowTitle([NSString stringWithUTF8String:titleName.c_str()]);
  }
  return true;
}

void WindowSystem::ShowErrorDialog(
    std::string_view message, std::string_view title,
    std::optional<WindowSystem::ErrorCategory> /*errorCategory*/) {
  @autoreleasepool {
    NSAlert *alert = [[NSAlert alloc] init];
    std::string titleCopy(title);
    NSString *alertTitle =
        titleCopy.empty() ? @"Error"
                          : [NSString stringWithUTF8String:titleCopy.c_str()];
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
  app = new CemuApp();
  app->OnInit();

  @autoreleasepool {
    NSApplication *app = [NSApplication sharedApplication];
    [app setActivationPolicy:NSApplicationActivationPolicyRegular];

    // Setup application delegate with menu bar
    g_app_delegate = [[CemuAppDelegate alloc] init];
    [app setDelegate:g_app_delegate];
    [g_app_delegate setupMenuBar];

    const NSRect frame = NSMakeRect(120.0, 120.0, 1280.0, 720.0);
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

    NSView *windowContentView = [g_main_window contentView];
    if (!windowContentView) {
      WindowSystem::ShowErrorDialog(
          "Main window content view is not available.", "Window setup failed");
      [NSApp terminate:nil];
      return;
    }

    g_renderer_host_view =
        [[NSView alloc] initWithFrame:windowContentView.bounds];
    g_renderer_host_view.autoresizingMask =
        NSViewWidthSizable | NSViewHeightSizable;
    g_renderer_host_view.wantsLayer = YES;
    g_renderer_host_view.layer.backgroundColor = NSColor.clearColor.CGColor;
    [windowContentView addSubview:g_renderer_host_view
                       positioned:NSWindowBelow
                       relativeTo:nil];

    // Instantiate SwiftUI-backed root controller via explicit Swift C symbol.
    NSViewController *rootViewController = nil;
    if (void *swiftControllerPtr = CemuCreateSwiftUIRootViewController()) {
      id swiftControllerObj = (__bridge id)swiftControllerPtr;
      if ([swiftControllerObj isKindOfClass:[NSViewController class]]) {
        rootViewController = (NSViewController *)swiftControllerObj;
      }
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
    g_root_view_controller = rootViewController;
    g_swiftui_overlay_view = rootViewController.view;
    g_swiftui_overlay_view.frame = windowContentView.bounds;
    g_swiftui_overlay_view.autoresizingMask =
        NSViewWidthSizable | NSViewHeightSizable;
    [windowContentView addSubview:g_swiftui_overlay_view
                       positioned:NSWindowAbove
                       relativeTo:g_renderer_host_view];

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

    [g_main_window makeKeyAndOrderFront:nil];
    [app activateIgnoringOtherApps:YES];

    g_window_info.app_active = true;
    UpdateMainWindowMetricsFromRendererHostView();
    g_window_info.pad_open = false;
    g_window_info.pad_width = 0;
    g_window_info.pad_height = 0;
    g_window_info.phys_pad_width = 0;
    g_window_info.phys_pad_height = 0;
    g_window_info.pad_dpi_scale = 1.0;
    g_window_info.is_fullscreen = false;
    g_window_info.debugger_focused = false;
    // window_main/canvas_main surfaces are initialized in
    // InitializeRendererForMainView()

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
  w = 0;
  h = 0;
}

void WindowSystem::GetWindowPhysSize(int &w, int &h) {
  w = g_window_info.phys_width;
  h = g_window_info.phys_height;
}

void WindowSystem::GetPadWindowPhysSize(int &w, int &h) {
  w = 0;
  h = 0;
}

double WindowSystem::GetWindowDPIScale() { return g_window_info.dpi_scale; }

double WindowSystem::GetPadDPIScale() { return 1.0; }

bool WindowSystem::IsPadWindowOpen() { return false; }

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
    if (g_swiftui_overlay_view)
      g_swiftui_overlay_view.hidden = YES;
  });
}

void WindowSystem::NotifyGameExited() {
  dispatch_async(dispatch_get_main_queue(), ^{
    if (g_swiftui_overlay_view)
      g_swiftui_overlay_view.hidden = NO;
  });
}

void WindowSystem::RefreshGameList() { CafeTitleList::Refresh(); }

void WindowSystem::CaptureInput(const ControllerState & /*currentState*/,
                                const ControllerState & /*lastState*/) {}

bool WindowSystem::IsFullScreen() { return g_window_info.is_fullscreen; }
