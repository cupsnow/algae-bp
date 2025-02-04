#!/bin/bash
# shellcheck disable=SC2120

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

_lo_qemuargs_bootdisk="-drive id=boot,file=fat:rw:${_pri_destdir}/boot,format=raw,media=disk"
_lo_qemuargs_rootdisk="-drive id=rootfs,file=${_pri_destdir}/rootfs.img,format=raw,media=disk,if=none -device virtio-blk-device,drive=rootfs"

_lo_qemuargs_nic="-net nic,vlan=0,macaddr=52:53:00:11:12:13,model=e1000,addr=08 -net user"
_lo_qemuargs_nic2="-nic user,model=virtio-net-pci"
_lo_qemuargs_nic3="-netdev user,id=net0,dhcpstart=10.0.2.30 -device virtio-net-pci,netdev=net0"
_lo_qemuargs_nic4="-netdev type=tap,id=net0 -device virtio-net-device,netdev=net0"

cmd_qemu_base1="qemu-system-aarch64"
cmd_qemu_base1="${cmd_qemu_base1} -cpu cortex-a57 -m 512M -smp 2"
cmd_qemu_base1="${cmd_qemu_base1} -machine virt,virtualization=on,secure=off"
cmd_qemu_base1="${cmd_qemu_base1} -nographic"

cmd_qemu_bootroot1="${cmd_qemu_base1} ${_lo_qemuargs_bootdisk} ${_lo_qemuargs_rootdisk}"

run_qemu () {
  _lo_cmd_qemu="${cmd_qemu_base1}"
  cmd_run eval "${_lo_cmd_qemu}" "$@"
}

query_nic_model () {
  run_qemu -net nic,model=?  
}

start_uboot1 () {
  _lo_cmd_qemu="${cmd_qemu_bootroot1}"
  _lo_dtb="${_pri_destdir}/boot/qemuarm64.dtb"
  _lo_ub="${_pri_destdir}/boot/u-boot.bin"

  if [ ! -f "${_lo_dtb}" ]; then
    # shellcheck disable=SC2086
    cmd_run ${_lo_cmd_qemu} -M virt,dumpdtb=${_lo_dtb}
    cmd_run dtc -I dtb -O dts ${_lo_dtb} -o ${_lo_dtb%.dtb}.dts
  fi
 
  # shellcheck disable=SC2086
  cmd_run ${_lo_cmd_qemu} \
    -bios ${_lo_ub} \
    "$@"
}

start_kernel1 () {
  # shellcheck disable=SC2086
  _lo_cmd_qemu="${cmd_qemu_bootroot1}"
  _lo_kernel="${_pri_destdir}/boot/Image"
  _lo_bootargs="console=ttyAMA0 root=/dev/vda rw rootwait"
  
  # shellcheck disable=SC2086
  cmd_run ${_lo_cmd_qemu} \
    -kernel ${_lo_kernel} \
    --append "$_lo_bootargs" \
    "$@"
}

show_help () {

  _lo_cmd_qemu="${cmd_qemu_base1}"

  cat <<-EOHELP
USAGE
  ${1:-$(basename $0)} [OPTIONS] [COMMAND [-- COMMAND_OPTIONS]]

OPTIONS
  --help   Show this help

COMMAND
  run_qemu
    - Run: $_lo_cmd_qemu
  start_uboot1
    - Start guest with uboot image
  start_kernel1
    - Start guest with kernel image and rootfs
  query_nic_model
    - Query the guest support NIC model

EOHELP
}

_pri_opts="$(getopt -l "help" -- h "$@")" || exit 1

eval set -- "$_pri_opts"
while [ -n "$1" ]; do
  case "$1" in
  -h|--help)
    shift
    show_help
    exit 1
    ;;
  --)
    shift
    break
    ;;
  esac
done

log_d "Escape sequence: ctrl-a x"
log_d "Run command: $*"

if [ -z "$1" ] || ! typeset -F | grep -q "declare -f $1"; then
  show_help
  exit 1
fi

"$@"
