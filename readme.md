<!-- omit from toc -->
# Developer Note

- [uboot command](#uboot-command)
- [Garage](#garage)

## uboot command

```sh
=> fatls mmc 1:1
   265925   tiboot3.bin
   822383   tispl.bin
  1011075   u-boot.img
 14148875   Image.gz
    55032   k3-am625-beagleplay.dtb

5 file(s), 0 dir(s)
```

fdt_addr_r=0x88000000
fdtaddr=0x88000000
kernel_addr_r=0x82000000
loadaddr=0x82000000

fatload mmc ${mmcdev} ${loadaddr} ${bootenvfile} && env import -t ${loadaddr} ${filesize}
run loadbootenv && run importbootenv

env export -t ${loadaddr} && fatwrite mmc ${mmcdev} ${loadaddr} ${bootenvfile} ${filesize}

fatload mmc 1:1 ${loadaddr} Image.gz && fatload mmc 1:1 ${fdtaddr} k3-am625-beagleplay.dtb
setenv kernel_comp_addr_r 0x85000000 && setenv kernel_comp_size 0x2000000
setenv bootargs console=ttyS2,115200n8 earlycon=ns16550a,mmio32,0x02800000

booti ${loadaddr} - ${fdtaddr}

## Garage


