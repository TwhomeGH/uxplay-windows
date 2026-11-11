cmake_minimum_required(VERSION 3.16)
project(AirPlayGUI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 COMPONENTS Core Widgets Network REQUIRED)

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
    src/resources.rc
)

target_link_libraries(uxplay-windows PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    uxplay
)

if(ENABLE_USB_CAPTURE)
    target_link_libraries(uxplay-windows PRIVATE usbcapture)
endif()

target_include_directories(uxplay-windows PRIVATE
    ${CMAKE_SOURCE_DIR}/libuxplay
)
