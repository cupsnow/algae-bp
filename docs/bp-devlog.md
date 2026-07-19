Developer Note
====

Repo
----

linux-upstream: v6.11.11, v6.11, 98f7e32f20d28ec452afb208f9cffc08448a2652, de0a9f4486337d0eabacc23bd67ff73146eacdc0
- `6.19.9` ethernet and wifi unavailable

beagleboard linux: 6.12.34-ti-arm64-r45, c009359dd2a3

ti-linux-firmware: `3987d170fc522565c5e4a9293aba1db75951b8c0`

u-boot-upstream: `v2026.01`, 8937bb265a7f2251c1bd999784a4ef10e9c6080d

optee_os-upstream: `5e26ef8f6a9ced63160f8db93c38bb397603036b`

arm-trusted-firmware-upstream: `f2735ebccf5173f74c0458736ec526276106097e`

busybox-upstream: `a6ce017a8a2db09c6f23aa6abf7ce21fd00c2fdf`

Build
----

    make dist

Flash to SD Card
----

    cp -a destdir/bp/boot/* destdir/bp/boot_sd/* /media/joelai/BOOT/
    umount /dev/sddx
    dd if=destdir/bp/rootfs.img of=/dev/sddx bs=4M conv=fdatasync status=progress iflag=nonblock oflag=nonblock

Format emmc
----

      sfdisk /dev/mmcblk0 <<-EOSFDISK
    label:gpt
    -,200M,uefi,*
    -,2G,linux,-
    -,2G,linux,-
    -,-,linux,-
    EOSFDISK

U-Boot
----

U-Boot version (**v2024.10**) failure to use external defconfig, workaround to apply upstream defconfig then patch

After boot, designed to read `uboot.env` in 1st partition (FAT)

Config U-Boot to read uboot.env

For SDCARD

      CONFIG_ENV_IS_NOWHERE=y
      CONFIG_ENV_IS_IN_FAT=y
      CONFIG_SYS_REDUNDAND_ENVIRONMENT=y
      CONFIG_ENV_FAT_DEVICE_AND_PART="1:1"
      CONFIG_SYS_MMC_ENV_DEV=1
      CONFIG_SYS_MMC_ENV_PART=1

For EMMC

      CONFIG_ENV_IS_NOWHERE=y
      CONFIG_ENV_IS_IN_FAT=y
      CONFIG_SYS_REDUNDAND_ENVIRONMENT=y
      CONFIG_ENV_FAT_DEVICE_AND_PART="0:1"
      CONFIG_SYS_MMC_ENV_DEV=0
      CONFIG_SYS_MMC_ENV_PART=1

Memory usage for boot

| addr       | offset | related varable             | memo |
| ---------- | ------ | --------------------------- | ---- |
| 0x80000000 | 0      | scriptaddr                  |      |
| 0x82000000 | 32M    | loadaddr, kernel_addr_r     |      |
| 0x85000000 | 80M    | kernel_comp_addr_r          |      |
| 0x88000000 | 128M   | fdtaddr, fdt_addr_r         |      |
| 0x89000000 | 144M   | dtboaddr, fdtoverlay_addr_r |      |
| 0x90000000 | 256M   | addr_fit                    |      |

The kernel bootargs

    setenv bootargs console=ttyS2,115200n8 earlycon=ns16550a,mmio32,0x02800000

Write bootloader to emmc


    echo "Enable Boot0 boot"
    mmc bootpart enable 1 2 /dev/mmcblk0
    mmc bootbus set single_backward x1 x8 /dev/mmcblk0
    mmc hwreset enable /dev/mmcblk0

    echo "Clearing eMMC boot0"
    echo '0' >> /sys/class/block/mmcblk0boot0/force_ro
    dd if=/dev/zero of=/dev/mmcblk0boot0 count=32 bs=128k

    mkdir /media/boot-sd && mount /dev/mmcblk1p1 /media/boot-sd

    echo "Write bootloader"
    dd if=/media/boot-sd/tiboot3.bin of=/dev/mmcblk0boot0 bs=128k

    echo "Copy the rest of the boot binaries"
    mkdir /media/boot-emmc && mount /dev/mmcblk0p1 /media/boot-emmc
    cp /media/boot-sd/tispl.bin /media/boot-emmc/
    cp /media/boot-sd/u-boot.img /media/boot-emmc/
    sync

More commands

    mmc dev 1 && fatls mmc 1:1

    fatload mmc 1:1 ${addr_fit} ubootenv-bp-a53.txt && env import ${addr_fit};

    fatload mmc 1:1 ${addr_fit} linux.itb && iminfo ${addr_fit};

    bootm ${addr_fit} -

    fatload mmc 1:1 ${addr_fit} Image

    setenv sdboot 'run importenv; run initbootset${bootset} && run loadfit && run loadbootargs && bootm ${addr_fit}'

yocto
----

Reference

- [BeaglePlay: Part 1 – Building a base image using Yocto][guide1]
- [meta-ti-bsp/readme][meta-ti-bsp readme]

[guide1]: https://kickstartembedded.com/2023/08/06/beagleplay-part-1-building-a-base-image-using-yocto/
[meta-ti-bsp readme]: https://git.ti.com/cgit/arago-project/meta-ti/tree/meta-ti-bsp/README?h=kirkstone

Build

1. Clone

        git clone -b kirkstone https://git.yoctoproject.org/poky poky-bp
        cd poky-bp
        git clone -b kirkstone git://git.yoctoproject.org/meta-arm
        git clone -b kirkstone https://git.ti.com/cgit/arago-project/meta-ti

2. Startup dev console

        cd poky-bp
        source oe-init-build-env build-ti
        sudo sysctl -w fs.inotify.max_user_watches=1048576

3. Modify **poky-bp/build-ti/conf/bblayers.conf**

        BBLAYERS ?= " \
        /home/shashank/work/yocto/poky/meta \
        /home/shashank/work/yocto/poky/meta-poky \
        /home/shashank/work/yocto/poky/meta-yocto-bsp \
        /home/shashank/work/yocto/meta-arm/meta-arm-toolchain \
        /home/shashank/work/yocto/meta-arm/meta-arm \
        /home/shashank/work/yocto/meta-ti/meta-ti-bsp \
        "

4. Modify **poky-bp/build-ti/conf/local.conf**

   Choose target from **poky-bp/meta-ti/meta-ti-bsp/conf/machine**

        MACHINE ??= "beagleplay"

5. Run

   Fetch source only

         bitbake core-image-minimal --runall=fetch

Garage
----

u-boot comand collection

    fatls mmc 1:1
    fatload mmc ${mmcdev} ${loadaddr} ${bootenvfile} && env import -t ${loadaddr} ${filesize}
    run loadbootenv && run importbootenv

    env export -t ${loadaddr} && fatwrite mmc ${mmcdev} ${loadaddr} ${bootenvfile} ${filesize}
    env export -t ${loadaddr} && fatwrite mmc ${mmcdev} ${loadaddr} uboot-env2.txt ${filesize}

    fatload mmc 1:1 ${loadaddr} Image.gz && fatload mmc 1:1 ${fdtaddr} k3-am625-beagleplay.dtb
    setenv kernel_comp_addr_r 0x85000000 && setenv kernel_comp_size 0x2000000
    setenv bootargs console=ttyS2,115200n8 earlycon=ns16550a,mmio32,0x02800000

    booti ${loadaddr} - ${fdtaddr}

    setenv bootargs_console "root=/dev/mmcblk1p2 rootwait earlycon=ns16550a,mmio32,0x02800000"

    setenv bootargs_root root=/dev/mmcblk1p2 rw rootwait rootfstype=ext4

    \# emmc
    setenv bootargs_root root=/dev/mmcblk0p2 rootfstype=ext4 rootwait

imx219

    run importenv; run initbootset${bootset} && run loadkern && run loadfdt && run loadbootargs

    fatload ${bootsetdev} ${dtboaddr} k3-am625-beagleplay-csi2-imx219.dtbo

    fdt addr ${fdtaddr} && fdt resize 8192 && fdt apply ${dtboaddr}

    booti ${loadaddr} - ${fdtaddr}

    fdt addr ${fdtaddr} && fdt list
    fdt addr ${fdtaddr} && fdt print
    fdt addr ${fitaddr} && fdt list

    modprobe j721e-csi2rx
    modprobe imx219

    setenv fitext "#conf-2"

    root@algae:~# i2ctransfer -y 4 w2@0x10 0x00 0x00 r2
    0x02 0x19

  
    media-ctl -p

    v4l2-ctl --list-devices

