#!/bin/bash

ts1 () {
  date +"%y/%m/%d %H:%M:%S"
}

log_d () {
  echo "[$(ts1)][Debug] $*"
}

path_push () {
  # when nothing to push, return original $PATH
  _lo_path=$1
  [ $# -ge 2 ] || { echo ${_lo_path}; return 0; }
  shift
  for _lo_dir in "$@"; do
    re="(^${_lo_dir}:|:${_lo_dir}:|:${_lo_dir}$)"
    [[ ${_lo_path} =~ $re ]] || _lo_path="${_lo_dir}:${_lo_path}"
  done
  echo "$_lo_path"
}

qemu_destdir="~/02_dev/qemu-ws/destdir"
qemu_system_aarch64=qemu-system-aarch64

export PATH=$(path_push "$PATH" "${qemu_destdir:+${qemu_destdir}/bin}")

log_d "PATH: ${PATH}"
