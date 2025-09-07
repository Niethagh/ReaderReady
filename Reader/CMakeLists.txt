cmake_minimum_required(VERSION 3.14)

project(Reader LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 COMPONENTS Core QUIET)
if (Qt6_FOUND)
  set(QT_TARGET Qt6::Core)
else()
  find_package(Qt5 COMPONENTS Core REQUIRED)
  set(QT_TARGET Qt5::Core)
endif()

find_path(ACR38USB_INCLUDE_DIR
  NAMES ReaderApi.h ReaderApi.hpp
  HINTS
    ${CMAKE_SOURCE_DIR}/../acr38usb/include
    ${CMAKE_SOURCE_DIR}/../../acr38usb/include
    /usr/local/include
    /usr/include
)
if(NOT ACR38USB_INCLUDE_DIR)
  message(FATAL_ERROR "Не найден ReaderApi.h/ReaderApi.hpp. Укажите -DACR38USB_INCLUDE_DIR=/путь/к/include")
endif()

add_executable(Reader
  src/main.cpp

)

target_include_directories(Reader PRIVATE ${ACR38USB_INCLUDE_DIR})

target_link_libraries(Reader PRIVATE ${QT_TARGET})

include(GNUInstallDirs)
install(TARGETS Reader
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(UNIX)
  add_custom_target(run
    COMMAND Reader --help
    DEPENDS Reader
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  )
endif()
