#include "Common/precompiled.h"

#include "gui/swiftui/macos/CemuApp.h"
#include "gui/swiftui/macos/WindowSystemSwiftUI/WindowSystemSwiftUIInternal.h"
#include "gui/swiftui/macos/canvas/RenderCanvas.h"

namespace WindowSystemSwiftUIInternal {

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
CemuApp *g_cemu_app = nullptr;

} // namespace WindowSystemSwiftUIInternal
