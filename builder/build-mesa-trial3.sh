#!/bin/bash

log_e() {
  echo "[ERROR] $*"
}

log_d() {
  echo "[Debug] $*"
}

cmd_run() {
  log_d "Execute: $*"
  "$@"
}

build_env() {
  export WS="$HOME/02_dev/algae-ws"
  export TOP="$WS/algae-bp"

  # Source tree
  export SRC="$WS"

  # Build output
  export BUILD="$WS/build"

  # Host LLVM installation
  export LLVM_HOST="$TOP/tool/llvm-host"

  # Target development/root filesystem:
  # libdrm, Mesa, LLVM, etc. will eventually be installed here.
  export BP_SYSROOT="$WS/build/sysroot-bp"

}

build_env_host() {
  true "placeholder"
}

build_env_cross() {
  export CROSS="$TOP/cross/aarch64-linux-gnu"

  # GCC's own sysroot: libc, loader, GCC runtime, etc.
  export GCC_SYSROOT="$CROSS/aarch64-linux-gnu/sysroot"

  # Cross tools
  export CC="$CROSS/bin/aarch64-linux-gnu-gcc"
  export CXX="$CROSS/bin/aarch64-linux-gnu-g++"
  export AR="$CROSS/bin/aarch64-linux-gnu-ar"
  export RANLIB="$CROSS/bin/aarch64-linux-gnu-ranlib"
  export STRIP="$CROSS/bin/aarch64-linux-gnu-strip"

  # Target pkg-config
  export PKG_CONFIG_SYSROOT_DIR="$BP_SYSROOT"
  export PKG_CONFIG_LIBDIR="$BP_SYSROOT/lib/pkgconfig"

  # Do not accidentally use host /usr/lib/pkgconfig packages.
  unset PKG_CONFIG_PATH

  # Target headers/libraries not belonging to GCC's sysroot.
  export CPPFLAGS="-I$BP_SYSROOT/include"
  export LDFLAGS="-L$BP_SYSROOT/lib"
}

inspect_fresh_test() {
  _lo_src=tmp/test.c
  _lo_tgt=tmp/test-aarch64

  cmd_run cat "$BP_SYSROOT/lib/pkgconfig/libdrm.pc"
  cmd_run file "$BP_SYSROOT/lib/libdrm.so"

  cat > $_lo_src <<'EOF'
#include <drm/drm.h>

int main(void)
{
    return 0;
}
EOF

  $CROSS/bin/aarch64-linux-gnu-gcc \
      --sysroot="$GCC_SYSROOT" \
      -I"$BP_SYSROOT/include" \
      -L"$BP_SYSROOT/lib" \
      $_lo_src \
      -ldrm \
      -o $_lo_tgt

  cmd_run file $_lo_tgt
  cmd_run eval "readelf -h \"$BP_SYSROOT/lib/libdrm.so\" | grep Machine"
  cmd_run eval "readelf -h $_lo_tgt | grep Machine"
  cmd_run eval "readelf -d $_lo_tgt | grep NEEDED"

  log_d "WS          = $WS"
  log_d "TOP         = $TOP"
  log_d "SRC         = $SRC"
  log_d "BUILD       = $BUILD"
  log_d "LLVM_HOST   = $LLVM_HOST"
  log_d "GCC_SYSROOT = $GCC_SYSROOT"
  log_d "BP_SYSROOT  = $BP_SYSROOT"
  log_d "CC=$CC"

  cmd_run $CC --version
  cmd_run $CC -print-sysroot

  cmd_run pkg-config --cflags --libs libdrm
  cmd_run pkg-config --modversion libdrm
  cmd_run pkg-config --variable=prefix libdrm

  cmd_run $CC -print-file-name=libstdc++.so
  cmd_run find "$CROSS" -name 'libstdc++.so*' -type f -o -type l
}

build_host_llvm() {
  mkdir -p "$LLVM_HOST"
  mkdir -p "$BUILD/llvm-host-build"

  . .venv/bin/activate \
    && cmd_run cmake -G Ninja \
        -S $SRC/llvm-project/llvm \
        -B "$BUILD/llvm-host-build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$LLVM_HOST" \
        -DCMAKE_C_COMPILER=/usr/bin/gcc \
        -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
        -DLLVM_TARGETS_TO_BUILD="X86;AArch64" \
        -DLLVM_ENABLE_PROJECTS="clang" \
        -DLLVM_INCLUDE_TESTS=OFF \
        -DLLVM_INCLUDE_EXAMPLES=OFF \
        -DLLVM_INCLUDE_BENCHMARKS=OFF \
        -DLLVM_ENABLE_ASSERTIONS=OFF \
        -DLLVM_ENABLE_DUMP=ON
  . .venv/bin/activate \
    && cmd_run ninja -C "$BUILD/llvm-host-build" \
    && cmd_run ninja -C "$BUILD/llvm-host-build" install
}

