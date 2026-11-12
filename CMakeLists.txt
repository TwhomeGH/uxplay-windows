cmake_minimum_required(VERSION 3.16)
project(AirPlayGUI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 COMPONENTS Core Widgets Network REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(USB_GST REQUIRED gstreamer-1.0 gstreamer-video-1.0)

option(ENABLE_USB_CAPTURE "Build the QuickTime USB capture backend (experimental)" OFF)

# build uxplay as a static library
add_subdirectory(libuxplay)

if(ENABLE_USB_CAPTURE)
    add_subdirectory(usbcapture)
endif()

# the WIN32 flag here tells Windows this is a GUI app, not a Console app
add_executable(uxplay-windows WIN32
    src/main.cpp
    src/mainwindow.cpp
    src/airplayworker.cpp
    src/settings.cpp
    src/logging.cpp
    src/mdns_responder.cpp
    src/cleanoutputwindow.cpp
    src/usbobscontroller.cpp
    src/usbpython.cpp
    src/usbdeviceconnector.cpp
    src/diagnostics.cpp
    src/resources.rc
)

target_link_libraries(uxplay-windows PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    uxplay
    ${USB_GST_LIBRARIES}
)

if(WIN32)
    target_link_libraries(uxplay-windows PRIVATE psapi)
endif()

target_include_directories(uxplay-windows PRIVATE
    ${USB_GST_INCLUDE_DIRS}
)

target_compile_options(uxplay-windows PRIVATE ${USB_GST_CFLAGS_OTHER})

target_compile_definitions(uxplay-windows PRIVATE
    USB_BRIDGE_SCRIPT="${CMAKE_SOURCE_DIR}/usbcapture/spike/tools/stream_to_tcp.py"
)

execute_process(
    COMMAND git describe --tags --always --dirty --match "v[0-9]*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE UXPLAY_APP_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT UXPLAY_APP_VERSION)
    set(UXPLAY_APP_VERSION "dev")
endif()

execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE UXPLAY_GIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT UXPLAY_GIT_SHA)
    set(UXPLAY_GIT_SHA "unknown")
endif()

target_compile_definitions(uxplay-windows PRIVATE
    UXPLAY_APP_VERSION="${UXPLAY_APP_VERSION}"
    UXPLAY_GIT_SHA="${UXPLAY_GIT_SHA}"
    UXPLAY_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
)

add_custom_command(TARGET uxplay-windows POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:uxplay-windows>/usbcapture"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/usbcapture/spike/tools/stream_to_tcp.py"
        "$<TARGET_FILE_DIR:uxplay-windows>/usbcapture/stream_to_tcp.py"
)

if(ENABLE_USB_CAPTURE)
    target_link_libraries(uxplay-windows PRIVATE usbcapture)
endif()

target_include_directories(uxplay-windows PRIVATE
    ${CMAKE_SOURCE_DIR}/libuxplay
)
