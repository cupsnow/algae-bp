#!/bin/bash

# LLVM 24.0.0

_pri_runner="systemd-run --user --scope -p MemoryMax=4G -p CPUQuota=480%"
_pri_parallel="4"

log_ts() {
  date "+%y%m%d %H:%M:%S"
}

log_d() {
  _lo_ts="$(log_ts)"
  echo "${_lo_ts:+[${_lo_ts}]}[Debug] $*"
}

log_e() {
  _lo_ts="$(log_ts)"
  echo "${_lo_ts:+[${_lo_ts}]}[Debug] $*"
}

cmd_run() {
  # echo "================================"
  log_d "Execute: $*"
  echo ""
  "$@"
  _lo_ret=$?
  echo ""
  log_d "Exit code $_lo_ret ($1 $2 $3 ...)"
  echo ""
  return $_lo_ret
}

setenv_base() {
  WS=$HOME/02_dev/algae-ws
  TOP=$WS/algae-bp
  CROSS=$TOP/cross/aarch64-linux-gnu
  GCC_SYSROOT=$CROSS/aarch64-linux-gnu/sysroot
  BP_SYSROOT=$WS/build/sysroot-qemuarm64
  LLVM_HOST="$TOP/tool/llvm-host"
  BUILD=$WS/build
  SRC=$WS

  export WS
  export TOP
  export CROSS
  export GCC_SYSROOT
  export BP_SYSROOT
  export BUILD
  export SRC
  export LLVM_HOST
}

setenv_host() {
  unset CROSS_COMPILE

  CC="${CROSS_COMPILE}gcc"
  CXX="${CROSS_COMPILE}g++"
  AR="${CROSS_COMPILE}ar"
  RANLIB="${CROSS_COMPILE}ranlib"
  STRIP="${CROSS_COMPILE}strip"

  export CC
  export CXX
  export AR
  export RANLIB
  export STRIP

  unset PKG_CONFIG_SYSROOT_DIR
  unset PKG_CONFIG_LIBDIR
}

setenv_cross() {
  CROSS_COMPILE=$CROSS/bin/aarch64-linux-gnu-
  CC="${CROSS_COMPILE}gcc"
  CXX="${CROSS_COMPILE}g++"
  AR="${CROSS_COMPILE}ar"
  RANLIB="${CROSS_COMPILE}ranlib"
  STRIP="${CROSS_COMPILE}strip"

  export CC
  export CXX
  export AR
  export RANLIB
  export STRIP

  PKG_CONFIG_SYSROOT_DIR="$BP_SYSROOT"
  PKG_CONFIG_LIBDIR="$BP_SYSROOT/lib/pkgconfig"

  export PKG_CONFIG_SYSROOT_DIR
  export PKG_CONFIG_LIBDIR

  unset PKG_CONFIG_PATH
}

# Test for cross cxx, libdrm, sysroot
test_cross_with_libdrm() {
  _lo_libdrm_so="$(realpath $BP_SYSROOT/lib/libdrm.so)"

  cmd_run eval "file \"$_lo_libdrm_so\" | grep \"ELF 64-bit LSB shared object, ARM aarch64\" >/dev/null 2>&1" || {
    log_e "Failed check libdrm aarch64"
    return 1
  }

  cmd_run eval "readelf -h \"$BP_SYSROOT/lib/libdrm.so\" | grep \"Machine:\s*AArch64\" >/dev/null 2>&1" || {
    log_e "Failed check libdrm aarch64 machine"
    return 1
  }

  cmd_run eval "cat $BP_SYSROOT/lib/pkgconfig/libdrm.pc | grep \"prefix=\" >/dev/null 2>&1" || {
    log_e "Failed check libdrm pkgconfig"
    return 1
  }

  _lo_src="tmp/test.cpp"
  _lo_out="tmp/test.o"
  cat > $_lo_src <<'EOF'
#include <iostream>
#include <drm/drm.h>

int main(void)
{
    std::cout << "hello\n";
    return 0;
}
EOF

  $CXX \
    --sysroot="$GCC_SYSROOT" \
    -I"$BP_SYSROOT/include" \
    -L"$BP_SYSROOT/lib" \
    $_lo_src \
    -ldrm \
    -o $_lo_out

  cmd_run eval "file $_lo_out | grep \"ELF 64-bit LSB executable, ARM aarch64\" >/dev/null 2>&1" || {
    log_e "Failed to compile test program for cross-compilation"
    return 1
  }

  cmd_run eval "readelf -d $_lo_out | grep NEEDED"
}

