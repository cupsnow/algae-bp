# the name of the target operating system
set(CMAKE_SYSTEM_NAME Linux)

set(CMAKE_SYSROOT "${BUILD_SYSROOT}")

# which compilers to use for C and C++
set(CMAKE_C_COMPILER ${AARCH64_CROSS_COMPILE}gcc)
set(CMAKE_CXX_COMPILER ${AARCH64_CROSS_COMPILE}g++)

# where is the target environment located
set(CMAKE_FIND_ROOT_PATH "${BUILD_ROOTPATH}")

# prevent find_program() search CMAKE_FIND_ROOT_PATH which is for target runtime
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# search headers and libraries for the target environment
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
