enable_language(Swift)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_executable(CemuSwiftUiIosApp MACOSX_BUNDLE
    ../../main.cpp
    ios/CemuIosApp.swift
    ios/IosBackends.swift
    ios/LaunchBridge.cpp
    ios/WindowSystemIOS.cpp
    ios/WindowSystemIOS_GameLoad.mm
    ios/CemuAppIOS.mm
    common/ContentView.swift
    common/GameList.swift
    common/GameList.cpp
    common/GameList.h
    common/SettingsView.swift
    common/SettingsView.cpp
    common/SettingsView.h
    macos/RegionStrings.cpp
    macos/RegionStrings.h
    macos/CemuApp.cpp
    macos/CemuApp.h
    macos/SwiftUICemuConfig.cpp
    macos/SwiftUICemuConfig.h
    common/settingsTabs/GeneralSettingsTab.swift
    common/settingsTabs/GraphicsSettingsTab.swift
    common/settingsTabs/AudioSettingsTab.swift
    common/settingsTabs/OverlaySettingsTab.swift
    common/settingsTabs/AccountSettingsTab.swift
    common/settingsTabs/DebugSettingsTab.swift
    ios/LaunchScreen.storyboard
  )

  set_target_properties(CemuSwiftUiIosApp PROPERTIES
    Swift_LANGUAGE_VERSION 5
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/Info-iOS.plist.in"
    MACOSX_BUNDLE_GUI_IDENTIFIER "info.cemu.CemuSwiftUIiOS"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "info.cemu.CemuSwiftUIiOS"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "YES"
  )

  target_compile_definitions(CemuSwiftUiIosApp PRIVATE VK_NO_PROTOTYPES)

  target_include_directories(CemuSwiftUiIosApp PRIVATE "../../")
  target_include_directories(CemuSwiftUiIosApp PUBLIC "../")
  target_include_directories(CemuSwiftUiIosApp PRIVATE "${CMAKE_SOURCE_DIR}/dependencies/Vulkan-Headers/include")

  if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    target_include_directories(CemuSwiftUiIosApp PRIVATE
      "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
    )
  endif()

  target_link_libraries(CemuSwiftUiIosApp PRIVATE
    CemuAudio
    CemuCafe
    CemuCommon
    CemuComponents
    CemuConfig
    CemuInput
    CemuResource
    CemuUtil
  )

  return()
endif()

add_library(CemuSwiftUiGui STATIC
  macos/CemuApp.cpp
  macos/CemuApp.h
  macos/MainWindow.cpp
  macos/MainWindow.h
  macos/canvas/RenderCanvas.h
  macos/canvas/MetalCanvas.mm
  macos/canvas/VulkanCanvas.mm
  macos/RegionStrings.cpp
  macos/RegionStrings.h
  macos/WindowSystemSwiftUI/WindowSystemSwiftUIInternal.h
  macos/WindowSystemSwiftUI/WindowSystemSwiftUIState.mm
  macos/WindowSystemSwiftUI/WindowSystemSwiftUIHelpers.mm
  macos/WindowSystemSwiftUI/WindowSystemSwiftUIDelegates.mm
  macos/WindowSystemSwiftUI.mm
  macos/SwiftUICemuConfig.cpp
  macos/SwiftUICemuConfig.h
  common/ContentView.swift
  common/GameList.swift
  common/GameList.cpp
  common/GameList.h
  common/SettingsView.swift
  common/settingsTabs/GeneralSettingsTab.swift
  common/settingsTabs/GraphicsSettingsTab.swift
  common/settingsTabs/AudioSettingsTab.swift
  common/settingsTabs/OverlaySettingsTab.swift
  common/settingsTabs/AccountSettingsTab.swift
  common/settingsTabs/DebugSettingsTab.swift
  common/SettingsView.cpp
  common/SettingsView.h
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
