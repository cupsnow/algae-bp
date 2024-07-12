#!/bin/sh

. /etc/init.d/func

_pri_tag="mdev-mount"

log_d "$0 $*"

case "$1" in
-l|--log)
  exit 0
  ;;
esac

case "$ACTION" in
add)
  [ -z "${MDEV}" ] && exit 1
  cat /proc/mounts | awk '{print $1}' | grep "^/dev/${MDEV}$" || {
    [ -d "/media/${MDEV}" ] || mkdir -p /media/${MDEV}
    mount -o sync /dev/${MDEV} /media/${MDEV} || {
      log_e "Failed mount /dev/${MDEV} to /media/${MDEV}"
      exit 1
    }
  }
  exit 0
  ;;
remove)
  [ -z "${MDEV}" ] && exit 1
  for i in `cat /proc/mounts | grep "${MDEV}" | cut -f 2 -d " "`; do
    umount $i
  done
  [ -d /media/${MDEV} ] && \
    [ "`{ ls /media/${MDEV} | wc -l; } 2>/dev/null`" = "0" ] && \
    rm -rf /media/${MDEV}
  exit 0
  ;;
esac
