cmake_minimum_required(VERSION 3.14)

project(AmazonTemplate3Lib LANGUAGES CXX)

include(../common.cmake)

include(../../common/openai/openai.cmake)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Network)

add_library(AmazonTemplate3Lib STATIC
  AmazonTemplate3Lib.cpp
  AmazonTemplate3Lib.h
  ${OPENAI_FILES}
  Attribute.h
  Attribute.cpp
  TemplateFiller.h
  TemplateFiller.cpp
)

target_link_libraries(AmazonTemplate3Lib PRIVATE
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Core)

target_compile_definitions(AmazonTemplate3Lib PRIVATE AMAZONTEMPLATE3LIB_LIBRARY)
