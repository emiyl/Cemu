#include "Common/precompiled.h"

#include "gui/swiftui/CemuApp.h"
#include "gui/swiftui/MainWindow.h"
#include "gui/swiftui/canvas/RenderCanvas.h"
#include "interface/WindowSystem.h"

#include "Cafe/CafeSystem.h"
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
- (void)togglePadView:(id)sender;
- (void)showHelp:(id)sender;
- (void)showAbout:(id)sender;
@end

@interface CemuPadWindowDelegate : NSObject <NSWindowDelegate>
@end

namespace {
extern WindowSystem::WindowInfo g_window_info;
extern NSWindow *g_main_window;
extern CemuAppDelegate *g_app_delegate;
extern CemuPadWindowDelegate *g_pad_window_delegate;
extern NSView *g_renderer_host_view;
extern std::unique_ptr<RenderCanvas> g_renderer_canvas;
extern NSWindow *g_pad_window;
extern NSView *g_pad_renderer_host_view;
extern std::unique_ptr<RenderCanvas> g_pad_renderer_canvas;
extern NSMenuItem *g_pad_view_menu_item;
extern bool g_pad_view_requested;

constexpr int kPadMinWidth = 320;
constexpr int kPadMinHeight = 180;
constexpr int kPadDefaultWidth = 854;
constexpr int kPadDefaultHeight = 480;

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
  if (!g_renderer_canvas || !g_renderer_host_view)
    return;

  g_renderer_canvas->Resize(g_renderer_host_view,
                            (int)g_window_info.width.load(),
                            (int)g_window_info.height.load(), true);
}

void UpdatePadWindowMetricsFromRendererHostView() {
  if (!g_pad_renderer_host_view)
    return;

  const NSRect points = g_pad_renderer_host_view.bounds;
  const NSRect pixels = [g_pad_renderer_host_view convertRectToBacking:points];

  const int width = std::max(1, (int)std::lround(points.size.width));
  const int height = std::max(1, (int)std::lround(points.size.height));
  const int physWidth = std::max(1, (int)std::lround(pixels.size.width));
  const int physHeight = std::max(1, (int)std::lround(pixels.size.height));

  g_window_info.pad_width = width;
  g_window_info.pad_height = height;
  g_window_info.phys_pad_width = physWidth;
  g_window_info.phys_pad_height = physHeight;
  g_window_info.pad_dpi_scale = (double)physWidth / (double)width;
}

void ResizePadRendererIfNeeded() {
  if (!g_pad_renderer_canvas || !g_pad_renderer_host_view)
    return;

  g_pad_renderer_canvas->Resize(g_pad_renderer_host_view,
                                (int)g_window_info.pad_width.load(),
                                (int)g_window_info.pad_height.load(), false);
}

bool IsPadWindowFullscreen(NSWindow *window) {
  if (!window)
    return false;

  return (window.styleMask & NSWindowStyleMaskFullScreen) != 0;
}