# Generate llvm cross toolchain file
# not used yet
llvm_cross_file() {
  _lo_src="builder/llvm4-aarch64-toolchain.cmake"
  _lo_out="build/llvm-aarch64-toolchain.cmake"

  log_d "Generate llvm cross toolchain file"
  cat $_lo_src | sed \
    -e "s|\$\${GCC_SYSROOT}|${GCC_SYSROOT}|g" \
    -e "s|\$\${BP_SYSROOT}|${BP_SYSROOT}|g" \
    -e "s|\$\${CC}|${CROSS}/bin/aarch64-linux-gnu-gcc|g" \
    -e "s|\$\${CXX}|${CROSS}/bin/aarch64-linux-gnu-g++|g" >$_lo_out
}

llvm_host_defconfig() {
    [ -d "$BUILD/llvm-host-build" ] || cmd_run mkdir -p "$BUILD/llvm-host-build"
    . .venv/bin/activate \
      && cmd_run cmake -G Ninja \
          -S $SRC/llvm-project/llvm \
          -B "$BUILD/llvm-host-build" \
          -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_INSTALL_PREFIX="$LLVM_HOST" \
          -DLLVM_TARGETS_TO_BUILD="X86;AArch64" \
          -DLLVM_ENABLE_PROJECTS="clang" \
          -DLLVM_INCLUDE_TESTS=OFF \
          -DLLVM_INCLUDE_EXAMPLES=OFF \
          -DLLVM_INCLUDE_BENCHMARKS=OFF \
          -DLLVM_ENABLE_ASSERTIONS=OFF \
          -DLLVM_ENABLE_DUMP=ON || {
      log_e "Failed to configure llvm host build"
      return 1
    }
}

# build llvm host
# following hint to limit memory and cpu usage for execution
# systemd-run --user --scope -p MemoryMax=4G -p CPUQuota=500% ./builder/mesa3d_eval.sh llvm_host_build -j3
llvm_host_build() {
  [ -f "$BUILD/llvm-host-build/build.ninja" ] || llvm_host_defconfig || {
    log_e "Failed to configure llvm host build"
    return 1
  }

  . .venv/bin/activate \
    && cmd_run $_pri_runner ninja ${_pri_parallel:+-j$_pri_parallel} -C "$BUILD/llvm-host-build" || {
      log_e "Failed to build llvm host"
      return 1
    }

  # sanity check for expecting x86_64 executable
  cmd_run eval "file $BUILD/llvm-host-build/bin/llvm-config | grep \"ELF 64-bit LSB .*executable, x86-64\" >/dev/null 2>&1" || {
    log_e "Failed to check llvm host"
    return 1
  }

  cmd_run eval "file $BUILD/llvm-host-build/bin/llvm-tblgen | grep \"ELF 64-bit LSB .*executable, x86-64\" >/dev/null 2>&1" || {
    log_e "Failed to check llvm host"
    return 1
  }
}

llvm_host_install() {
  [ -e "$LLVM_HOST/bin/llvm-config" ] || llvm_host_build || {
    log_e "Failed to build llvm host"
    return 1
  }

  . .venv/bin/activate \
    && cmd_run ninja -C "$BUILD/llvm-host-build" install || {
      log_e "Failed to install llvm host"
      return 1
    }

  # sanity check for expecting x86_64 executable
  cmd_run eval "file $LLVM_HOST/bin/llvm-config | grep \"ELF 64-bit LSB .*executable, x86-64\" >/dev/null 2>&1" || {
    log_e "Failed to check llvm host"
    return 1
  }

  cmd_run eval "file $LLVM_HOST/bin/llvm-tblgen | grep \"ELF 64-bit LSB .*executable, x86-64\" >/dev/null 2>&1" || {
    log_e "Failed to check llvm host"
    return 1
  }

  # expect output the version and info
  cmd_run "$LLVM_HOST/bin/llvm-config" --version
  cmd_run "$LLVM_HOST/bin/llvm-config" --host-target
  cmd_run "$LLVM_HOST/bin/llvm-config" --targets-built
}

