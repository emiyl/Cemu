#include "Common/precompiled.h"

#include "gui/swiftui/macos/WindowSystemSwiftUI/WindowSystemSwiftUIInternal.h"

#include "Cafe/CafeSystem.h"
#include "config/ActiveSettings.h"
#include "gui/swiftui/macos/canvas/RenderCanvas.h"
#include "input/InputManager.h"

@interface CemuInputCaptureView : NSView
- (instancetype)initWithFrame:(NSRect)frame mainWindow:(BOOL)mainWindow;
@end

@implementation CemuInputCaptureView {
  BOOL _mainWindow;
  NSPanGestureRecognizer *_panGesture;
}

- (instancetype)initWithFrame:(NSRect)frame mainWindow:(BOOL)mainWindow {
  self = [super initWithFrame:frame];
  if (!self)
    return nil;

  _mainWindow = mainWindow;
  _panGesture = [[NSPanGestureRecognizer alloc]
      initWithTarget:self
              action:@selector(handlePanGesture:)];
  [self addGestureRecognizer:_panGesture];

  return self;
}

- (BOOL)acceptsFirstResponder {
  return YES;
}

- (void)updateTrackingAreas {
  [super updateTrackingAreas];

  NSTrackingAreaOptions options = NSTrackingMouseMoved |
                                  NSTrackingActiveInKeyWindow |
                                  NSTrackingInVisibleRect;
  NSTrackingArea *trackingArea = [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                                              options:options
                                                                owner:self
                                                             userInfo:nil];
  [self addTrackingArea:trackingArea];
}

- (glm::ivec2)physicalPositionForPoint:(NSPoint)point {
  NSPoint backingPoint = [self convertPointToBacking:point];
  const NSRect backingBounds = [self convertRectToBacking:self.bounds];
  const double width = backingBounds.size.width;
  const double height = backingBounds.size.height;

  const double x = std::clamp(backingPoint.x, 0.0, std::max(0.0, width));
  const double yBottomUp =
      std::clamp(backingPoint.y, 0.0, std::max(0.0, height));
  const double yTopDown = std::max(0.0, height - yBottomUp);

  return {(int)std::lround(x), (int)std::lround(yTopDown)};
}

- (void)withMouseInfo:(void (^)(InputManager::MouseInfo &))handler {
  auto &input = InputManager::instance();
  InputManager::MouseInfo &mouse =
      _mainWindow ? input.m_main_mouse : input.m_pad_mouse;
  handler(mouse);
}

- (void)withTouchInfo:(void (^)(InputManager::MouseInfo &))handler {
  auto &input = InputManager::instance();
  InputManager::MouseInfo &touch =
      _mainWindow ? input.m_main_touch : input.m_pad_touch;
  handler(touch);
}

- (void)updateMousePositionFromEvent:(NSEvent *)event {
  NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
  const glm::ivec2 pos = [self physicalPositionForPoint:point];

  [self withMouseInfo:^(InputManager::MouseInfo &mouse) {
    std::scoped_lock lock(mouse.m_mutex);
    mouse.position = pos;
  }];
}

- (void)mouseMoved:(NSEvent *)event {
  [self updateMousePositionFromEvent:event];
}

- (void)mouseDragged:(NSEvent *)event {
  [self updateMousePositionFromEvent:event];
}

- (void)rightMouseDragged:(NSEvent *)event {
  [self updateMousePositionFromEvent:event];
}

- (void)mouseDown:(NSEvent *)event {
  NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
  const glm::ivec2 pos = [self physicalPositionForPoint:point];

  [self withMouseInfo:^(InputManager::MouseInfo &mouse) {
    std::scoped_lock lock(mouse.m_mutex);
    mouse.left_down = true;
    mouse.left_down_toggle = true;
    mouse.position = pos;
  }];
}

- (void)mouseUp:(NSEvent *)event {
  NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
  const glm::ivec2 pos = [self physicalPositionForPoint:point];

  [self withMouseInfo:^(InputManager::MouseInfo &mouse) {
    std::scoped_lock lock(mouse.m_mutex);
    mouse.left_down = false;
    mouse.position = pos;
  }];
}

- (void)rightMouseDown:(NSEvent *)event {
  NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
  const glm::ivec2 pos = [self physicalPositionForPoint:point];

  [self withMouseInfo:^(InputManager::MouseInfo &mouse) {
    std::scoped_lock lock(mouse.m_mutex);
    mouse.right_down = true;
    mouse.right_down_toggle = true;
    mouse.position = pos;
  }];
}

- (void)rightMouseUp:(NSEvent *)event {
  NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
  const glm::ivec2 pos = [self physicalPositionForPoint:point];

  [self withMouseInfo:^(InputManager::MouseInfo &mouse) {
    std::scoped_lock lock(mouse.m_mutex);
    mouse.right_down = false;
    mouse.position = pos;
  }];
}

- (void)scrollWheel:(NSEvent *)event {
  auto &input = InputManager::instance();
  input.m_mouse_wheel = (float)(event.scrollingDeltaY / 120.0);
}

- (void)handlePanGesture:(NSPanGestureRecognizer *)gesture {
  NSPoint point = [gesture locationInView:self];
  const glm::ivec2 pos = [self physicalPositionForPoint:point];

  const NSGestureRecognizerState state = gesture.state;
  const bool down = state == NSGestureRecognizerStateBegan ||
                    state == NSGestureRecognizerStateChanged;

  [self withTouchInfo:^(InputManager::MouseInfo &touch) {
    std::scoped_lock lock(touch.m_mutex);
    touch.position = pos;
    touch.left_down = down;
    if (down)
      touch.left_down_toggle = true;
  }];
}

@end

namespace WindowSystemSwiftUIInternal {

NSView *CreateInputCaptureHostView(NSRect frame, bool mainWindow) {
  return [[CemuInputCaptureView alloc] initWithFrame:frame
                                          mainWindow:mainWindow ? YES : NO];
}

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
  if (!titleName.empty() && g_main_window)
    SetMainWindowTitle([NSString stringWithUTF8String:titleName.c_str()]);
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
#ifdef ENABLE_METAL
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
#ifdef ENABLE_METAL
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
  [g_pad_window setAcceptsMouseMovedEvents:YES];
  if (!g_pad_window_delegate)
    g_pad_window_delegate = [[CemuPadWindowDelegate alloc] init];
  [g_pad_window setDelegate:g_pad_window_delegate];

  NSView *contentView = [g_pad_window contentView];
  if (!contentView) {
    errorOut = "GamePad content view is not available.";
    DestroyPadWindowIfExists();
    return false;
  }

  g_pad_renderer_host_view =
      CreateInputCaptureHostView(contentView.bounds, false);
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

} // namespace WindowSystemSwiftUIInternal
