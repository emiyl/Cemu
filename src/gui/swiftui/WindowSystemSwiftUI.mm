#include "Common/precompiled.h"

#include "interface/WindowSystem.h"

#import <Cocoa/Cocoa.h>

namespace {
WindowSystem::WindowInfo g_window_info{};
NSWindow *g_main_window = nil;
} // namespace

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
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    const NSRect frame = NSMakeRect(120.0, 120.0, 1280.0, 720.0);
    const NSWindowStyleMask style =
        NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
        NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;

    g_main_window = [[NSWindow alloc] initWithContentRect:frame
                                                styleMask:style
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
    [g_main_window setTitle:@"Cemu (SwiftUI)"];
    [g_main_window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    g_window_info.app_active = true;
    g_window_info.width = static_cast<int32_t>(frame.size.width);
    g_window_info.height = static_cast<int32_t>(frame.size.height);
    g_window_info.phys_width = g_window_info.width.load();
    g_window_info.phys_height = g_window_info.height.load();
    g_window_info.dpi_scale = 1.0;
    g_window_info.pad_open = false;
    g_window_info.pad_width = 0;
    g_window_info.pad_height = 0;
    g_window_info.phys_pad_width = 0;
    g_window_info.phys_pad_height = 0;
    g_window_info.pad_dpi_scale = 1.0;
    g_window_info.is_fullscreen = false;
    g_window_info.debugger_focused = false;
    g_window_info.window_main.backend =
        WindowSystem::WindowHandleInfo::Backend::Cocoa;
    g_window_info.window_main.display = nullptr;
    g_window_info.window_main.surface = (__bridge void *)g_main_window;

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

  [g_main_window setTitle:title];
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

void WindowSystem::NotifyGameLoaded() {}

void WindowSystem::NotifyGameExited() {}

void WindowSystem::RefreshGameList() {}

void WindowSystem::CaptureInput(const ControllerState & /*currentState*/,
                                const ControllerState & /*lastState*/) {}

bool WindowSystem::IsFullScreen() { return g_window_info.is_fullscreen; }
