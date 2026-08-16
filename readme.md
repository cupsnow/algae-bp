Developer Note
====

Target
----

Beagleboard Play / Beagleplay / bp: [bp-devlog.md](docs/bp-devlog.md)

QEMU aarch64 / qemuarm64

Arduino Nano 33 BLE rev2: [nano33ble-devlog.md](docs/nano33ble-devlog.md)

Prerequisite for HOST
----

Ubuntu 26.04

    apt install python3-venv libssl-dev device-tree-compiler swig libgnutls28-dev flex bison cmake autoconf automake autopoint libtool mtools

Build
----

`make dist`

Command example
----

example for dd: `dd if=destdir/bp/rootfs.img of=/dev/sddx bs=4M conv=fdatasync status=progress iflag=nonblock oflag=nonblock`

example to sfdisk

      sfdisk /dev/mmcblk0 <<-EOSFDISK
    label:gpt
    -,200M,uefi,*
    -,2G,linux,-
    -,2G,linux,-
    -,-,linux,-
    EOSFDISK

example for diff and patch

`diff -u pkg_org/file1 pkg/file1 >pkg.patch`

`git diff >package-001-reason.patch`

`patch -p1 <package-001-reason.patch`

example for setup TAP/TUN
----

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

example to setup NFS
----

1. install `sudo apt install nfs-kernel-server`

2. modify **/etc/exports**

        /home/joelai/02_dev 192.168.31.1/24(ro,sync,no_subtree_check,anonuid=1000)
        /home/joelai/Downloads 192.168.31.1/24(rw,sync,no_subtree_check,anonuid=1000)

3. Restart NFS with command `exportfs -r`

4. client **busybox**

        mkdir -p /media/lavender/02_dev
        mount -o nolock 192.168.31.16:/home/joelai/02_dev /media/lavender/02_dev

Check the ELF interpreter (dynamic linker, ie. ld-linux.so)
----

ELF interpreter (dynamic linker, ie. ld-linux.so)

`readelf -l /sbin/init | grep interpreter`


wifi manager
----
[wifimgr.md](docs/wifimgr.md)

Tools
----

yq - yaml / json process
wget https://github.com/mikefarah/yq/releases/latest/download/yq_linux_amd64 -O /usr/bin/yq

jq - for json process

Garage
----

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

