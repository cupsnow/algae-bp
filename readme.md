<!-- omit from toc -->
# Developer Note

- [beagleplay Ready Source](#beagleplay-ready-source)
- [Build](#build)
- [U-Boot command](#u-boot-command)
- [Issue](#issue)
  - [Debug U-Boot Load Kernel](#debug-u-boot-load-kernel)
- [Garage](#garage)

## beagleplay Ready Source

linux-upstream: de0a9f4486337d0eabacc23bd67ff73146eacdc0
ti-linux-firmware: 3987d170fc522565c5e4a9293aba1db75951b8c0
u-boot-upstream: 8937bb265a7f2251c1bd999784a4ef10e9c6080d
optee_os-upstream: 5e26ef8f6a9ced63160f8db93c38bb397603036b
arm-trusted-firmware-upstream: f2735ebccf5173f74c0458736ec526276106097e
busybox-upstream: a6ce017a8a2db09c6f23aa6abf7ce21fd00c2fdf

## Build

   ```sh
   make dist && make dist_sd
   ```

## U-Boot command

- Boot from sdcard: `run sdboot`

## Issue

### Debug U-Boot Load Kernel

```sh
setenv bootargs console=ttyS2,115200n8 earlycon=ns16550a,mmio32,0x02800000
```

## Garage

- u-boot comand collection

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

- preload u-boot

   ```sh
   => part list mmc 0

   Partition Map for MMC device 0  --   Partition Type: DOS

   Part    Start Sector    Num Sectors     UUID            Type
   1     2048            262144          c5802c6f-01     0c Boot
   2     264192          30357504        c5802c6f-02     83
   => part list mmc 1

   Partition Map for MMC device 1  --   Partition Type: DOS

   Part    Start Sector    Num Sectors     UUID            Type
   1     4096            512000          925eb125-01     0c Boot
   2     516096          120545280       925eb125-02     83
   => fatls mmc 0:1
               System Volume Information/
               extlinux/
               overlays/
      61704   k3-am625-beagleplay.dtb
         54   ID.txt
   29315584   Image
   15402499   initrd.img
      56043   k3-am625-sk-lpmdemo.dtb
      55532   k3-am625-sk.dtb
      42192   k3-am625-skeleton.dtb
      323786   tiboot3.bin
      996328   tispl.bin
   1044684   u-boot.img

   10 file(s), 3 dir(s)
   ```

