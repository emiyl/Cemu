enable_language(Swift)

add_library(CemuSwiftUiGui STATIC
  CemuApp.cpp
  CemuApp.h
  MainWindow.cpp
  MainWindow.h
  canvas/RenderCanvas.h
  canvas/MetalCanvas.mm
  canvas/VulkanCanvas.mm
  RegionStrings.cpp
  RegionStrings.h
  WindowSystemSwiftUI/WindowSystemSwiftUIInternal.h
  WindowSystemSwiftUI/WindowSystemSwiftUIState.mm
  WindowSystemSwiftUI/WindowSystemSwiftUIHelpers.mm
  WindowSystemSwiftUI/WindowSystemSwiftUIDelegates.mm
  WindowSystemSwiftUI.mm
  ContentView.swift
  components/GameList.swift
  components/GameList.cpp
  components/GameList.h
  components/SettingsView.swift
  components/settingsTabs/GeneralSettingsTab.swift
  components/settingsTabs/GraphicsSettingsTab.swift
  components/settingsTabs/AudioSettingsTab.swift
  components/settingsTabs/OverlaySettingsTab.swift
  components/settingsTabs/AccountSettingsTab.swift
  components/settingsTabs/DebugSettingsTab.swift
  components/SettingsView.cpp
  components/SettingsView.h
)

set_target_properties(CemuSwiftUiGui PROPERTIES
  Swift_LANGUAGE_VERSION 5
)

set_property(TARGET CemuSwiftUiGui PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_include_directories(CemuSwiftUiGui PRIVATE "../../")
target_include_directories(CemuSwiftUiGui PUBLIC "../")

target_link_libraries(CemuSwiftUiGui PRIVATE
  CemuCommon
  CemuConfig
  CemuCafe
  CemuResource
)

find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
target_link_libraries(CemuSwiftUiGui PRIVATE ${COCOA_FRAMEWORK})

if(ALLOW_PORTABLE)
  target_compile_definitions(CemuSwiftUiGui PRIVATE CEMU_ALLOW_PORTABLE)
endif()
