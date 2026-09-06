#!/bin/bash
#
# Wrapper to apply sysroot and env (ie. LD_LIBRARY_PATH)
# 
# apt install qemu-user
#

# set -ex

log_d() {
  echo "[Debug] $*"
}

log_e() {
  echo "[ERROR] $*"
}

find_first_file() {
  for file in "$*"; do
    [ -e "$file" ] && {
      echo $file
      return
    }
  done
}

# find the gcc executable
TOOLCHAIN_PATH=$(realpath ./cross/aarch64-linux-gnu)
CC=$(find_first_file ${TOOLCHAIN_PATH}/bin/*-gcc)
[ -n "$CC" ] || { log_e "miss GCC"; exit 1; }
log_d "CC: $CC"

# find the toolchain sysroot
[ -n "$TOOLCHAIN_SYSROOT" ] || TOOLCHAIN_SYSROOT=$(${CC} -print-sysroot)
log_d "TOOLCHAIN_SYSROOT: $TOOLCHAIN_SYSROOT"

CROSS_COMPILE=$(${CC} -dumpmachine)
log_d "CROSS_COMPILE: $CROSS_COMPILE"

LD_PATH=${LD_PATH:+${LD_PATH}:}${TOOLCHAIN_PATH}/${CROSS_COMPILE}/lib64:${TOOLCHAIN_PATH}/${CROSS_COMPILE}/lib
log_d "LD_PATH: $LD_PATH"

echo "Execute: qemu-aarch64 -L ${TOOLCHAIN_SYSROOT} -E LD_LIBRARY_PATH=${LD_PATH}"
qemu-aarch64 -L ${TOOLCHAIN_SYSROOT} -E LD_LIBRARY_PATH=${LD_PATH} "$@"
