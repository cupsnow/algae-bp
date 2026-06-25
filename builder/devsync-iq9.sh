#!/bin/sh
# shellcheck disable=SC2120,SC1091

# scp joelai@192.168.234.16:~/02_exdev2/iq9/devsync.sh ./
# ./devsync.sh "~/02_exdev2/algae-ws/build/busybox-aarch64/busybox" ./
# mkdir -p /run/lavender5/02_exdev2 /run/lavender5/dw
# ./busybox mount -o nolock 192.168.234.16:/mnt/dev/02_exdev2 /run/lavender5/02_exdev2

alias mount="~/busybox mount"
alias umount="~/busybox umount"

log_ts() {
  date "+%y-%m-%d %H:%M:%S"
}

log_d() {
  _lo_ts="$(log_ts)"
  echo "${_lo_ts:+[${_lo_ts}]}[Debug] $*"
}

log_e() {
  _lo_ts="$(log_ts)"
  echo "${_lo_ts:+[${_lo_ts}]}[ERROR] $*"
}

cmd_run () {
  log_d "Execute: $*"
  "$@"
}

# add_list _list_name_only fn1 fn2 fn2 fn3
add_list() {
  [ "$#" -ge 2 ] || { log_e "Nothing to add"; return 1; }
  _lo_list=$1
  shift
  _lo_old="$(eval echo \$$_lo_list)"
  for _lo_in in "$@"; do
    for _lo_iter in $_lo_old; do
      if [ "$_lo_in" = "$_lo_iter" ]; then
        # zero when found
        _lo_in=
        break
      fi
    done
    # non-zero to add
    [ -n "$_lo_in" ] || continue
    eval "$_lo_list=\${$_lo_list:+"\$$_lo_list "}${_lo_in}"
  done
}

# rm_list list_name_only fn1 fn2
rm_list() {
  [ "$#" -ge 2 ] || { log_e "Nothing to rm"; return 1; }
  _lo_list=$1
  shift
  _lo_old="$(eval echo \$$_lo_list)"
  _lo_new=
  for _lo_iter in $_lo_old; do
    for _lo_in in "$@"; do
      if [ "$_lo_in" = "$_lo_iter" ]; then
        # zero when found
        _lo_in=
        break
      fi
    done
    # non-zero to recover
    [ -n "$_lo_in" ] || continue
    _lo_new="${_lo_new:+"$_lo_new "}${_lo_iter}"
  done
  if [ ! "$_lo_new" = "$_lo_old" ]; then
    eval "$_lo_list=\"$_lo_new\""
  fi
}

get_ip () {
  if [ -n "$_pri_ip" ]; then
    echo "${_pri_ip}"
    return 0
  fi
  if [ "$_pri_devsite" = "mbp_ub22" ]; then
    _lo_iptest="192.168.234.86 192.168.50.42"
  else
    _lo_iptest="192.168.234.16 192.168.50.123"
  fi
  for i in $_lo_iptest; do
    if cmd_run eval "ping -c 1 -W 1 ${i} >/dev/null 2>&1" >/dev/null 2>&1; then
      _pri_ip=${i}
      echo "${_pri_ip}"
      return 0
    fi
  done
  return 1
}

get_nfsroot () {
  if [ -n "$_pri_nfsroot" ]; then
    echo "${_pri_nfsroot}"
    return 0
  fi
  for i in /run /media /mnt /var/run /tmp; do
    if [ -d "$i" ]; then
      _pri_nfsroot="${i}/lavender5"
      echo "${_pri_nfsroot}"
      return 0
    fi
  done
  return 1
}

_pri_nfsdw="$(get_nfsroot)/dw"
_pri_nfseveplayws="$(get_nfsroot)/02_dev/eveplay-ws"
_pri_nfseshws="$(get_nfsroot)/02_exdev2/agt-ws/esh-ws"
_pri_nfsalgaews="$(get_nfsroot)/02_exdev2/algae-ws"
_pri_nfsiq9ws="$(get_nfsroot)/02_exdev2/iq9"

add_ok() {
  rm_list _pri_listfailed "$@"
  add_list _pri_listok "$@"
}

add_failed() {
  rm_list _pri_listok "$@"
  add_list _pri_listfailed "$@"
}

