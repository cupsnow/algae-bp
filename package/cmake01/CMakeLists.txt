# Set the minimum required version of CMake
cmake_minimum_required(VERSION 3.10)

# Project name
project(cm01 VERSION 1.0 LANGUAGES CXX)

# Enable C++11 or a later standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Include directories (if any)
include_directories(include)

add_library(libcm01 SHARED STATIC utils.cpp utils.h)

# Define the executable and specify source files
add_executable(cm01 main.cpp utils.cpp utils.h)

# Optionally, link against libraries if needed
target_link_libraries(cm01 libcm01)
