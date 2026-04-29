include_guard(GLOBAL)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Call setup_qcoro(COMPONENTS ...) AFTER find_package(Qt...) in each subproject.
# Compile and install QCoro:
#   git clone https://github.com/qcoro/qcoro.git && cd qcoro && mkdir build && cd build
#   cmake .. -DCMAKE_PREFIX_PATH=/home/cedric/Qt/6.8.3/gcc_64 -DUSE_QT_VERSION=6 \
#            -DCMAKE_BUILD_TYPE=Release
#   make && sudo make install
macro(setup_qcoro)
    set(Qt_DIR_HINT "${Qt${QT_VERSION_MAJOR}_DIR}")
    message("Qt hints: ${Qt_DIR_HINT}")
    set(QCoro_DIR_HINT_1 "/usr/local/lib/cmake/QCoro6")
    set(QCoro_DIR_HINT_2 "/usr/lib/cmake/QCoro6")
    set(QCoro_DIR_HINT_3 "${Qt_DIR_HINT}/../../../../lib/cmake/QCoro6")
    set(QCoro_DIR_HINT_4 "${Qt_DIR_HINT}/../../../../lib/cmake/lib/cmake/QCoro6")
    set(QCoro_DIR_HINT_5 "${Qt_DIR_HINT}/../QCoro6")
    set(QCoro_DIR_HINT_6 "${Qt_DIR_HINT}/../lib/cmake/QCoro6")
    message("QCoro hints: ${QCoro_DIR_HINT_1} ; ${QCoro_DIR_HINT_2} ; ${QCoro_DIR_HINT_3} ; ${QCoro_DIR_HINT_4} ; ${QCoro_DIR_HINT_5} ; ${QCoro_DIR_HINT_6}")
    list(APPEND CMAKE_PREFIX_PATH "${Qt_DIR_HINT}/../../../../lib/cmake")
    list(APPEND CMAKE_PREFIX_PATH "${Qt_DIR_HINT}/..")
    find_package(QCoro6 REQUIRED ${ARGN}
        HINTS
            "${QCoro_DIR_HINT_1}"
            "${QCoro_DIR_HINT_2}"
            "${QCoro_DIR_HINT_3}"
            "${QCoro_DIR_HINT_4}"
            "${QCoro_DIR_HINT_5}"
            "${QCoro_DIR_HINT_6}"
    )
endmacro()