host_llvm_test() {
  cmd_run file "$BUILD/llvm-host-build/bin/llvm-min-tblgen"
  cmd_run file "$BUILD/llvm-host-build/bin/llvm-tblgen"
  cmd_run file "$BUILD/llvm-host-build/bin/clang"
  cmd_run "$BUILD/llvm-host-build/bin/llvm-config" --version

  cmd_run file "$LLVM_HOST/bin/llvm-config"
  cmd_run file "$LLVM_HOST/bin/clang"
}

host_llvm_install() {
  . .venv/bin/activate \
    && cmd_run ninja -C "$BUILD/llvm-host-build" install

  cmd_run file "$LLVM_HOST/bin/llvm-config"
  cmd_run file "$LLVM_HOST/bin/llvm-tblgen"
  cmd_run "$LLVM_HOST/bin/llvm-config" --version
}

inspect_cross_test() {
  cmd_run $CC -dumpmachine
  cmd_run eval "find \"$LLVM_HOST/bin\" -maxdepth 1 -type f \
      \( -name '*tblgen*' -o -name 'llvm-config' \) \
      -printf '%f\n' | sort"
  cmd_run "$LLVM_HOST/bin/llvm-config" --host-target
  cmd_run "$LLVM_HOST/bin/llvm-config" --targets-built
}

inspect_cross_build_cxx() {
  _lo_src=tmp/test-cxx.cpp
  _lo_tgt=tmp/test-cxx-aarch64
  cat >$_lo_src <<'EOF'
#include <iostream>

int main()
{
    std::cout << "hello\n";
    return 0;
}
EOF

  "$CXX" \
      --sysroot="$GCC_SYSROOT" \
      $_lo_src \
      -o $_lo_tgt

  cmd_run file $_lo_tgt

  cmd_run eval "readelf -d $_lo_tgt | grep NEEDED"

}

build_cross_llvm_defconfig_deprecate1() {
  mkdir -p $BUILD/llvm-aarch64-build

  . .venv/bin/activate \
    && cmake -G Ninja \
        -S "$SRC/llvm-project/llvm" \
        -B "$BUILD/llvm-aarch64-build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_C_COMPILER="$CC" \
        -DCMAKE_CXX_COMPILER="$CXX" \
        -DCMAKE_AR="$AR" \
        -DCMAKE_RANLIB="$RANLIB" \
        -DCMAKE_SYSROOT="$GCC_SYSROOT" \
        -DCMAKE_C_FLAGS="-I$BP_SYSROOT/include" \
        -DCMAKE_CXX_FLAGS="-I$BP_SYSROOT/include" \
        -DCMAKE_EXE_LINKER_FLAGS="-L$BP_SYSROOT/lib" \
        -DCMAKE_SHARED_LINKER_FLAGS="-L$BP_SYSROOT/lib" \
        -DCMAKE_FIND_ROOT_PATH="$BP_SYSROOT;$GCC_SYSROOT" \
        -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
        -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
        -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
        -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
        -DLLVM_NATIVE_TOOL_DIR="$LLVM_HOST/bin" \
        -DLLVM_HOST_TRIPLE=x86_64-unknown-linux-gnu \
        -DLLVM_DEFAULT_TARGET_TRIPLE=aarch64-linux-gnu \
        -DLLVM_TARGETS_TO_BUILD="AArch64" \
        -DLLVM_ENABLE_PROJECTS="clang;libclc" \
        -DLLVM_ENABLE_DUMP=ON \
        -DLLVM_INCLUDE_TESTS=OFF \
        -DLLVM_INCLUDE_EXAMPLES=OFF \
        -DLLVM_INCLUDE_BENCHMARKS=OFF \
        -DLLVM_ENABLE_ASSERTIONS=OFF
}

