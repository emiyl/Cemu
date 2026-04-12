add_library(CemuSwiftUiGui STATIC
  WindowSystemSwiftUI.mm
)

set_property(TARGET CemuSwiftUiGui PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_include_directories(CemuSwiftUiGui PRIVATE "../../") # To find src/Common/platform.h
target_include_directories(CemuSwiftUiGui PUBLIC "../")

# Link against core libraries for access to their headers and dependencies
target_link_libraries(CemuSwiftUiGui PRIVATE
  CemuCommon
)

find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
target_link_libraries(CemuSwiftUiGui PRIVATE ${COCOA_FRAMEWORK})

if(ALLOW_PORTABLE)
  target_compile_definitions(CemuSwiftUiGui PRIVATE CEMU_ALLOW_PORTABLE)
endif()
