<!-- omit from toc -->
# Developer Note

- [beagleplay Ready Source](#beagleplay-ready-source)
- [Build](#build)
- [host qemu](#host-qemu)
- [nfs](#nfs)
  - [Host](#host)
  - [Client](#client)
- [U-Boot](#u-boot)
  - [Env for SDCard](#env-for-sdcard)
  - [Env for EMMC](#env-for-emmc)
  - [added commend to boot from sdcard](#added-commend-to-boot-from-sdcard)
  - [kernel bootargs](#kernel-bootargs)
  - [write uboot to emmc](#write-uboot-to-emmc)
- [yocto](#yocto)
  - [step](#step)
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

## host qemu

`nm-connection-editor`

```sh
sudo ip link del algaebr0 type bridge
sudo ip tuntap del algaetap0 mode tap
```

```sh
sudo ip link add algaebr0 type bridge

sudo ip tuntap add algaetap0 mode tap user `whoami`
sudo ip link set algaetap0 master algaebr0

sudo ip link set wlx94186551a58a master algaebr0

sudo ip link set algaebr0 up
sudo ip link set algaetap0 up
```

```sh
nmcli connection add type bridge ifname algaebr0
nmcli connection modify bridge-algaebr0 bridge.stp yes
nmcli connection modify bridge-algaebr0 ipv4.method manual ipv4.address "10.20.190.2/24" ipv4.gateway "10.20.190.1" ipv4.dns 8.8.8.8
nmcli connection add type bridge-slave ifname wlx94186551a58a master algaebr0
nmcli connection delete wlx94186551a58a
```

## nfs

### Host

Assume host runs Ubuntu 24.04

```sh
sudo apt install nfs-kernel-server
```

**/etc/exports**

```
/home/joelai/02_dev 192.168.31.1/24(ro,sync,no_subtree_check,anonuid=1000)
/home/joelai/Downloads 192.168.31.1/24(rw,sync,no_subtree_check,anonuid=1000)
```

### Client

Assume client runs busybox

```sh
mkdir -p /media/lavender/02_dev
mount -o nolock 192.168.31.16:/home/joelai/02_dev /media/lavender/02_dev
```

## U-Boot

### Env for SDCard

### Env for EMMC

### added commend to boot from sdcard

- Boot from sdcard: `run sdboot`

### kernel bootargs

```sh
setenv bootargs console=ttyS2,115200n8 earlycon=ns16550a,mmio32,0x02800000
```

### write uboot to emmc

commands in linux shell

```
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
```


## yocto

Reference

- [BeaglePlay: Part 1 – Building a base image using Yocto][guide1]
- [meta-ti-bsp/readme][meta-ti-bsp readme]

[guide1]: https://kickstartembedded.com/2023/08/06/beagleplay-part-1-building-a-base-image-using-yocto/
[meta-ti-bsp readme]: https://git.ti.com/cgit/arago-project/meta-ti/tree/meta-ti-bsp/README?h=kirkstone

### step

1. Clone

   ```sh
   git clone -b kirkstone https://git.yoctoproject.org/poky poky-bp
   cd poky-bp
   git clone -b kirkstone git://git.yoctoproject.org/meta-arm
   git clone -b kirkstone https://git.ti.com/cgit/arago-project/meta-ti
   ```
2. Startup dev console

   ```sh
   cd poky-bp
   source oe-init-build-env build-ti
   sudo sysctl -w fs.inotify.max_user_watches=1048576
   ```

3. Modify **poky-bp/build-ti/conf/bblayers.conf**

   ```
   BBLAYERS ?= " \
   /home/shashank/work/yocto/poky/meta \
   /home/shashank/work/yocto/poky/meta-poky \
   /home/shashank/work/yocto/poky/meta-yocto-bsp \
   /home/shashank/work/yocto/meta-arm/meta-arm-toolchain \
   /home/shashank/work/yocto/meta-arm/meta-arm \
   /home/shashank/work/yocto/meta-ti/meta-ti-bsp \
   "
   ```

4. Modify **poky-bp/build-ti/conf/local.conf**

   Choose target from **poky-bp/meta-ti/meta-ti-bsp/conf/machine**

   ```
   MACHINE ??= "beagleplay"
   ```

5. Run

   Fetch source only

   ```sh
   bitbake core-image-minimal --runall=fetch
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

