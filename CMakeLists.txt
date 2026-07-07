cmake_minimum_required(VERSION 3.20)
project(screencap LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SCREENCAP_SOURCES
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

function(configure_screencap_target target)
  target_include_directories(${target} PRIVATE src)

  target_compile_definitions(${target} PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
  )

  target_link_libraries(${target} PRIVATE
    d3d11
    dxgi
    windowsapp
    dwmapi
    shcore
    windowscodecs
  )
endfunction()

add_executable(screencap ${SCREENCAP_SOURCES})
configure_screencap_target(screencap)

add_executable(screencapw WIN32 ${SCREENCAP_SOURCES})
configure_screencap_target(screencapw)
if(MSVC)
  target_link_options(screencapw PRIVATE /ENTRY:mainCRTStartup)
endif()
