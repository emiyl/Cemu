#include "Common/precompiled.h"

#include "gui/swiftui/CemuApp.h"
#include "gui/swiftui/WindowSystemSwiftUI/WindowSystemSwiftUIInternal.h"
#include "gui/swiftui/canvas/RenderCanvas.h"

#include "gui/swiftui/MainWindow.h"

#include "Cafe/CafeSystem.h"
#include "config/ActiveSettings.h"

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

using namespace WindowSystemSwiftUIInternal;

@implementation CemuAppDelegate

- (BOOL)windowShouldClose:(NSWindow *)sender {
  if (sender == g_main_window) {
    [self quitApp:sender];
    return NO;
  }

  return YES;
}

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

  NSMenu *editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
  NSMenuItem *editMenuItem =
      [mainMenu addItemWithTitle:@"Edit" action:nil keyEquivalent:@""];
  [mainMenu setSubmenu:editMenu forItem:editMenuItem];

  NSMenuItem *preferencesItem =
      [editMenu addItemWithTitle:@"Preferences..."
                          action:@selector(openPreferences:)
                   keyEquivalent:@","];
  [preferencesItem setTarget:self];

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
  g_cemu_app->OnExit();
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
