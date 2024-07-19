#!/bin/bash

# qemu esc: ctrl-a x

log_d () {
  echo "[Debug] $*"
}

log_e () {
  echo "[ERROR] $*"
}

cmd_run () {
  log_d "Execute $*"
  "$@"
}

_pri_destdir="destdir/qemuarm64"

cmd_qemu_virtio1="-drive id=boot,file=fat:rw:${_pri_destdir},format=raw,media=disk"
# cmd_qemu_virtio2="-drive id=lfs,file=destdir/lfs.bin,format=raw,media=disk"
cmd_qemu_virtio2="-drive id=lfs,file=destdir/lfs.bin,format=raw,media=disk,if=none -device virtio-blk-device,drive=lfs"

cmd_qemu_base1="qemu-system-aarch64"
cmd_qemu_base1="${cmd_qemu_base1} -cpu cortex-a57 -m 512M -smp 2"
cmd_qemu_base1="${cmd_qemu_base1} -machine virt,virtualization=on,secure=off"
cmd_qemu_base1="${cmd_qemu_base1} -nographic"

run_uboot1 () {
  _lo_cmd_qemu="${cmd_qemu_base1} ${cmd_qemu_virtio1} ${cmd_qemu_virtio2}"
  _lo_dtb="${_pri_destdir}/boot/qemuarm64.dtb"
  _lo_ub="${_pri_destdir}/boot/u-boot.bin"

  if [ ! -f "${_lo_dtb}" ]; then
    # shellcheck disable=SC2086
    cmd_run ${_lo_cmd_qemu} -M virt,dumpdtb=${_lo_dtb}
    cmd_run dtc -I dtb -O dts ${_lo_dtb} -o ${_lo_dtb%.dtb}.dts
  fi
 
  # shellcheck disable=SC2086
  cmd_run ${_lo_cmd_qemu} \
    -bios ${_lo_ub}

# ub
#   setenv bootargs console=ttyAMA0 root=/dev/vda rw rootwait && fatload virtio 1:1 ${kernel_addr_r} boot/Image && booti ${kernel_addr_r} - ${fdtcontroladdr}
#   env export ${loadaddr} && fatwrite virtio 0:1 ${loadaddr} uenv-exported.txt ${filesize}
#   virtio info
#   fatls virtio 0:1 boot
#   fatload virtio 0:1 ${fdt_addr} boot/qemuarm64.dtb
#   booti ${kernel_addr_r} - ${fdt_addr}
#   

}

run_kernel1 () {
  # shellcheck disable=SC2086
  _lo_cmd_qemu="${cmd_qemu_base1} ${cmd_qemu_virtio1} ${cmd_qemu_virtio2}"
  _lo_kernel="${_pri_destdir}/boot/Image"
  _lo_bootargs="console=ttyAMA0 root=/dev/vda rw rootwait"
  cmd_run ${_lo_cmd_qemu} \
    -kernel ${_lo_kernel} \
    --append "$_lo_bootargs"

}

if [ -n "$1" ]; then
  "$@"
  exit
fi

run_test1
