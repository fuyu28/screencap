cmake_minimum_required(VERSION 3.20)
project(screencap LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(configure_screencap_target target)
  target_include_directories(${target} PRIVATE src)

  target_compile_definitions(${target} PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
  )
endfunction()

# Shared core linked into both executables.
add_library(screencap_core STATIC
  src/cli/cli.cpp
  src/common/logging.cpp
  src/common/util.cpp
  src/winsys/window_enum.cpp
  src/winsys/monitor_enum.cpp
  src/image/crop.cpp
  src/image/image_stats.cpp
  src/image/encode_wic_png.cpp
  src/capture/capture_gdi.cpp
  src/capture/capture_dxgi.cpp
  src/capture/capture_wgc.cpp
  src/capture/d3d_readback.cpp
)
configure_screencap_target(screencap_core)
target_link_libraries(screencap_core PUBLIC
  d3d11
  dxgi
  windowsapp
  dwmapi
  shcore
  windowscodecs
  comctl32
  comdlg32
)

add_executable(screencap WIN32
  src/apps/gui_main.cpp
  src/gui/gui.cpp
)
configure_screencap_target(screencap)
target_link_libraries(screencap PRIVATE screencap_core)
if(MSVC)
  target_link_options(screencap PRIVATE /ENTRY:mainCRTStartup)
endif()

add_executable(screencap-cli
  src/apps/cli_main.cpp
)
configure_screencap_target(screencap-cli)
target_link_libraries(screencap-cli PRIVATE screencap_core)
