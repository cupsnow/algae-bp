#!/bin/bash

# LLVM 24.0.0

_lo_free_mem=$(free -g | awk '/^Mem:/ {print $7}')
if [ "$_lo_free_mem" -ge 20 ]; then
  _pri_memory_max=15G
  _pri_cpu_quota=1400%
  _pri_parallel="15"
elif [ "$_lo_free_mem" -ge 8 ]; then
  _pri_memory_max=4G
  _pri_cpu_quota=480%
  _pri_parallel="4"
else 
  _pri_memory_max=2G
  _pri_cpu_quota=190%
  _pri_parallel="2"
fi

_pri_runner="systemd-run --user --scope \
  ${_pri_memory_max:+-p MemoryMax=$_pri_memory_max} \
  ${_pri_cpu_quota:+-p CPUQuota=$_pri_cpu_quota}"

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
        -DCMAKE_SYSROOT="$GCC_SYSROOT" \
        \
        -DCMAKE_C_FLAGS="-I$BP_SYSROOT/include" \
        -DCMAKE_CXX_FLAGS="-I$BP_SYSROOT/include" \
        -DCMAKE_EXE_LINKER_FLAGS="-L$BP_SYSROOT/lib -Wl,-rpath-link,$BP_SYSROOT/lib  -Wl,-rpath-link,$CROSS/aarch64-linux-gnu/lib64" \
        -DCMAKE_SHARED_LINKER_FLAGS="-L$BP_SYSROOT/lib -Wl,-rpath-link,$BP_SYSROOT/lib  -Wl,-rpath-link,$CROSS/aarch64-linux-gnu/lib64" \
        \
        -DCMAKE_FIND_ROOT_PATH="$BP_SYSROOT;$GCC_SYSROOT" \
        -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
        -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
        -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
        -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
        \
        -DLLVM_USE_HOST_TOOLS=ON \
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
    cmd_run realpath $BUILD/llvm-aarch64-build/bin/clang

    cmd_run eval "file \$(realpath $BUILD/llvm-aarch64-build/bin/clang) | grep \"ELF 64-bit LSB executable, ARM aarch64\" >/dev/null 2>&1" || {
      log_e "Failed to check llvm aarch64"
      return 1
    }
  else
    log_e "\$BUILD/llvm-aarch64-build/bin/clang not found"
  fi

  if [ -f "$BUILD/llvm-aarch64-build/lib/libLLVM.so" ]; then
    cmd_run realpath $BUILD/llvm-aarch64-build/lib/libLLVM.so

    cmd_run eval "file \$(realpath $BUILD/llvm-aarch64-build/lib/libLLVM.so) | grep \"ELF 64-bit LSB shared object, ARM aarch64\" >/dev/null 2>&1" || {
      log_e "Failed to check llvm aarch64"
      return 1
    }
  else
    log_e "\$BUILD/llvm-aarch64-build/lib/libLLVM.so not found"
  fi
}

llvm_aarch64_install() {

  cmd_run rm -rf "$BUILD/llvm-aarch64-staging"
  cmd_run mkdir -p "$BUILD/llvm-aarch64-staging"

  . .venv/bin/activate \
    && cmd_run env DESTDIR="$BUILD/llvm-aarch64-staging" \
        ninja -C "$BUILD/llvm-aarch64-build" install
}

mesa_aarch64_cross_file() {
  _lo_crossfile="${1:-$BUILD/mesa_aarch64.cmake}"
  cmd_run eval "mkdir -p \$(dirname \"$_lo_crossfile\")"

  cat > "$_lo_crossfile" <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER "$CROSS/bin/aarch64-linux-gnu-gcc")
set(CMAKE_CXX_COMPILER "$CROSS/bin/aarch64-linux-gnu-g++")
set(CMAKE_AR "$CROSS/bin/aarch64-linux-gnu-ar")
set(CMAKE_RANLIB "$CROSS/bin/aarch64-linux-gnu-ranlib")
set(CMAKE_STRIP "$CROSS/bin/aarch64-linux-gnu-strip")

set(CMAKE_SYSROOT "$GCC_SYSROOT")

set(CMAKE_C_FLAGS_INIT
    "--sysroot=$GCC_SYSROOT")
