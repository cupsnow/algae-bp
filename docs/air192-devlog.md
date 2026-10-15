algae application on Air192
====

- Bluetooth on Air192 memo [`air192-bt.md`](air192-bt.md)

System

- The board and runtime based on **Air192**
- algae application based on `/media/02_dev/algae-ws/build/sysroot-air192`

Runtime environment

    SYSROOT_AIR192=/media/02_dev/algae-ws/build/sysroot-air192
    LD_LIBRARY_PATH=${SYSROOT_AIR192}$/usr/lib:${SYSROOT_AIR192}$/lib:${SYSROOT_AIR192}$/lib/systemd
    PATH=$PATH:${SYSROOT_AIR192}$/usr/bin:${SYSROOT_AIR192}$/sbin:${SYSROOT_AIR192}$/bin
