#!/bin/bash

log_e () {
  _lo_eno=$?
  echo "[ERROR] $*"
  return $_lo_eno
}

log_d () {
  _lo_eno=$?
  echo "[Debug] $*"
  return $_lo_eno
}

cmd_run () {
  log_d "Execute: $*"
  "$@"
}


show_help () {
cat <<- EOHELP
Usage: $(basename $0) [OPTIONS] [COMMAND]
OPTIONS:
  h, help          Show this help
  t, target=<DEV>  The target device
  y, silent        Apply default value instead of prompt

EOHELP
}

_pri_opts=$(getopt -o 'ht:y' --long 'help,target:,silent' -- "$@") || {
  log_e "Failed parse command line arguments"
  exit 1
}

_pri_opt_target=
_pri_opt_silent=

eval set -- "$_pri_opts"

while [ -n "$1" ]; do
  case "$1" in
  -t|--target)
    _pri_opt_target=$2
    shift
    ;;
  -y|--silent)
    _pri_opt_silent=1
    ;;
  -h|--help)
    show_help
    exit 1
    ;;
  --)
    shift
    break
    ;;
  *)
    log_e "Invalid argument: $1"
    exit 1
    ;;
  esac
  shift
done

log_d "non-optional: $*"

# no target -> take non-optional argument
[ -z "$_pri_opt_target" ] && [ -n "$1" ] && _pri_opt_target=$1
[ -n "$_pri_opt_target" ] || {
  log_e "Miss target"
  show_help
  exit 1
}

[[ "$_pri_opt_target" =~ ^/dev/ ]] || {
  _pri_opt_target="/dev/${_pri_opt_target}"
}

[ -n "$_pri_opt_silent" ] || {
  _lo_yes=
  read -p "Are you sure dd to ${_pri_opt_target} (y|N)" -n 1 -r _lo_yes
  echo ""
  [[ "$_lo_yes" =~ ^[Y|y]$ ]] || {
    log_e "break"
    exit 1
  }
}

cmd_run eval "sudo dd if=destdir/bp/rootfs.img of=${_pri_opt_target} bs=4M conv=fdatasync"
