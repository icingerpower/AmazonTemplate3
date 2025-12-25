cmake_minimum_required(VERSION 3.14)

project(AmazonTemplate3Lib LANGUAGES CXX)

include(../common.cmake)

include(../../common/openai/openai.cmake)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Network)

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

# Compile and install QCoro
# cd /home/cedric/code
# git clone https://github.com/qcoro/qcoro.git
# cd qcoro
# mkdir build
# cd build
# cmake .. -DCMAKE_PREFIX_PATH=/home/cedric/Qt/6.7.3/gcc_64 -DUSE_QT_VERSION=6 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/home/cedric/Qt/6.7.3/lib/cmake
# make
# This will install QCoro into /usr/local/ prefix, change it by passing -DCMAKE_INSTALL_PREFIX=/usr
# to the cmake command above.
# sudo make install
set(QCoro_DIR_HINT_1 "/usr/local/lib/cmake/QCoro6")
set(QCoro_DIR_HINT_2 "/usr/lib/cmake/QCoro6")
set(QCoro_DIR_HINT_3 "${Qt_DIR_HINT}/../../../../lib/cmake/QCoro6") # only if you installed QCoro into the same Qt prefix
message("QCoro hints: ${QCoro_DIR_HINT_1} ; ${QCoro_DIR_HINT_2} ; ${QCoro_DIR_HINT_3}")

find_package(QCoro6 REQUIRED COMPONENTS Network
    HINTS
        "${QCoro_DIR_HINT_1}"
        "${QCoro_DIR_HINT_2}"
        "${QCoro_DIR_HINT_3}"
)

add_library(AmazonTemplate3Lib STATIC
  AmazonTemplate3Lib.cpp
  AmazonTemplate3Lib.h
  ${OPENAI_FILES}
  Attribute.h
  Attribute.cpp
  TemplateFiller.h
  TemplateFiller.cpp
  MandatoryAttributesManager.h
  MandatoryAttributesManager.cpp
)

target_link_libraries(AmazonTemplate3Lib PRIVATE
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Core
    QXlsx::QXlsx
    QCoro::Network
)

target_compile_definitions(AmazonTemplate3Lib PRIVATE AMAZONTEMPLATE3LIB_LIBRARY)