find_mount() {
  # root@Eve_Play: ~ # cat /proc/mounts
  # ubi0:rootfs / ubifs rw,sync,relatime,assert=read-only,ubi=0,vol=0 0 0
  # devtmpfs /dev devtmpfs rw,relatime,size=51140k,nr_inodes=12785,mode=755 0 0
  # none /proc proc rw,relatime 0 0
  # none /sys sysfs rw,relatime 0 0
  # none /dev/pts devpts rw,relatime,mode=600,ptmxmode=000 0 0
  # none /dev/mqueue mqueue rw,relatime 0 0
  # none /var/run tmpfs rw,relatime,size=10240k 0 0
  # none /var/lock tmpfs rw,relatime,size=10240k 0 0
  # none /media tmpfs rw,relatime,size=40960k 0 0
  # none /sys/kernel/debug debugfs rw,relatime 0 0
  # /dev/ubi1_0 /mnt/cfg ubifs rw,relatime,assert=read-only,ubi=1,vol=0 0 0
  _pri_for_iter=0
  while read -r _lo_line; do
    # [ $_pri_for_iter -lt $_pri_for_count ] || break
    # echo "[$_pri_for_iter]$_lo_line"

    read -r _pri_dev _pri_dir _pri_fs _dommy <<-EOM
$_lo_line
EOM
    # log_d "[#$_pri_for_iter] $_pri_dev, $_pri_dir, $_pri_fs"

    _lo_ng=
    [ -n "$_lo_ng" ] || [ "$1" = "*" ] || [ "$1" = "$_pri_dev" ] || _lo_ng=n
    [ -n "$_lo_ng" ] || [ -z "$2" ] || [ "$2" = "*" ] || [ "$2" = "$_pri_dir" ] \
      || _lo_ng=n
    [ -n "$_lo_ng" ] || [ -z "$3" ] || [ "$3" = "*" ] || [ "$3" = "$_pri_fs" ] \
      || _lo_ng=n
    [ -z "$_lo_ng" ] && { echo "$_lo_line"; return 0; }

    _pri_for_iter="$(( $_pri_for_iter + 1 ))"
  done <<-EOR
$(cat /proc/mounts)
EOR
  return 1
}

devsync_scp() {
  [ "$#" -ge 1 ] || { log_e "Invalid argument"; return 1; }
  if [ "$#" -eq 1 ]; then
    cmd_run scp "joelai@$(get_ip):$@" ./
  else
    cmd_run scp "joelai@$(get_ip):$@"
  fi
}

# mount device, ie. not nfs
devsync_mount() {
  [ "$#" -ge 1 ] || { log_e "Invalid argument"; return 1; }

  _lo_src="$1"
  _lo_tgt="${2:-/media/$(basename ${_lo_src})}"

  find_mount "*" "${_lo_tgt}" >/dev/null && { log_d "already mounted ${_lo_tgt}"; return 0; }

  [ -d "${_lo_tgt}" ] || mkdir -p "${_lo_tgt}"
  cmd_run eval "$_pri_mount \"${_lo_src}\" \"${_lo_tgt}\"" || return 1
  log_d "mounted ${_lo_tgt}"
  return 0
}

devsync_nfsumount() {
  [ "$#" -ge 1 ] || { log_e "Invalid arguments"; return 1; }
  _lo_tgt="${1}"
  find_mount "*" "${_lo_tgt}" >/dev/null || { return 0; }
  cmd_run umount -f "$_lo_tgt" || { return 1; }
  log_d "un-mounted ${_lo_tgt}"
  return 0
}

devsync_nfsmount() {
  # [ "$#" -ge 1 ] || { log_e "Invalid arguments"; return 1; }
  if [ "$_pri_devsite" == "mbp_ub22" ]; then
    _lo_src="${1:-/home/joelai/02_dev}"
  else
    _lo_src="${1:-/mnt/dev/02_exdev2}"
  fi
  _lo_tgt="${2:-$(get_nfsroot)/$(basename "${_lo_src}")}"
  _lo_ip="${3:-$(get_ip)}"

  find_mount "*" "${_lo_tgt}" >/dev/null && { log_d "already mounted ${_lo_tgt}"; return 0; }

  cmd_run eval "ping -c 1 -W 1 ${_lo_ip} >/dev/null 2>&1" || { log_e "failed ping to ${_lo_ip}"; return 1; }

  [ -d "${_lo_tgt}" ] || mkdir -p "${_lo_tgt}"
  cmd_run eval "mount -o nolock \"${_lo_ip}:${_lo_src}\" \"${_lo_tgt}\"" || return 1
  log_d "mounted ${_lo_tgt}"
  return 0
}

