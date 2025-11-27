#!/bin/bash

log_d () {
  echo "[debug] $*"
}

cmd_run () {
  log_d "Execute $*"
  "$@"
}

atech_reader_vid=11b0
atech_reader_pid=6368
atech_reader_usbid="${atech_reader_vid}:${atech_reader_pid}"

# 0bda:c820 rtl8821cs
# 11b0:6368 ATECH FLASH TECHNOLOGY Multi-Reader
usb_find () {
  # log_d "\$1: $1"
  for _lo_dev in /sys/bus/usb/devices/*; do
      if [ -f "$_lo_dev/idVendor" ] && [ -f "$_lo_dev/idProduct" ]; then
          _lo_vid=$(cat "$_lo_dev/idVendor")
          _lo_pid=$(cat "$_lo_dev/idProduct")
          if [ "${_lo_vid}:${_lo_pid}" = "$1" ]; then
              # echo "Found device at $_lo_dev"
              # Look for corresponding block device
              find "$_lo_dev" -name 'sd*' 2>/dev/null
              echo "$_lo_dev"
              return 0
          fi
      fi
  done
  return 1
}

atech_reader_bus=$(usb_find "${atech_reader_usbid}")
log_d "xxxxxxxxxxx $atech_reader_bus"
# cmd_run eval "ls -F \"/sys/bus/usb/devices/${atech_reader_bus}/\""

# find -L /sys/bus/usb/devices/ -maxdepth 2 -type f -name idVendor -exec grep -l "^${atech_reader_vid}\$" "{}" \; | while read vendor_file; do
#     dir=$(dirname "$vendor_file")
#     if [ "$(cat "$dir/idProduct")" = "${atech_reader_pid}" ]; then
#         echo "USB Device Path: $dir"
#         lsblk -S | grep "$(basename "$dir")"
#     fi
# done

