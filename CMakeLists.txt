cmake_minimum_required(VERSION 3.20)
project(screencap LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(screencap
  src/main.cpp
  src/cli.cpp
  src/logging.cpp
  src/window_enum.cpp
  src/monitor_enum.cpp
  src/crop.cpp
  src/image_stats.cpp
  src/encode_wic_png.cpp
  src/capture_gdi.cpp
  src/capture_dxgi.cpp
  src/capture_wgc.cpp
  src/util.cpp
)

target_include_directories(screencap PRIVATE src)

target_compile_definitions(screencap PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

target_link_libraries(screencap PRIVATE
  d3d11
  dxgi
  windowsapp
  dwmapi
  shcore
  windowscodecs
)
