#!/bin/bash

log_ts() {
  echo "$(date +%Y-%m-%d\ %H:%M:%S)"
}

log_d() {
  _lo_ts=$(log_ts)
  echo "${_lo_ts:+[${_lo_ts}]}[DEBUG] $*"
}

log_e() {
  _lo_ts=$(log_ts)
  echo "${_lo_ts:+[${_lo_ts}]}[ERROR] $*"
}

cmd_run() {
  log_d "Running command: $*"
  "$@"
}

_pri_opts_org="$@"
_pri_opts=$(getopt -o h --long help -n "$0" -- "$@")
eval set -- "$_pri_opts"

show_help() {
  cat <<-EOHELP
Usage: $0 [options]
Options:
  -h, --help    Show this help message and exit
EOHELP
}

while true; do
  case "$1" in
  -h|--help)
    show_help
    # exit 1
    ;;
  --)
    shift
    break
    ;;
  esac
  shift
done

log_d "argc: $#; non-option arguments: $*"