llvm_aarch64_defconfig() {

  [ -d "$BUILD/llvm-aarch64-build" ] || cmd_run mkdir -p "$BUILD/llvm-aarch64-build"
  . .venv/bin/activate \
    && cmd_run cmake -G Ninja \
        -S "$SRC/llvm-project/llvm" \
        -B "$BUILD/llvm-aarch64-build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        \
        -DCMAKE_C_COMPILER="$CC" \
        -DCMAKE_CXX_COMPILER="$CXX" \
        -DCMAKE_AR="$AR" \
        -DCMAKE_RANLIB="$RANLIB" \
        \
        -DCMAKE_SYSROOT="$GCC_SYSROOT" \
        -DCMAKE_C_FLAGS="-I$BP_SYSROOT/include" \
        -DCMAKE_CXX_FLAGS="-I$BP_SYSROOT/include" \
        -DCMAKE_EXE_LINKER_FLAGS="-L$BP_SYSROOT/lib" \
        -DCMAKE_SHARED_LINKER_FLAGS="-L$BP_SYSROOT/lib" \
        \
        -DCMAKE_FIND_ROOT_PATH="$BP_SYSROOT;$GCC_SYSROOT" \
        -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
        -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
        -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
        -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
        \
        -DLLVM_NATIVE_TOOL_DIR="$LLVM_HOST/bin" \
        -DLLVM_HOST_TRIPLE=x86_64-unknown-linux-gnu \
        -DLLVM_DEFAULT_TARGET_TRIPLE=aarch64-linux-gnu \
        -DLLVM_TARGETS_TO_BUILD=AArch64 \
        \
        -DLLVM_ENABLE_PROJECTS=clang \
        -DLLVM_ENABLE_DUMP=ON \
        \
        -DCLANG_BUILD_TOOLS=OFF \
        -DCLANG_INCLUDE_DOCS=OFF \
        -DCLANG_INCLUDE_TESTS=OFF \
        \
        -DLLVM_INCLUDE_TESTS=OFF \
        -DLLVM_INCLUDE_EXAMPLES=OFF \
        -DLLVM_INCLUDE_BENCHMARKS=OFF \
        -DLLVM_ENABLE_ASSERTIONS=OFF || {
    log_e "Failed to configure llvm aarch64 build"
    return 1
  }

  # expect
  # CMAKE_C_COMPILER:STRING=.../aarch64-linux-gnu-gcc
  # CMAKE_CXX_COMPILER:STRING=.../aarch64-linux-gnu-g++
  # CMAKE_SYSROOT=.../aarch64-linux-gnu/sysroot
  # LLVM_ENABLE_PROJECTS:STRING=clang
  # LLVM_TARGETS_TO_BUILD:STRING=AArch64
  # LLVM_DEFAULT_TARGET_TRIPLE:STRING=aarch64-linux-gnu
  # LLVM_HOST_TRIPLE:STRING=x86_64-unknown-linux-gnu
  # LLVM_NATIVE_TOOL_DIR:PATH=.../tool/llvm-host/bin
  cmd_run grep -E \
      'LLVM_ENABLE_PROJECTS:|LLVM_ENABLE_RUNTIMES:|LLVM_TARGETS_TO_BUILD:|LLVM_DEFAULT_TARGET_TRIPLE:|LLVM_HOST_TRIPLE:|LLVM_NATIVE_TOOL_DIR:|CMAKE_C_COMPILER:|CMAKE_CXX_COMPILER:|CMAKE_SYSROOT:' \
      "$BUILD/llvm-aarch64-build/CMakeCache.txt"
}

