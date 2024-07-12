#!/bin/sh

. /etc/init.d/func

_pri_tag="mdev-default"

log_d "$0 $*"

# _pri_mdev_log="/var/run/mdev-default.log"
# [ -f "$_pri_mdev_log" ] && rm $_pri_mdev_log

_pri_mdev_log=${_pri_mdev_log:-/dev/null}
log_f "$_pri_mdev_log"
log_f "$_pri_mdev_log" "$0 $*"