void UpdatePadRestoreStateFromWindow(NSWindow *window) {
  if (!window)
    return;

  const bool isMaximized = [window isZoomed] && !IsPadWindowFullscreen(window);
  g_window_info.pad_maximized = isMaximized;
  if (!isMaximized && !IsPadWindowFullscreen(window)) {
    const NSRect frame = window.frame;
    g_window_info.restored_pad_x = (int)std::lround(frame.origin.x);
    g_window_info.restored_pad_y = (int)std::lround(frame.origin.y);
    g_window_info.restored_pad_width = (int)std::lround(frame.size.width);
    g_window_info.restored_pad_height = (int)std::lround(frame.size.height);
  }
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

void SetMainWindowUnifiedTitleToolbar(bool enabled) {
  if (!g_main_window)
    return;

  NSWindowStyleMask styleMask = g_main_window.styleMask;
  if (enabled)
    styleMask |= NSWindowStyleMaskUnifiedTitleAndToolbar;
  else
    styleMask &= ~NSWindowStyleMaskUnifiedTitleAndToolbar;

  if (styleMask != g_main_window.styleMask)
    [g_main_window setStyleMask:styleMask];

  if (@available(macOS 11.0, *)) {
    g_main_window.toolbarStyle =
        enabled ? NSWindowToolbarStyleUnified : NSWindowToolbarStyleAutomatic;
  }

  [g_main_window setTitlebarAppearsTransparent:NO];
}

void UpdateTitleFromGame() {
  const std::string titleName = CafeSystem::GetForegroundTitleName();
  if (!titleName.empty() && g_main_window) {
    SetMainWindowTitle([NSString stringWithUTF8String:titleName.c_str()]);
  }
}

bool InitializeRendererForMainView(NSView *mainView, int width, int height,
                                   std::string &errorOut) {
  if (!mainView || !g_main_window) {
    errorOut = "Main window view is not available.";
    return false;
  }

  const auto api = ActiveSettings::GetGraphicsAPI();
  if (api == kVulkan)
    g_renderer_canvas = CreateVulkanCanvas();
#if ENABLE_METAL
  else
    g_renderer_canvas = CreateMetalCanvas();
#endif

  if (!g_renderer_canvas) {
    errorOut = "Renderer canvas could not be created.";
    return false;
  }

  try {
    return g_renderer_canvas->Initialize([g_main_window contentView], mainView,
                                         width, height, true, errorOut);
  } catch (const std::exception &ex) {
    errorOut = fmt::format("Failed to initialize renderer: {}", ex.what());
    return false;
  }
}

bool InitializeRendererForPadView(NSView *padView, int width, int height,
                                  std::string &errorOut) {
  if (!padView || !g_pad_window) {
    errorOut = "Pad window view is not available.";
    return false;
  }

  const auto api = ActiveSettings::GetGraphicsAPI();
  if (api == kVulkan)
    g_pad_renderer_canvas = CreateVulkanCanvas();
#if ENABLE_METAL
  else
    g_pad_renderer_canvas = CreateMetalCanvas();
#endif

  if (!g_pad_renderer_canvas) {
    errorOut = "Pad renderer canvas could not be created.";
    return false;
  }

  try {
    return g_pad_renderer_canvas->Initialize(
        [g_pad_window contentView], padView, width, height, false, errorOut);
  } catch (const std::exception &ex) {
    errorOut = fmt::format("Failed to initialize renderer: {}", ex.what());
    return false;
  }
}

void UpdatePadViewMenuState() {
  if (!g_pad_view_menu_item)
    return;

  [g_pad_view_menu_item setEnabled:YES];
  [g_pad_view_menu_item setState:g_pad_view_requested ? NSControlStateValueOn
                                                      : NSControlStateValueOff];
}

void DestroyPadWindowIfExists() {
  if (g_pad_window)
    UpdatePadRestoreStateFromWindow(g_pad_window);

  g_pad_renderer_canvas.reset();
  g_pad_renderer_host_view = nil;

  if (g_pad_window) {
    [g_pad_window setDelegate:nil];
    [g_pad_window orderOut:nil];
    [g_pad_window close];
  }
  g_pad_window = nil;

  g_window_info.pad_open = false;
  g_window_info.pad_width = 0;
  g_window_info.pad_height = 0;
  g_window_info.phys_pad_width = 0;
  g_window_info.phys_pad_height = 0;
  g_window_info.pad_dpi_scale = 1.0;
}

bool CreatePadWindowIfNeeded(std::string &errorOut) {
  if (g_pad_window)
    return true;

  if (!CafeSystem::IsTitleRunning())
    return true;

  const int restoredWidth = (int)g_window_info.restored_pad_width.load();
  const int restoredHeight = (int)g_window_info.restored_pad_height.load();
  const bool hasRestoredSize =
      restoredWidth >= kPadMinWidth && restoredHeight >= kPadMinHeight;

  const int width = hasRestoredSize ? restoredWidth : kPadDefaultWidth;
  const int height = hasRestoredSize ? restoredHeight : kPadDefaultHeight;
  NSRect frame = NSMakeRect(200.0, 200.0, (CGFloat)width, (CGFloat)height);

  if (g_window_info.restored_pad_x.load() != -1 &&
      g_window_info.restored_pad_y.load() != -1) {
    frame.origin.x = (CGFloat)g_window_info.restored_pad_x.load();
    frame.origin.y = (CGFloat)g_window_info.restored_pad_y.load();
  }

  const NSWindowStyleMask style =
      NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
      NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;

  g_pad_window = [[NSWindow alloc] initWithContentRect:frame
                                             styleMask:style
                                               backing:NSBackingStoreBuffered
                                                 defer:NO];
  if (!g_pad_window) {
    errorOut = "Failed to create GamePad window.";
    return false;
  }

  [g_pad_window setTitle:@"GamePad View"];
  [g_pad_window setContentMinSize:NSMakeSize(kPadMinWidth, kPadMinHeight)];
  [g_pad_window setTitlebarAppearsTransparent:NO];
  if (!g_pad_window_delegate)
    g_pad_window_delegate = [[CemuPadWindowDelegate alloc] init];
  [g_pad_window setDelegate:g_pad_window_delegate];

  NSView *contentView = [g_pad_window contentView];
  if (!contentView) {
    errorOut = "GamePad content view is not available.";
    DestroyPadWindowIfExists();
    return false;
  }

  g_pad_renderer_host_view = [[NSView alloc] initWithFrame:contentView.bounds];
  g_pad_renderer_host_view.autoresizingMask =
      NSViewWidthSizable | NSViewHeightSizable;
  g_pad_renderer_host_view.wantsLayer = YES;
  g_pad_renderer_host_view.layer.backgroundColor = NSColor.blackColor.CGColor;
  [contentView addSubview:g_pad_renderer_host_view];

  [g_pad_window makeKeyAndOrderFront:nil];
  [contentView layoutSubtreeIfNeeded];

  UpdatePadWindowMetricsFromRendererHostView();
  if (!InitializeRendererForPadView(
          g_pad_renderer_host_view, (int)g_window_info.pad_width.load(),
          (int)g_window_info.pad_height.load(), errorOut)) {
    DestroyPadWindowIfExists();
    return false;
  }

  if (g_window_info.pad_maximized)
    [g_pad_window zoom:nil];

  g_window_info.pad_open = true;
  return true;
}

void ApplyPadWindowRequestedState() {
  UpdatePadViewMenuState();
  if (!CafeSystem::IsTitleRunning()) {
    DestroyPadWindowIfExists();
    return;
  }

  if (!g_pad_view_requested) {
    DestroyPadWindowIfExists();
    return;
  }

  std::string errorOut;
  if (!CreatePadWindowIfNeeded(errorOut)) {
    g_pad_view_requested = false;
    UpdatePadViewMenuState();
    WindowSystem::ShowErrorDialog(errorOut, "GamePad window failed");
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

  g_pad_view_menu_item = [viewMenu addItemWithTitle:@"Separate GamePad View"
                                             action:@selector(togglePadView:)
                                      keyEquivalent:@""];
  [g_pad_view_menu_item setTarget:self];
  [g_pad_view_menu_item setState:NSControlStateValueOff];
  [g_pad_view_menu_item setEnabled:NO];

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

  UpdateTitleFromGame();
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

- (void)togglePadView:(id)sender {
  (void)sender;
  g_pad_view_requested = !g_pad_view_requested;
  ApplyPadWindowRequestedState();
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

@implementation CemuPadWindowDelegate

- (void)windowDidResize:(NSNotification *)notification {
  if (!g_pad_window || notification.object != g_pad_window)
    return;
  UpdatePadRestoreStateFromWindow(g_pad_window);
  UpdatePadWindowMetricsFromRendererHostView();
  ResizePadRendererIfNeeded();
}

- (void)windowDidMove:(NSNotification *)notification {
  if (!g_pad_window || notification.object != g_pad_window)
    return;
  UpdatePadRestoreStateFromWindow(g_pad_window);
}

- (void)windowDidChangeBackingProperties:(NSNotification *)notification {
  if (!g_pad_window || notification.object != g_pad_window)
    return;
  UpdatePadWindowMetricsFromRendererHostView();
  ResizePadRendererIfNeeded();
}

- (void)windowWillClose:(NSNotification *)notification {
  if (!g_pad_window || notification.object != g_pad_window)
    return;

  UpdatePadRestoreStateFromWindow(g_pad_window);
  g_pad_renderer_canvas.reset();
  g_pad_renderer_host_view = nil;
  g_pad_window = nil;

  g_window_info.pad_open = false;
  g_window_info.pad_width = 0;
  g_window_info.pad_height = 0;
  g_window_info.phys_pad_width = 0;
  g_window_info.phys_pad_height = 0;
  g_window_info.pad_dpi_scale = 1.0;

  g_pad_view_requested = false;
  UpdatePadViewMenuState();
}

@end

namespace {
WindowSystem::WindowInfo g_window_info{};
NSWindow *g_main_window = nil;
CemuAppDelegate *g_app_delegate = nil;
CemuPadWindowDelegate *g_pad_window_delegate = nil;
NSView *g_renderer_host_view = nil;
std::unique_ptr<RenderCanvas> g_renderer_canvas;
NSWindow *g_pad_window = nil;
NSView *g_pad_renderer_host_view = nil;
std::unique_ptr<RenderCanvas> g_pad_renderer_canvas;
NSMenuItem *g_pad_view_menu_item = nil;
bool g_pad_view_requested = false;
NSView *g_swiftui_overlay_view = nil;
NSViewController *g_root_view_controller = nil;
NSViewController *g_game_view_controller = nil;
NSToolbar *g_swiftui_toolbar = nil;
} // namespace

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
  app = new CemuApp();
  app->OnInit();

  @autoreleasepool {
    NSApplication *app = [NSApplication sharedApplication];
    [app setActivationPolicy:NSApplicationActivationPolicyRegular];

    // Setup application delegate with menu bar
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

    g_renderer_host_view =
        [[NSView alloc] initWithFrame:windowContentView.bounds];
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

    // Use standard window chrome while the renderer owns the content.
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

    // Restore unified title/toolbar when returning to the game list UI.
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
