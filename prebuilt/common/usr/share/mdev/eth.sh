#!/bin/sh

. /etc/init.d/func

_pri_tag="mdev-eth"

log_d "$0 $*"

case "$1" in
-l|--log)
  exit 0
  ;;
esac

case "$ACTION" in
add)
  [ -z "${MDEV}" ] && exit 1
  ifplugd -i ${MDEV} -k || log_e "Failed ifplugd -i ${MDEV} -k"
  _pri_ct=5
  while true; do
    ifplugd -i ${MDEV} -Mpql -u 6 && { log_d "Successful ifplugd -i ${MDEV} -Mpql"; break; }
    _pri_ct=$(( $_pri_ct - 1 ))
    log_e "Failed ifplugd -i ${MDEV} -Mpql (retry $_pri_ct more)"
    [ $_pri_ct -gt 0 ] || break
    sync ; sync; sleep 1
  done
  exit 0
  ;;
remove)
  [ -z "${MDEV}" ] && exit 1
  exit 0
  ;;
esac

