cmake_minimum_required(VERSION 3.14)

project(AmazonTemplate3Lib LANGUAGES CXX)

include(../common.cmake)

#include(../../common/workingdirectory/workingdirectory.cmake)
include(../../common/openai/openai2.cmake)
include(../../common/utils/utils.cmake)

include(fillers/fillers.cmake)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Network)

# Compile and install QXlsx
#sudo apt-get install libxkbcommon-dev
#cd /home/cedric/code
#git clone https://github.com/QtExcel/QXlsx.git
#cd QXlsx
#mkdir build
#cd build
#cmake ../QXlsx/ -DCMAKE_PREFIX_PATH=/home/cedric/Qt/6.7.3/gcc_64 -DCMAKE_BUILD_TYPE=Release
#cmake --build .
#cmake --install .
set(Qt_DIR_HINT "${Qt${QT_VERSION_MAJOR}_DIR}")
message("Qt found in path: ${Qt_DIR_HINT}")
find_package(QXlsxQt6 REQUIRED HINTS "${Qt_DIR_HINT}/../../../../lib/cmake/QXlsxQt6")

setup_qcoro(COMPONENTS Network)

add_library(AmazonTemplate3Lib STATIC
  AmazonTemplate3Lib.cpp
  AmazonTemplate3Lib.h
  ${OPENAI_FILES}
  #${WORKING_DIR_FILES}
  Attribute.h
  Attribute.cpp
  TemplateFiller.h
  TemplateFiller.cpp
  AttributesMandatoryAiTable.h
  AttributesMandatoryAiTable.cpp
  AttributesMandatoryTable.h
  AttributesMandatoryTable.cpp
  ExceptionTemplate.h
  ExceptionTemplate.cpp
  FileModelSources.h
  FileModelSources.cpp
  FileModelToFill.h
  FileModelToFill.cpp
  TableInfoExtractor.h
  TableInfoExtractor.cpp
  AttributeFlagsTable.h
  AttributeFlagsTable.cpp
  AttributeValueReplacedTable.h
  AttributeValueReplacedTable.cpp
  AttributePossibleMissingTable.h
  AttributePossibleMissingTable.cpp
  AttributeEquivalentTable.h
  AttributeEquivalentTable.cpp
  AiFailureTable.h
  AiFailureTable.cpp
  ${FILLER_FILES}
)

target_link_libraries(AmazonTemplate3Lib PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Core
    PUBLIC
    QXlsx::QXlsx
    QCoro::Network
)

add_definitions(-DCOL_SEP=",")
add_definitions(-DCELL_SEP="\\073")
add_definitions(-DSTR_TRUE="true")
add_definitions(-DSTR_FALSE="false")

target_compile_definitions(AmazonTemplate3Lib PRIVATE AMAZONTEMPLATE3LIB_LIBRARY)

# Compatibility stub: newer QXlsx versions reference xlsxreadsax.h in
# xlsxdocument.h but don't install it; the compat/ dir provides the stub.
target_include_directories(AmazonTemplate3Lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PUBLIC ${CMAKE_SOURCE_DIR}/compat)

# Library variant for unit tests (exposes protected hooks)
add_library(AmazonTemplate3Lib_Tests STATIC
  AmazonTemplate3Lib.cpp
  AmazonTemplate3Lib.h
  ${OPENAI_FILES}
  Attribute.h
  Attribute.cpp
  TemplateFiller.h
  TemplateFiller.cpp
  AttributesMandatoryAiTable.h
  AttributesMandatoryAiTable.cpp
  AttributesMandatoryTable.h
  AttributesMandatoryTable.cpp
  ExceptionTemplate.h
  ExceptionTemplate.cpp
  AttributeFlagsTable.h
  AttributeFlagsTable.cpp
  AttributeEquivalentTable.h
  AttributeEquivalentTable.cpp
  AttributePossibleMissingTable.h
  AttributePossibleMissingTable.cpp
  AttributeValueReplacedTable.h
  AttributeValueReplacedTable.cpp
  AiFailureTable.h
  AiFailureTable.cpp
  ${FILLER_FILES}
)

target_link_libraries(AmazonTemplate3Lib_Tests PUBLIC
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Core
    QXlsx::QXlsx
    QCoro::Network
)

target_compile_definitions(AmazonTemplate3Lib_Tests PUBLIC
    AMAZONTEMPLATE3LIB_LIBRARY
    OPENAI2_UNIT_TESTS
)

target_include_directories(AmazonTemplate3Lib_Tests PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PUBLIC ${CMAKE_SOURCE_DIR}/compat)