nfsget_n () {
  [ "$#" -ge 1 ] || { log_e "Invalid arguments"; return 1; }
  _nfsgetn_lo_tgt="${2:-$(basename "$1")}"

  if ! cmd_run cp -dpR "$1" "${_nfsgetn_lo_tgt}"; then
    add_failed "$_nfsgetn_lo_tgt"
  	return 1
  fi
  add_ok "$_nfsgetn_lo_tgt"
  return 0
}

nfsget_s () {
  [ "$#" -ge 1 ] || { log_e "Invalid arguments"; return 1; }
  _lo_tgt="${2:-$(basename "$1")}"
  _mdsum1="1"
  _mdsum2="2"

  if [ -e "$_lo_tgt" ]; then
    _mdsum1="$(md5sum "$1" | awk '{print $1}')"
    _mdsum2="$(md5sum "$_lo_tgt" 2>/dev/null | awk '{print $1}')"
  fi
  if [ "$_mdsum1" = "$_mdsum2" ]; then
    log_d "same file: $1, $_lo_tgt"
    add_ok "$_lo_tgt"
    return 0
  fi
  nfsget_n "$@"
}

nfsget_x () {
  [ "$#" -ge 1 ] || { log_e "Invalid arguments"; return 1; }
  _lo_tgt="${2:-$(basename "$1")}"
  nfsget_n "$@" && cmd_run chmod +x "$_lo_tgt"
}

# shellcheck disable=SC2120
show_help () {
cat <<-EOHELP
USAGE
  ${1:-$(basename $0)} [OPTIONS] [COMMAND ...]

OPTIONS
  --help         Show this help
  -m, --nfsmount=[0|2]
      Non-arg: mount NFS,
      2: also mount 'dw'
      0: do umount

COMMAND
  wificonn <SSID> <PW> [open | wpa3-only]
      ex: ./devsync.sh wificonn DK_SWQA_Linksys_5G 5555500000 wpa3-only
  wpaconf <SSID> <PW> [open | wpa3-only]
  swqa | swrd | swrd2
EOHELP
}

_pri_opts="$(getopt -l "help,nfsmount::" -- hm:: "$@")" || exit 1

eval set -- "$_pri_opts"

opt_nfsmount=
while [ -n "$1" ]; do
  _lo_opt1=$1
  shift

  case "$_lo_opt1" in
  -h|--help)
    show_help
    exit 1
    ;;
  -m|--nfsmount)
    opt_nfsmount=${1:-1}
    shift
    ;;
  --)
    break
    ;;
  esac
done

if [ -n "$opt_nfsmount" ]; then
  case "$opt_nfsmount" in
  0)
    devsync_nfsumount "$(get_nfsroot)/02_exdev"
    devsync_nfsumount "$(get_nfsroot)/02_exdev2"
    devsync_nfsumount "$(get_nfsroot)/02_dev"
    devsync_nfsumount "$(get_nfsroot)/dw"
    ;;
  *)
    devsync_nfsmount || exit
    devsync_nfsmount "/mnt/dev/02_exdev2" || exit
    devsync_nfsmount "/home/joelai/Downloads" "$_pri_nfsdw" || exit
    ;;
  esac
fi

while [ -n "$1" ]; do
  _lo_opt1=$1
  shift

  case "$_lo_opt1" in
  call)
    "$@"
    ;;
  devsync|devsync.sh)
    devsync_nfsmount || exit
    nfsget_x "${_pri_nfsiq9ws}/devsync.sh"
    ;;
  busybox)
    devsync_nfsmount || exit
    nfsget_x "${_pri_nfsalgaews}/build/busybox-aarch64/busybox"
    ;;
  scp)
    devsync_scp "$@"
    exit
    ;;
  esac
done

[ -n "$_pri_listok" ] && echo && echo "Ok: " && ls -l $_pri_listok
[ -n "$_pri_listfailed" ] && {
  echo && echo "Failed:" && ls -l $_pri_listfailed
}
