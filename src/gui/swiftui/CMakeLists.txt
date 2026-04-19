enable_language(Swift)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_executable(CemuSwiftUiIosApp MACOSX_BUNDLE
    CemuIosApp.swift
    ContentView.swift
    components/GameList.swift
    components/SettingsView.swift
    components/settingsTabs/GeneralSettingsTab.swift
    components/settingsTabs/GraphicsSettingsTab.swift
    components/settingsTabs/AudioSettingsTab.swift
    components/settingsTabs/OverlaySettingsTab.swift
    components/settingsTabs/AccountSettingsTab.swift
    components/settingsTabs/DebugSettingsTab.swift
    LaunchScreen.storyboard
  )

  set_target_properties(CemuSwiftUiIosApp PROPERTIES
    Swift_LANGUAGE_VERSION 5
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info-iOS.plist.in"
    MACOSX_BUNDLE_GUI_IDENTIFIER "info.cemu.CemuSwiftUIiOS"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "info.cemu.CemuSwiftUIiOS"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
    XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
  )

  return()
endif()

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
