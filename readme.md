Developer Note
====

beagleplay source
----

<!-- linux-upstream: de0a9f4486337d0eabacc23bd67ff73146eacdc0 -->
linux-upstream: 98f7e32f20d28ec452afb208f9cffc08448a2652 or v6.11
ti-linux-firmware: 3987d170fc522565c5e4a9293aba1db75951b8c0
u-boot-upstream: 8937bb265a7f2251c1bd999784a4ef10e9c6080d
optee_os-upstream: 5e26ef8f6a9ced63160f8db93c38bb397603036b
arm-trusted-firmware-upstream: f2735ebccf5173f74c0458736ec526276106097e
busybox-upstream: a6ce017a8a2db09c6f23aa6abf7ce21fd00c2fdf

Build
----

    make dist

Flash to SD Card
----

    umount /dev/sddx
    dd if=destdir/bp/rootfs.img of=/dev/sddx bs=4M conv=fdatasync status=progress iflag=nonblock oflag=nonblock
    cp -a destdir/bp/boot/* destdir/bp/boot_sd/* /media/joelai/BOOT/

format emmc
----

      sfdisk /dev/mmcblk0 <<-EOSFDISK
    label:gpt
    -,200M,uefi,*
    -,2G,linux,-
    -,2G,linux,-
    -,-,linux,-
    EOSFDISK

diff and patch
----

git diff

    git diff >package-001-reason.patch

patch

    patch -p1 <package-001-reason.patch

GPIO -> DAPLink
----

[cat /sys/kernel/debug/gpio](docs/bp-gpio.txt)

| gpio_sysfs | MikroBus | net      | DAPLink |
| ---------- | -------- | -------- | ------- |
| 640        | INT      | GPIO1_9  | SWDIO   |
| 641        | AN       | GPIO1_10 | SWDCLK  |
| 642        | PWM      | GPIO1_11 |         |
| 643        | RST      | GPIO1_12 | RESET   |

```
./devsync.sh gpio_init 640 out
./devsync.sh gpio_init 641 out
./devsync.sh gpio_init 643 out
openocd -f bpgpioswd.cfg
openocd -f bpgpioswd.cfg -c "program arduino_nano_33_ble_bootloader-0.9.2_s140_6.1.1.hex verify reset exit"
openocd -f bpgpioswd.cfg -c "program arduino_nano_33_ble_bootloader-0.9.2-29-g6a9a6a3_s140_6.1.1.hex verify reset exit"

```

make CROSS_COMPILE=/home/joelai/07_sw/pkg/toolchain-arm-none-eabi/bin/arm-none-eabi- BOARD=arduino_nano_33_ble all


There are two pins, DFU and FRST that bootloader will check upon reset/power:

    Double Reset Reset twice within 500 ms will enter DFU with UF2 and CDC support (only works with nRF52840)
    DFU = LOW and FRST = HIGH: Enter bootloader with UF2 and CDC support
    DFU = LOW and FRST = LOW: Enter bootloader with OTA, to upgrade with a mobile application such as Nordic nrfConnect/Toolbox
    DFU = HIGH and FRST = LOW: Factory Reset mode: erase firmware application and its data
    DFU = HIGH and FRST = HIGH: Go to application code if it is present, otherwise enter DFU with UF2
    The GPREGRET register can also be set to force the bootloader can enter any of above modes (plus a CDC-only mode for Arduino). GPREGRET is set by the application before performing a soft reset.

```c
#define BUTTON_1              _PINNUM(1, 11)  // D2 switch
#define BUTTON_2              _PINNUM(1, 12)  // D3 switch

#ifndef BUTTON_DFU
#define BUTTON_DFU      BUTTON_1
#endif

#ifndef BUTTON_FRESET
#define BUTTON_FRESET   BUTTON_2
#endif
```




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

nfs
----

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

U-Boot
----

For BP, currently upstream version (**v2024.10**) failure to use external defconfig, workaround to apply upstream defconfig then patch

### memory for boot
| addr       | offset | related varable             | memo |
| ---------- | ------ | --------------------------- | ---- |
| 0x80000000 | 0      | scriptaddr                  |
| 0x82000000 | 32M    | loadaddr, kernel_addr_r     |
| 0x85000000 | 80M    | kernel_comp_addr_r          |
| 0x88000000 | 128M   | fdtaddr, fdt_addr_r         |
| 0x89000000 | 144M   | dtboaddr, fdtoverlay_addr_r |
| 0x90000000 | 256M   | addr_fit                    |


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

### other commands

```
mmc dev 1 && fatls mmc 1:1

fatload mmc 1:1 ${addr_fit} ubootenv-bp-a53.txt && env import ${addr_fit};

fatload mmc 1:1 ${addr_fit} linux.itb && iminfo ${addr_fit};

bootm ${addr_fit} -

fatload mmc 1:1 ${addr_fit} Image

setenv sdboot 'run importenv; run initbootset${bootset} && run loadfit && run loadbootargs && bootm ${addr_fit}'


```

yocto
----

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

todo
----

- RPi NoIR Camera V2 based on IMX219
- Waveshare 2.9inch e-Paper
- hdmi framebuffer

Garage
----

- Check the ELF interpreter (dynamic linker, ie. ld-linux.so)
  readelf -l /sbin/init | grep interpreter

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

