cmake_minimum_required(VERSION 3.5)

project(rik2gui VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

set(PROJECT_SOURCES
        src/main.cpp
        src/mainwindow.cpp
        src/mainwindow.hpp
        mainwindow.ui
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(rik2gui
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )

ind_path(READERAPI_INCLUDE
  NAMES ReaderApi.h ReaderApi.hpp
  HINTS ${CMAKE_SOURCE_DIR}/../acr38usb/include /usr/local/include /usr/include
)
if(NOT READERAPI_INCLUDE)
  message(FATAL_ERROR "Не найден ReaderApi.h/ReaderApi.hpp. Укажите -DREADERAPI_INCLUDE=/path")
endif()

# Define target properties for Android with Qt 6 as:
#    set_property(TARGET rik2gui APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(rik2gui SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(rik2gui
            ${PROJECT_SOURCES}
            include/ReaderSession.hpp
            include/Rik2Model.hpp
            include/Rik2Worker.hpp
            include/Hex.hpp
            src/ReaderSession.cpp
            src/Rik2Model.cpp
            src/Rik2Worker.cpp
            assets/sample_rik2_layout.json
            mainwindow.ui
        )
    endif()
endif()

target_include_directories(rik2gui PRIVATE ${READERAPI_INCLUDE} ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(rik2gui PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.rik2gui)
endif()
set_target_properties(rik2gui PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)

install(TARGETS rik2gui RUNTIME DESTINATION bin)
install(FILES assets/sample_rik2_layout.json DESTINATION share/rik2gui)
install(TARGETS rik2gui
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(CMAKE_BUILD_TYPE MATCHES "Debug")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(rik2gui)
endif()
