cmake_minimum_required([[VERSION]] [[3.10]])
project([[cmake01]] [[VERSION]] [[1.0.0]] [[LANGUAGES]] [[CXX]])
set([[CMAKE_CXX_STANDARD_17]])
set([[CMAKE_CXX_STANDARD_REQUIRED]] [[True]])

include_directories([[include]])
add_library(libcmake01 STATIC SHARED utils.cpp utils.h)

add_executable(cmake01 main.cpp utils.cpp utils.h)
# target_link_libries(cmake01 libcmake01)