build_cross_llvm_defconfig() {
  mkdir -p $BUILD/llvm-aarch64-build

  . .venv/bin/activate \
    && cmake -G Ninja \
        -S "$SRC/llvm-project/llvm" \
        -B "$BUILD/llvm-aarch64-build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_C_COMPILER="$CC" \
        -DCMAKE_CXX_COMPILER="$CXX" \
        -DCMAKE_AR="$AR" \
        -DCMAKE_RANLIB="$RANLIB" \
        -DCMAKE_SYSROOT="$GCC_SYSROOT" \
        -DCMAKE_C_FLAGS="-I$BP_SYSROOT/include" \
        -DCMAKE_CXX_FLAGS="-I$BP_SYSROOT/include" \
        -DCMAKE_EXE_LINKER_FLAGS="-L$BP_SYSROOT/lib" \
        -DCMAKE_SHARED_LINKER_FLAGS="-L$BP_SYSROOT/lib" \
        -DCMAKE_FIND_ROOT_PATH="$BP_SYSROOT;$GCC_SYSROOT" \
        -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
        -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
        -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
        -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
        -DLLVM_NATIVE_TOOL_DIR="$LLVM_HOST/bin" \
        -DLLVM_HOST_TRIPLE=x86_64-unknown-linux-gnu \
        -DLLVM_DEFAULT_TARGET_TRIPLE=aarch64-linux-gnu \
        -DLLVM_TARGETS_TO_BUILD=AArch64 \
        -DLLVM_ENABLE_PROJECTS=clang \
        -DLLVM_ENABLE_DUMP=ON \
        -DCLANG_BUILD_TOOLS=OFF \
        -DCLANG_INCLUDE_DOCS=OFF \
        -DCLANG_INCLUDE_TESTS=OFF \
        -DLLVM_INCLUDE_TESTS=OFF \
        -DLLVM_INCLUDE_EXAMPLES=OFF \
        -DLLVM_INCLUDE_BENCHMARKS=OFF \
        -DLLVM_ENABLE_ASSERTIONS=OFF
}

inspect_cross_defconfig() {
  # cmd_run grep -E \
  #     'CMAKE_(C|CXX)_COMPILER:|CMAKE_SYSROOT:|LLVM_NATIVE_TOOL_DIR:|LLVM_DEFAULT_TARGET_TRIPLE:|LLVM_HOST_TRIPLE:|LLVM_TARGETS_TO_BUILD:' \
  #     "$BUILD/llvm-aarch64-build/CMakeCache.txt"
  # cmd_run eval "file \"$BUILD/llvm-aarch64-build/bin/llvm-tblgen\" 2>/dev/null || true"
  # cmd_run grep 'CMAKE_C_COMPILER:' \
  #     "$BUILD/llvm-aarch64-build/CMakeCache.txt"
  # cmd_run file "$LLVM_HOST/bin/llvm-tblgen"
  # cmd_run file "$LLVM_HOST/bin/clang-tblgen"

  # cmd_run eval "cmake -LAH \"$BUILD/llvm-aarch64-build\" | grep -i -E 'LIBCLC|CLANG|LLVM_ENABLE_DUMP|LLVM_NATIVE'"
  # cmd_run grep -R "LIBCLC" "$BUILD/llvm-aarch64-build/CMakeCache.txt"

  # cmd_run eval "grep -R \"LIBCLC_TARGETS_TO_BUILD\" \"$SRC/llvm-project/libclc\" \"$SRC/llvm-project/llvm\" | head -30"
  # cmd_run eval "find \"$SRC/llvm-project/libclc\" -maxdepth 2 -type f \( -name 'CMakeLists.txt' -o -name '*.cmake' \) -print"
  # cmd_run eval "grep -R \"spirv\" \"$SRC/llvm-project/libclc/CMakeLists.txt\" \"$SRC/llvm-project/libclc/cmake\" 2>/dev/null"

}

if [ -n "$1" ]; then
  $1
  log_d "done"
  exit
fi

build_env || { log_e "failed set env"; exit 1; }
# build_env_host || { log_e "failed set env for host build"; exit 1; }
# inspect_fresh_test || { log_e "failed inspect fresh cross"; exit 1; }
# build_host_llvm || { log_e "failed build host llvm"; exit 1; }
# host_llvm_test || { log_e "failed test host llvm"; exit 1; }
# host_llvm_install || { log_e "failed install host llvm"; exit 1; }
build_env_cross || { log_e "failed set env for cross build"; exit 1; }
# inspect_cross_build_cxx

# inspect_cross_test
# build_cross_llvm_defconfig
inspect_cross_defconfig

log_d "done"
exit