llvm_aarch64_defconfig2() {

  [ -d "$BUILD/llvm-aarch64-build" ] || cmd_run mkdir -p "$BUILD/llvm-aarch64-build"
  . .venv/bin/activate \
    && cmd_run cmake -G Ninja \
        -S "$SRC/llvm-project/llvm" \
        -B "$BUILD/llvm-aarch64-build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        \
        -DCMAKE_C_COMPILER="$CC" \
        -DCMAKE_CXX_COMPILER="$CXX" \
        -DCMAKE_AR="$AR" \
        -DCMAKE_RANLIB="$RANLIB" \
        \
        -DCMAKE_SYSROOT="$GCC_SYSROOT" \
        -DCMAKE_C_FLAGS="-I$BP_SYSROOT/include" \
        -DCMAKE_CXX_FLAGS="-I$BP_SYSROOT/include" \
        -DCMAKE_EXE_LINKER_FLAGS="-L$BP_SYSROOT/lib" \
        -DCMAKE_SHARED_LINKER_FLAGS="-L$BP_SYSROOT/lib" \
        \
        -DCMAKE_FIND_ROOT_PATH="$BP_SYSROOT;$GCC_SYSROOT" \
        -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
        -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
        -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
        -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
        \
        -DLLVM_NATIVE_TOOL_DIR="$LLVM_HOST/bin" \
        -DLLVM_HOST_TRIPLE=x86_64-unknown-linux-gnu \
        -DLLVM_DEFAULT_TARGET_TRIPLE=aarch64-linux-gnu \
        -DLLVM_TARGETS_TO_BUILD=AArch64 \
        \
        -DLLVM_ENABLE_PROJECTS=clang \
        -DLLVM_ENABLE_DUMP=ON \
        \
        -DLLVM_BUILD_LLVM_DYLIB=ON \
        -DLLVM_LINK_LLVM_DYLIB=ON \
        \
        -DCLANG_BUILD_TOOLS=OFF \
        -DCLANG_INCLUDE_DOCS=OFF \
        -DCLANG_INCLUDE_TESTS=OFF \
        \
        -DLLVM_INCLUDE_TESTS=OFF \
        -DLLVM_INCLUDE_EXAMPLES=OFF \
        -DLLVM_INCLUDE_BENCHMARKS=OFF \
        -DLLVM_ENABLE_ASSERTIONS=OFF || {
    log_e "Failed to configure llvm aarch64 build"
    return 1
  }

  # expect
  # CMAKE_C_COMPILER:STRING=.../aarch64-linux-gnu-gcc
  # CMAKE_CXX_COMPILER:STRING=.../aarch64-linux-gnu-g++
  # CMAKE_SYSROOT=.../aarch64-linux-gnu/sysroot
  # LLVM_ENABLE_PROJECTS:STRING=clang
  # LLVM_TARGETS_TO_BUILD:STRING=AArch64
  # LLVM_DEFAULT_TARGET_TRIPLE:STRING=aarch64-linux-gnu
  # LLVM_HOST_TRIPLE:STRING=x86_64-unknown-linux-gnu
  # LLVM_NATIVE_TOOL_DIR:PATH=.../tool/llvm-host/bin
  cmd_run grep -E \
      'LLVM_ENABLE_PROJECTS:|LLVM_ENABLE_RUNTIMES:|LLVM_TARGETS_TO_BUILD:|LLVM_DEFAULT_TARGET_TRIPLE:|LLVM_HOST_TRIPLE:|LLVM_NATIVE_TOOL_DIR:|CMAKE_C_COMPILER:|CMAKE_CXX_COMPILER:|CMAKE_SYSROOT:' \
      "$BUILD/llvm-aarch64-build/CMakeCache.txt"

  # expect
  # LLVM_BUILD_LLVM_DYLIB:BOOL=ON
  # LLVM_LINK_LLVM_DYLIB:BOOL=ON
  cmd_run grep -E \
      'LLVM_BUILD_LLVM_DYLIB:|LLVM_LINK_LLVM_DYLIB:' \
      "$BUILD/llvm-aarch64-build/CMakeCache.txt"
}

