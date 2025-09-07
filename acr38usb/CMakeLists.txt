cmake_minimum_required(VERSION 3.14)

project(acr38usb LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

add_library(acr38usb SHARED

  src/acr38usb.cpp
  src/acr38usb.h
  src/exports.cpp
  include/ReaderApi.h
  include/ReaderApi.hpp
)

target_link_libraries(acr38usb PRIVATE PkgConfig::LIBUSB)
target_include_directories(acr38usb
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${LIBUSB_INCLUDE_DIRS}
)
set_target_properties(acr38usb PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  OUTPUT_NAME "acr38usb"
)

install(TARGETS acr38usb LIBRARY DESTINATION lib)
install(FILES include/ReaderApi.h include/ReaderApi.hpp DESTINATION include)

target_compile_definitions(acr38usb PRIVATE ACR38USB_LIBRARY)