set(CMAKE_CXX_FLAGS_INIT
    "--sysroot=$GCC_SYSROOT")

set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-L$BP_SYSROOT/lib -L$CROSS/aarch64-linux-gnu/lib64 -Wl,-rpath-link,$BP_SYSROOT/lib -Wl,-rpath-link,$CROSS/aarch64-linux-gnu/lib64")

set(CMAKE_SHARED_LINKER_FLAGS_INIT
    "-L$BP_SYSROOT/lib -L$CROSS/aarch64-linux-gnu/lib64 -Wl,-rpath-link,$BP_SYSROOT/lib -Wl,-rpath-link,$CROSS/aarch64-linux-gnu/lib64")

set(CMAKE_FIND_ROOT_PATH
    "$BP_SYSROOT"
    "$GCC_SYSROOT"
)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF

  cmd_run cat "$_lo_crossfile"
}

spirvtools_aarch64_defconfig() {
  _lo_crossfile="$BUILD/spirv-tools_aarch64.cmake"

  [ -f "$_lo_crossfile" ] || mesa_aarch64_cross_file "$_lo_crossfile" || {
    log_e "Failed to generate aarch64 cross file"
    return 1
  }

  rm -rf "$BUILD/spirv-tools-aarch64-build"

  mkdir -p "$BUILD/spirv-tools-aarch64-build"

  . .venv/bin/activate \
    && cmake -S "$SRC/spirv-tools" \
        -B "$BUILD/spirv-tools-aarch64-build" \
        -G Ninja \
        ${_lo_crossfile:+-DCMAKE_TOOLCHAIN_FILE="$_lo_crossfile"} \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_INSTALL_LIBDIR=lib \
        -DSPIRV_SKIP_TESTS=ON \
        -DSPIRV_WERROR=OFF
}

spirvtools_aarch64_build() {
  . .venv/bin/activate \
    && cmd_run $_pri_runner ninja -C "$BUILD/spirv-tools-aarch64-build"
}

spirvtools_aarch64_install() {
  mkdir -p "$BUILD/spirv-tools-aarch64-staging"
  . .venv/bin/activate \
    && cmd_run env DESTDIR="$BUILD/spirv-tools-aarch64-staging" \
      ninja -C "$BUILD/spirv-tools-aarch64-build" install
}

inspect() {
  echo '=== spirv-tools ==='
  cmd_run cd $WS/spirv-tools
  cmd_run git status --short
  cmd_run git log --oneline -n1
  cmd_run git -C external/spirv-headers log --oneline -n1

  cmd_run cd "$SRC/spirv-llvm-translator"

  echo '=== Translator ==='
  cmd_run git log --oneline -n3

  echo
  echo '=== LLVM references ==='
  cmd_run eval "grep -RniE \
      'LLVM_VERSION|LLVM.*24|llvm_release|SPIRV-Headers|SPIRV_TOOLS' \
      --include='CMakeLists.txt' \
      --include='*.cmake' \
      --include='*.conf' \
      . 2>/dev/null | head -100"

echo
echo '=== SPIRV-Tools staging ==='
cmd_run eval "find \"$BUILD/spirv-tools-aarch64-staging/usr\" \
    -maxdepth 3 -type f | sort"

echo
echo '=== ELF check ==='
find "$BUILD/spirv-tools-aarch64-staging/usr/bin" \
    -type f -executable -print 2>/dev/null |
while read f; do
    printf '%-80s ' "$f"
    file "$f" | sed 's/.*: //'
done

echo
echo '=== find .pc ==='
cmd_run eval "find \"$BUILD/spirv-tools-aarch64-staging/usr\" \
    -name '*.pc' -print"

echo
echo '=== grep function   ==='
cmd_run eval "grep -RniE \
    'SPIRV_TOOLS.*(VERSION|LIBRARY|INCLUDE)|SPIRV-ToolsConfig' \
    \"$BUILD/spirv-tools-aarch64-staging/usr\" \
    2>/dev/null | head -50"


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

show_help() {
  cat <<EOHELP
Usage: $(basename $0) <COMMAND>

COMMAND:
  llvm_host_defconfig
  llvm_host_build
  llvm_host_install
  llvm_aarch64_defconfig
  llvm_aarch64_build
  llvm_aarch64_install

EOHELP
}