llvm_aarch64_defconfig3() {

  [ -d "$BUILD/llvm-aarch64-build" ] || cmd_run mkdir -p "$BUILD/llvm-aarch64-build"
  . .venv/bin/activate \
    && cmd_run cmake -G Ninja \
        -S "$SRC/llvm-project/llvm" \
        -B "$BUILD/llvm-aarch64-build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        \
        -DCMAKE_C_COMPILER="$CC" \
        -DCMAKE_CXX_COMPILER="$CXX" \
        -DCMAKE_AR="$AR" \
        -DCMAKE_RANLIB="$RANLIB" \
        -DCMAKE_SYSROOT="$GCC_SYSROOT" \
        \
        -DCMAKE_C_FLAGS="-I$BP_SYSROOT/include" \
        -DCMAKE_CXX_FLAGS="-I$BP_SYSROOT/include" \
        -DCMAKE_EXE_LINKER_FLAGS="-L$BP_SYSROOT/lib" \
        -DCMAKE_SHARED_LINKER_FLAGS="-L$BP_SYSROOT/lib" \
        \
        -DCMAKE_FIND_ROOT_PATH="$BP_SYSROOT;$GCC_SYSROOT" \
        -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
        -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
        -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
        -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
        \
        -DLLVM_NATIVE_TOOL_DIR="$LLVM_HOST/bin" \
        -DLLVM_TABLEGEN="$LLVM_HOST/bin/llvm-tblgen" \
        \
        -DLLVM_HOST_TRIPLE=x86_64-unknown-linux-gnu \
        -DLLVM_DEFAULT_TARGET_TRIPLE=aarch64-linux-gnu \
        -DLLVM_TARGETS_TO_BUILD=AArch64 \
        \
        -DLLVM_ENABLE_PROJECTS=clang \
        \
        -DLLVM_BUILD_LLVM_DYLIB=ON \
        -DLLVM_LINK_LLVM_DYLIB=ON \
        -DLLVM_ENABLE_DUMP=ON \
        \
        -DCLANG_TOOL_DRIVER_BUILD=ON \
        -DCLANG_TOOL_LIBCLANG_BUILD=ON \
        \
        -DLLVM_INCLUDE_TESTS=OFF \
        -DLLVM_INCLUDE_EXAMPLES=OFF \
        -DLLVM_INCLUDE_BENCHMARKS=OFF \
        -DLLVM_ENABLE_ASSERTIONS=OFF

  # expect
  # LLVM_NATIVE_TOOL_DIR:PATH=.../tool/llvm-host/bin
  # LLVM_TABLEGEN:STRING=.../tool/llvm-host/bin/llvm-tblgen

  # LLVM_BUILD_LLVM_DYLIB:BOOL=ON
  # LLVM_LINK_LLVM_DYLIB:BOOL=ON

  # CLANG_TOOL_DRIVER_BUILD:BOOL=ON
  # CLANG_TOOL_LIBCLANG_BUILD:BOOL=ON

  # LLVM_TARGETS_TO_BUILD:STRING=AArch64
  # LLVM_DEFAULT_TARGET_TRIPLE:STRING=aarch64-linux-gnu
  cmd_run grep -E \
      'LLVM_NATIVE_TOOL_DIR|LLVM_TABLEGEN|CLANG_TABLEGEN|LLVM_HOST_TRIPLE|LLVM_DEFAULT_TARGET_TRIPLE|LLVM_TARGETS_TO_BUILD|LLVM_BUILD_LLVM_DYLIB|LLVM_LINK_LLVM_DYLIB|CLANG_TOOL_DRIVER_BUILD|CLANG_TOOL_LIBCLANG_BUILD' \
      "$BUILD/llvm-aarch64-build/CMakeCache.txt"

  cmd_run eval "ninja -C \"$BUILD/llvm-aarch64-build\" -t targets all \
      | grep -E 'llvm-min-tblgen|llvm-tblgen|clang-tblgen'"

  cmd_run eval "ninja -C \"$BUILD/llvm-aarch64-build\" -t targets all \
      | grep -E 'LLVM.*(dylib|Dylib)|libLLVM'"

  cmd_run find "$BUILD/llvm-aarch64-build" \
      -type f \
      \( -name '*tblgen*' -o -name '*LLVM*.so*' \) \
      -print
}

llvm_aarch64_build() {
  [ -f "$BUILD/llvm-aarch64-build/build.ninja" ] || llvm_aarch64_defconfig || {
    log_e "Failed to configure llvm aarch64 build"
    return 1
  }

  . .venv/bin/activate \
    && cmd_run $_pri_runner ninja ${_pri_parallel:+-j$_pri_parallel} -C "$BUILD/llvm-aarch64-build" || {
      log_e "Failed to build llvm aarch64"
      return 1
    }

  if [ -f "$BUILD/llvm-aarch64-build/bin/clang" ]; then
    cmd_run eval "file \"$BUILD/llvm-aarch64-build/bin/clang\" | grep \"ELF 64-bit LSB executable, ARM aarch64\" >/dev/null 2>&1" || {
      log_e "Failed to check llvm aarch64"
      return 1
    }
  else
    log_e "\$BUILD/llvm-aarch64-build/bin/clang not found"
  fi

  if [ -f "$BUILD/llvm-aarch64-build/lib/libLLVM.so" ]; then
    cmd_run eval "file \"$BUILD/llvm-aarch64-build/lib/libLLVM.so\" | grep \"ELF 64-bit LSB shared object, ARM aarch64\" >/dev/null 2>&1" || {
      log_e "Failed to check llvm aarch64"
      return 1
    }
  else
    log_e "\$BUILD/llvm-aarch64-build/lib/libLLVM.so not found"
  fi
}

