#!/bin/sh

if [ -z "$SYSROOT_AIR192" ] || ! type -p python3; then
  export SYSROOT_AIR192=/media/02_dev/algae-ws/build/sysroot-air192
  export LD_LIBRARY_PATH=${SYSROOT_AIR192}/usr/lib:${SYSROOT_AIR192}/lib:${SYSROOT_AIR192}/lib/systemd
  export PATH=$PATH:${SYSROOT_AIR192}/usr/bin:${SYSROOT_AIR192}/sbin:${SYSROOT_AIR192}/bin
fi
