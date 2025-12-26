cmake_minimum_required(VERSION 3.5)

project(AmazonTemplate3 VERSION 0.1 LANGUAGES CXX)

include(../common.cmake)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)

#include(../../common/openai/openai.cmake)
include(../../common/workingdirectory/workingdirectory.cmake)

include(gui/gui.cmake)

set(PROJECT_SOURCES
        main.cpp
        "../../common/types/types.h"
        "../../common/types/types.cpp"
        ${GUI_FILES}
        ${WORKING_DIR_FILES}
    )

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(AmazonTemplate3
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET AmazonTemplate3 APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(AmazonTemplate3 SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(AmazonTemplate3
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(AmazonTemplate3 PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network)

set_target_properties(AmazonTemplate3 PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER my.example.com
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

install(TARGETS AmazonTemplate3
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(AmazonTemplate3)
endif()