llvm_aarch64_build_inspect2() {
  if [ -f "$BUILD/llvm-aarch64-build/bin/clang" ]; then
    cmd_run eval "file \"$BUILD/llvm-aarch64-build/bin/clang\" | grep \"ELF 64-bit LSB executable, ARM aarch64\" >/dev/null 2>&1" || {
      log_e "Failed to check llvm aarch64"
      return 1
    }
  else
    log_e "\$BUILD/llvm-aarch64-build/bin/clang not found"
  fi

  if [ -f "$BUILD/llvm-aarch64-build/lib/libLLVM.so" ]; then
    cmd_run eval "file \"$BUILD/llvm-aarch64-build/lib/libLLVM.so\" | grep \"ELF 64-bit LSB shared object, ARM aarch64\" >/dev/null 2>&1" || {
      log_e "Failed to check llvm aarch64"
      return 1
    }
  else
    log_e "\$BUILD/llvm-aarch64-build/lib/libLLVM.so not found"
  fi
}

llvm_aarch64_build_inspect() {
  echo "=== build/bin ==="
  find "$BUILD/llvm-aarch64-build/bin" -maxdepth 1 -type f -printf '%f\n' 2>/dev/null | sort | head -50

  echo
  echo "=== LLVM libraries ==="
  find "$BUILD/llvm-aarch64-build" -type f \
      \( -name 'libLLVM*.so*' -o -name 'libLLVM*.a' -o -name 'libclang*.so*' -o -name 'libclang*.a' \) \
      | head -50

  echo
  echo "=== clang ==="
  find "$BUILD/llvm-aarch64-build" -type f \
      \( -name 'clang' -o -name 'clang-*' \) \
      | head -30

  echo
  echo "=== important targets ==="
  ninja -C "$BUILD/llvm-aarch64-build" -t targets all 2>/dev/null \
      | grep -E '(^|/)(clang|LLVM|libclang|llvm-config)' \
      | head -80


  echo
  cmd_run grep -E \
    'LLVM_ENABLE_PROJECTS:|LLVM_BUILD_LLVM_DYLIB:|LLVM_LINK_LLVM_DYLIB:|LLVM_BUILD_TOOLS:|LLVM_ENABLE_RUNTIMES:' \
    "$BUILD/llvm-aarch64-build/CMakeCache.txt"

  cmd_run file \
      "$BUILD/llvm-aarch64-build/lib/libclang-cpp.so.24.0git" \
      "$BUILD/llvm-aarch64-build/lib/libLLVMCore.a" \
      "$BUILD/llvm-aarch64-build/bin/llvm-config"

  cmd_run file "$BUILD/llvm-aarch64-build/bin/llvm-ar"
  cmd_run file "$BUILD/llvm-aarch64-build/bin/llc"
  cmd_run file "$BUILD/llvm-aarch64-build/bin/llvm-config"

  cmd_run file "$BUILD/llvm-aarch64-build/lib/libLLVM.so"
  cmd_run readelf -d "$BUILD/llvm-aarch64-build/lib/libLLVM.so" \
      | grep NEEDED
}

inspect() {

  cmd_run eval "grep -R \"llvm-min-tblgen\" \
      \"$BUILD/llvm-aarch64-build\" \
      --exclude='*.o' --exclude='*.a' --exclude='*.so' \
      2>/dev/null | head -50"

  cmd_run ls -lh "$LLVM_HOST/bin/"*tblgen*

  cmd_run eval "grep -E \
      'LLVM_(TABLEGEN|MIN_TABLEGEN|NATIVE_TOOL|HOST_TOOL)|CLANG_TABLEGEN' \
      \"$BUILD/llvm-aarch64-build/CMakeCache.txt\""

  cmd_run eval "ninja -C \"$BUILD/llvm-aarch64-build\" -t targets all \
      | grep -i tblgen"

}

mini_build() {
  cmd_run ninja -C "$BUILD/llvm-aarch64-build" llvm-min-tblgen
}


setenv_base

if [ "$1" = "llvm_host_defconfig" ] \
    || [ "$1" = "llvm_host_build" ] \
    || [ "$1" = "llvm_host_install" ]; then
  setenv_host
  "$@"
  exit
fi

setenv_cross

[ -n "$1" ] && {
  cmd_run "$@"
  exit
}
