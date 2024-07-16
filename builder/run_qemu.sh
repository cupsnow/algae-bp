#!/bin/bash

cmd_qemu_base1="qemu-system-aarch64"
cmd_qemu_base1="${cmd_qemu_base1} -machine virt,virtualization=on,secure=off"
cmd_qemu_base1="${cmd_qemu_base1} -cpu cortex-a57 -m 512 -smp 2"
cmd_qemu_base1="${cmd_qemu_base1} -nographic"

cmd_qemu_disk1="-drive file=fat:rw:./destdir/qemuarm64,format=raw,media=disk"

run_export_dtb () {
  # shellcheck disable=SC2086
  ${cmd_qemu_base1} ${cmd_qemu_disk1} \
    -M virt,dumpdtb=cortex-a57.dtb \
    -bios ../build/uboot-qemuarm64/u-boot.bin

  dtc -I dtb -O dts cortex-a57.dtb -o cortex-a57.dts
}

run_test1 () {
  # shellcheck disable=SC2086
  ${cmd_qemu_base1} ${cmd_qemu_disk1} \
    -bios ../build/uboot-qemuarm64/u-boot.bin \
   

# ub
#   virtio info
#   fatls virtio 0:1 boot
#   fatload virtio 0:1 ${kernel_addr_r} boot/Image
#   booti ${kernel_addr_r}
}

if [ -n "$1" ]; then
  "$@"
  exit
fi

run_test1
