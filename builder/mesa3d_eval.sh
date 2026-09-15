#!/bin/bash

_pri_runner="systemd-run --user --scope -p MemoryMax=4G -p CPUQuota=500%"
_pri_parallel="3"

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
  log_d "Execute $*"
  "$@"
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

  # TARGET=aarch64-linux-gnu
  # TARGET="$(printf "%s" $(EXEC_CMD="\$(CC) -dumpmachine" make exec))"
  # SYSROOT=/home/joelai/02_dev/algae-ws/build/sysroot-qemuarm64
  # SYSROOT="$(printf "%s" $(make print_BUILD_SYSROOT))"

  # export TARGET
  # export SYSROOT

}

# Test for cross compile, libdrm, sysroot
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

  _lo_src="tmp/test.c"
  _lo_out="tmp/test.o"
  cat > $_lo_src <<'EOF'
#include <drm/drm.h>

int main(void)
{
    return 0;
}
EOF

  $CC \
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
}

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
}

setenv_base

if [ "$1" == "llvm_host_defconfig" ] \
    || [ "$1" == "llvm_host_build" ]; then
  setenv_host
  "$@"
  exit
fi

setenv_cross

[ -n "$1" ] && {
  cmd_run "$@"
  exit
}
