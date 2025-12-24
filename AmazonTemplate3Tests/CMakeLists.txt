cmake_minimum_required(VERSION 3.5)

project(TestOpenAi2 LANGUAGES CXX)

# Option to run real contract tests (requires API key)
# Option to run real contract tests (requires API key)
option(DO_REAL_TESTS "Run real OpenAI contract tests" OFF)

if(DO_REAL_TESTS)
    add_definitions(-DDO_REAL_TESTS=1)
    if(DEFINED OPEN_AI_API_KEY)
        add_definitions(-DOPEN_AI_API_KEY="${OPEN_AI_API_KEY}")
        message(STATUS "DO_REAL_TESTS is ON. API Key injected.")
    else()
        message(WARNING "DO_REAL_TESTS is ON but OPEN_AI_API_KEY is NOT defined. Tests may fail to compile or run.")
    endif()
else()
    message(STATUS "DO_REAL_TESTS is OFF. Contract tests will be skipped.")
endif()

enable_testing()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Network Test Gui)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Network Test Gui)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(../common.cmake)

if(NOT DEFINED OPEN_AI_API_KEY)
    set(OPEN_AI_API_KEY "TODO" CACHE STRING "The open AI api key")
endif()
if(OPEN_AI_API_KEY STREQUAL "TODO")
    message(FATAL_ERROR "Variable OPEN_AI_API_KEY is not defined.")
else()
    add_definitions(-DOPEN_AI_API_KEY="${OPEN_AI_API_KEY}")
endif()

add_executable(AmazonTemplate3Tests tst_testopenai2.cpp)

target_link_libraries(AmazonTemplate3Tests PRIVATE AmazonTemplate3Lib)
target_include_directories(AmazonTemplate3Tests PRIVATE ../AmazonTemplate3Lib)
target_include_directories(AmazonTemplate3Tests PRIVATE ../../common/openai/)

add_test(NAME AmazonTemplate3Tests COMMAND AmazonTemplate3Tests)

target_link_libraries(AmazonTemplate3Tests PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Gui)

