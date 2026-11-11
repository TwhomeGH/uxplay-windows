# QuickTime USB capture backend. Kept separate from the AirPlay engine.

cmake_minimum_required(VERSION 3.16)

find_package(PkgConfig REQUIRED)
pkg_check_modules(IMOBILEDEVICE REQUIRED IMPORTED_TARGET libimobiledevice-1.0)
pkg_check_modules(USBMUXD REQUIRED IMPORTED_TARGET libusbmuxd)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET
    gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0
)

add_library(usbcapture STATIC
    src/capture.cpp
    src/device.cpp
    src/session.cpp
    src/usbmuxd_client.cpp
    src/quicktime_session.cpp
    src/pipeline.cpp
)

target_compile_features(usbcapture PUBLIC cxx_std_17)
target_include_directories(usbcapture PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(usbcapture PRIVATE
    PkgConfig::IMOBILEDEVICE
    PkgConfig::USBMUXD
    PkgConfig::GSTREAMER
)
