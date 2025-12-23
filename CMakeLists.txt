cmake_minimum_required(VERSION 3.5)

project(AmazonTemplate3)

include(common.cmake)

add_subdirectory(AmazonTemplate3Lib)
add_subdirectory(AmazonTemplate3)
add_subdirectory(AmazonTemplate3Tests)


add_dependencies(AmazonTemplate3
AmazonTemplate3Lib
)
add_dependencies(AmazonTemplate3Tests
AmazonTemplate3Lib
)

