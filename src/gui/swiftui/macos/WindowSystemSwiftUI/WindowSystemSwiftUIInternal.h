#pragma once

#include "Common/precompiled.h"

#include <memory>
#include <string>

#include "interface/WindowSystem.h"

#ifdef __OBJC__
#import <Cocoa/Cocoa.h>
@class NSMenuItem;
@class NSView;
@class NSViewController;
@class NSWindow;
@class NSToolbar;
@class NSString;
#else
class NSMenuItem;
class NSView;
class NSViewController;
class NSWindow;
class NSToolbar;
class NSString;
#endif

class CemuApp;
class RenderCanvas;

extern "C" void *CemuCreateSwiftUIRootViewController(void);
extern "C" void CemuShowSettingsWindow(void);

#ifdef __OBJC__
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
#else
class CemuAppDelegate;
class CemuPadWindowDelegate;
#endif

namespace WindowSystemSwiftUIInternal {

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
extern NSView *g_swiftui_overlay_view;
extern NSViewController *g_root_view_controller;
extern NSViewController *g_game_view_controller;
extern NSToolbar *g_swiftui_toolbar;
extern CemuApp *g_cemu_app;

constexpr int kPadMinWidth = 320;
constexpr int kPadMinHeight = 180;
constexpr int kPadDefaultWidth = 854;
constexpr int kPadDefaultHeight = 480;

void UpdateMainWindowMetricsFromRendererHostView();
void ResizeMainRendererIfNeeded();

void UpdatePadWindowMetricsFromRendererHostView();
void ResizePadRendererIfNeeded();
bool IsPadWindowFullscreen(NSWindow *window);
void UpdatePadRestoreStateFromWindow(NSWindow *window);

void SetMainWindowTitle(NSString *title);
void SetMainWindowUnifiedTitleToolbar(bool enabled);
void UpdateTitleFromGame();
NSView *CreateInputCaptureHostView(NSRect frame, bool mainWindow);

bool InitializeRendererForMainView(NSView *mainView, int width, int height,
                                   std::string &errorOut);
bool InitializeRendererForPadView(NSView *padView, int width, int height,
                                  std::string &errorOut);

void UpdatePadViewMenuState();
void DestroyPadWindowIfExists();
bool CreatePadWindowIfNeeded(std::string &errorOut);
void ApplyPadWindowRequestedState();

} // namespace WindowSystemSwiftUIInternal
