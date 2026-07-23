#!/bin/sh
# shellcheck disable=SC2120,SC2004
# shellcheck disable=SC2164,SC2034

# killall -sigint dexatek_main
# /usrdata/devsync.sh --applet=wifi_conn DK_SWRD_Test_5G 00000@55555 wpa3-only
# mkdir -p /run/lavender5/02_exdev2 /run/lavender5/dw
# mount -o nolock 192.168.234.16:/mnt/dev/02_exdev2 /run/lavender5/02_exdev2
# mount -o nolock 192.168.50.123:/mnt/dev/02_exdev2 /run/lavender5/02_exdev2
# mount -o nolock 192.168.50.123:/home/joelai/Downloads /run/lavender5/dw

# cp /run/lavender5/02_exdev2/agt-ws/dkmapi-ws/builder/devsync.sh .
# cp /run/lavender5/02_exdev2/agt-ws/augentix-platform-application/application_dexatek/dexatek/main_application/dexatek_main /usr/dexatek/dexatek_main
# cp /run/lavender5/02_exdev2/agt-ws/augentix-platform-application/SA7586_OTA_v6.97.5.54.swu /tmp/

# cp /run/lavender5/02_exdev2/agt-ws/esh-ws/builder/devsync.sh .
# cp /run/lavender5/02_exdev2/agt-ws/esghub/application_dexatek/dexatek/main_application/dexatek_main /usr/dexatek/dexatek_main
# cp /run/lavender5/02_exdev2/agt-ws/esghub-lc/SA7586_OTA_v6.99.5.63.swu  /tmp/

# vi wpasup.conf
# country=US
# ctrl_interface=/var/run/wpa_supplicant
# update_config=1
# network={
#   scan_ssid=1
#   ssid="DK_SWRD_Test_5G"
#   ieee80211w=2
#   key_mgmt=SAE
#   psk="00000@55555"
# }
# wpa_cli terminate
# wpa_supplicant -Dnl80211 -iwlan0 -c wpasup.conf -B
# udhcpc -i wlan0 -q

self=$0
selfdir="$(cd "$(dirname "$self")"; pwd)"

_pri_mount="busybox mount"
_pri_umount="busybox umount"
_pri_dd_args2="conv=fdatasync status=progress iflag=nonblock oflag=nonblock"

ts_uptime() {
  echo "$(</proc/uptime awk '{ print $1 }') * 1000 / 1" | bc
}

ts_dt() {
  date "+%y-%m-%d %H:%M:%S"
}

log_d() {
  echo "[$(ts_dt)][Debug]${_pri_tag:+"[$_pri_tag]"} $*"
}

log_e() {
  echo "[$(ts_dt)][ERROR]${_pri_tag:+"[$_pri_tag]"} $*"
}

cmd_run() {
  log_d "Execute: $*"
  "$@"
}

logval() {
  [ "$#" -ge 1 ] || { log_e "logval: missing arguments $#"; return 1; }
  _lo_file=${1}
  _lo_inc=${2:-0}
  [ -f ${_lo_file} ] || echo 0 > ${_lo_file}
  _lo_cnt=$(cat ${_lo_file})
  if [ "${_lo_inc}" -ne 0 ]; then
    _lo_cnt=$(( _lo_cnt + _lo_inc ))
    echo ${_lo_cnt} > ${_lo_file}
  fi
  echo ${_lo_cnt}
}

daemon_sh_start() {
  [ "$#" -ge 2 ] || { log_e "logval: missing arguments $#"; return 1; }
  _lo_pid_file=$1
  shift
  start-stop-daemon -S -b -m -p ${_lo_pid_file} \
    -x /bin/sh -- -c "exec $* >$(tty) 2>&1"
}

daemon_sh_stop() {
  [ "$#" -ge 1 ] || { log_e "logval: missing arguments $#"; return 1; }
  _lo_pid_file=$1
  start-stop-daemon -K -p ${_lo_pid_file}
}

_pri_listok=""
_pri_listfailed=""

get_ip () {
  if [ -n "$_pri_ip" ]; then
    echo "${_pri_ip}"
    return 0
  fi
  if [ "$_pri_devsite" = "mbp_ub22" ]; then
    _lo_iptest="192.168.234.86 192.168.50.42"
  else
    _lo_iptest="192.168.16.6"
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
  for i in /media /mnt /run /var/run /tmp; do
    if [ -d "$i" ]; then
      _pri_nfsroot="${i}"
      echo "${_pri_nfsroot}"
      return 0
    fi
  done
  return 1
}

_pri_nfsdw="$(get_nfsroot)/dw"
_pri_nfsalgaews="$(get_nfsroot)/02_dev/algae-ws"
_pri_nfsalgaebp="${_pri_nfsalgaews}/algae-bp"

duty1k_num() {
  [ "$#" -ge 2 ] || { log_e "Invalid arguments"; return 1; }
  _lo_period=$1
  [ $_lo_period -ge 1000 ] || { log_e "Too low period"; return 1; }
  _lo_duty1k=$2
  echo "$(( $_lo_duty1k * $_lo_period / 1000 ))"
}

usb_find() {
  # log_d "\$1: $1"
  _lo_list2=$(find -L /sys/bus/usb/devices/ -maxdepth 2 -iname idVendor | sed -n "/\/sys\/bus\/usb\/devices\/[0-9.-]\+\/idVendor/p")
  for i in ${_lo_list2}; do
    _lo_path2=$(dirname ${i})
    _lo_bus=$(basename ${_lo_path2})
    _lo_vid=$(cat ${_lo_path2}/idVendor)
    _lo_pid=$(cat ${_lo_path2}/idProduct)
    # log_d "${_lo_bus} ${_lo_vid}:${_lo_pid}"
    if [ "${_lo_vid}:${_lo_pid}" = "$1" ]; then
      echo "${_lo_bus}"
      return 0
    fi
  done
  return 1
}

countdown() {
  _lo_cdt=${1:-5}
  shift
  log_d "${*:-countdown }in ${_lo_cdt}"
  for i in $(seq $_lo_cdt); do
    sleep 1
    log_d "${*:-countdown }in $(( _lo_cdt - i ))"
  done
}

# shellcheck disable=SC2120
wpa_state() {
  _lo_st=$(wpa_cli ${1:+-i${1}} status 2>/dev/null \
    | sed -n "s/^wpa_state\s*=\s*\(.*\)/\1/p")
  echo "$_lo_st"
  test "$_lo_st" = "COMPLETED"
}

wpa_wait() {
  _lo_ct=${1:-3}
  while ! _lo_st="$(wpa_state)" && [ "$_lo_ct" -gt 0 ]; do
    log_d "wait wpa complete in $_lo_ct"
    sleep 1
    _lo_ct="$(( $_lo_ct - 1 ))"
  done
  log_d "wpa_state: $_lo_st"
  test "$_lo_st" = "COMPLETED"
}

gen_wpa_def() {
  _lo_wpacfg="${1:-wpa_supplicant.conf}"
  _lo_country="${2:-US}"

  # shellcheck disable=SC2154
  if [ -f "$_pri_wpa_base" ]; then
    cp "$_pri_wpa_base" "$_lo_pri_wpa_conf"
    return
  fi

  cat <<EOWPADEF > "$_lo_wpacfg"
country=${_lo_country}
ctrl_interface=/var/run/wpa_supplicant
update_config=1
EOWPADEF
}

gen_wpa_conf() {
  _lo_netcfg=${1:-wpa_network.txt}
  _lo_ssid=$2
  _lo_pw=$3
  _lo_auth=$4
  _lo_psk=

  if [ -n "${_lo_pw}" ]; then
    _lo_psk="$(wpa_passphrase "${_lo_ssid}" "${_lo_pw}")" || { log_e "Failed compose psk"; return 1; }
    _lo_psk="$(echo "$_lo_psk" | sed -n 's/^\s*psk=\(.*\)$/\1/p')"
  fi

  {
    echo "network={"
    echo "  scan_ssid=1"
    echo "  ssid=\"$_lo_ssid\""
    if [ "$_lo_auth" = "open" ]; then
      echo "  key_mgmt=NONE"
    elif [ "$_lo_auth" = "wpa3_only" ] || [ "$_lo_auth" = "wpa3-only" ]; then
      echo "  ieee80211w=2"
      echo "  key_mgmt=SAE"
      [ -n "${_lo_pw}" ] && echo "  psk=\"$_lo_pw\""
    elif [ -n "${_lo_pw}" ]; then
      echo "  psk=$_lo_psk"
    fi
    echo "}"
  } > "${_lo_netcfg}"
}

wpa_cmd() {
  _lo_st="$(wpa_cli "$@" 2>/dev/null)" || return 1
  # log_d "Execute wpa_cli $* -> $_lo_st"
  echo "$_lo_st" | tail -n 1 | grep -i "OK" >/dev/null
}

wpa_conn() {
  _lo_ssid=$1
  _lo_pw=$2
  _lo_auth=$3
  wpa_cmd disconnect
  if ! wpa_cmd select_network 0; then
    wpa_cmd add_network
  else
    wpa_cmd disable_network 0
  fi
  wpa_cmd select_network 0 || { log_e "Failed select network 0"; return 1; }
  wpa_cmd set_network 0 ssid "\"${_lo_ssid}\"" || { log_e "Failed set ssid=\"${_lo_ssid}\""; return 1; }
  wpa_cmd set_network 0 scan_ssid 1  || { log_e "Failed set scan_ssid=1"; return 1; }
  if [ -n "${_lo_pw}" ]; then
    wpa_cmd set_network 0 psk "\"${_lo_pw}\"" || { log_e "Failed set psk"; return 1; }
  fi
  if [ "${_lo_auth}" = "open" ]; then
    wpa_cmd set_network 0 key_mgmt NONE || { log_e "Failed set key_mgmt=NONE"; return 1; }
  elif [ "${_lo_auth}" = "wpa3-only" ]; then
    wpa_cmd set_network 0 ieee80211w 2 || { log_e "Failed set ieee80211w=2"; return 1; }
    wpa_cmd set_network 0 key_mgmt SAE || { log_e "Failed set key_mgmt=SAE"; return 1; }
  fi
}

do_ifce_down() {
  for i in "$@"; do
    cmd_run ip a flush dev "$i"
    cmd_run ip l set dev "$i" down
  done
}

do_ifce_up() {
  for i in "$@"; do
    cmd_run ip l set dev "$i" up
  done
}

wifi_conn() {
  _lo_opt_cli=${1}
  _lo_opt_ssid=${2}
  _lo_opt_pw=${3}
  _lo_opt_auth=${4}

  _lo_wpacfg="${_pri_wpa_conf:-wpa_supplicant.conf}"
  _lo_netcfg="wpa_network.txt"

  _lo_wpasup=
  # shellcheck disable=SC2043
  for i in "./wpa_supplicant"; do
    if [ -x "$i" ]; then
      _lo_wpasup="${i}"
      break
    fi
  done
  [ -z "$_lo_wpasup" ] && _lo_wpasup="wpa_supplicant"

  cmd_run eval "killall -9 wpa_supplicant udhcpc >/dev/null 2>&1"
  
  do_ifce_down wlan0
  do_ifce_up wlan0

  gen_wpa_def "${_lo_wpacfg}" || { log_e "Failed generate $_lo_wpacfg"; return 1; }

  cmd_run ${_lo_wpasup} -Dnl80211 -iwlan0 "-c${_lo_wpacfg}" -B
  if [ -n "$_lo_opt_cli" ]; then
    sleep 0.1
    wpa_conn "$_lo_opt_ssid" "$_lo_opt_pw" "$_lo_opt_auth" || { log_e "Failed connect wifi"; return 1; }
    wpa_cmd disable_network 0
    wpa_cmd enable_network 0 || { log_e "Failed enable network"; return 1; }
  else
    gen_wpa_conf "$_lo_netcfg" "$_lo_opt_ssid" "$_lo_opt_pw" "$_lo_opt_auth" || { log_e "Failed generate $_lo_netcfg"; return 1; }
    cat "$_lo_netcfg" >> "$_lo_wpacfg"
    wpa_cmd reconfigure || { log_e "Failed reconfigure network"; return 1; }
  fi

  wpa_wait 15 || { log_e "Failed connect $_lo_opt_ssid"; return 1; }
  udhcpc -i wlan0 -q || { log_e "Failed dhcp"; return 1; }
}

wpa_conf() {
  _lo_opt_ssid=${1}
  _lo_opt_pw=${2}
  _lo_opt_auth=${3}

  _lo_wpacfg="${_pri_wpa_conf:-wpa_supplicant.conf}"
  _lo_netcfg="wpa_network.txt"

  gen_wpa_def "${_lo_wpacfg}" || { log_e "Failed generate $_lo_wpacfg"; return 1; }
  if [ -n "${_lo_opt_ssid}" ]; then
    gen_wpa_conf "$_lo_netcfg" "$_lo_opt_ssid" "$_lo_opt_pw" "$_lo_opt_auth" || { log_e "Failed generate $_lo_netcfg"; return 1; }
    cat "$_lo_netcfg" >> "$_lo_wpacfg"
  fi
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

gpio_out() {
  [ "$#" -ge 2 ] || { log_e "Invalid arguments"; return 1; }
  _lo_port=$1
  _lo_val=$2
  _lo_iof0=/sys/class/gpio
  _lo_iof=${_lo_iof0}/gpio${_lo_port}
  echo "$_lo_val" >"${_lo_iof}/value"
}

# <port> <in|out> [value]
gpio_init() {
  [ "$#" -ge 2 ] || { log_e "Invalid arguments"; return 1; }
  _lo_port=$1
  _lo_dir=$2
  [ "$_lo_dir" = "in" ] || [ "$_lo_dir" = "out" ] || { log_e "Invalid GPIO direction $_lo_dir"; return 1; }
  _lo_iof0=/sys/class/gpio
  _lo_iof=${_lo_iof0}/gpio${_lo_port}
  if [ ! -d "$_lo_iof" ]; then
    echo "$_lo_port" > "${_lo_iof0}/export"
    [ -d "$_lo_iof" ] || { log_e "Failed export GPIO${_lo_port}"; return 1; }
  fi

  echo "$_lo_dir" > "${_lo_iof}/direction"

  [ -n "$3" ] && [ "$_lo_dir" = "out" ] && gpio_out "$_lo_port" "$3"
}

pwm_en() {
  [ "$#" -ge 2 ] || { log_e "Invalid arguments"; return 1; }
  _lo_port=$1
  _lo_en=$2
  _lo_iof0=/sys/class/pwm/pwmchip0
  _lo_iof=${_lo_iof0}/pwm${_lo_port}
  echo "$_lo_en" > "${_lo_iof}/enable"
}

pwm_out() {
  [ "$#" -ge 2 ] || { log_e "Invalid arguments"; return 1; }
  _lo_port=$1
  _lo_val=$2
  _lo_iof0=/sys/class/pwm/pwmchip0
  _lo_iof=${_lo_iof0}/pwm${_lo_port}
  echo "$_lo_val" > "${_lo_iof}/duty_cycle"
}

# <port> <period> <duty>
pwm_init() {
  [ "$#" -ge 2 ] || { log_e "Invalid arguments"; return 1; }
  _lo_port=$1
  _lo_period=$2
  _lo_iof0=/sys/class/pwm/pwmchip0
  _lo_iof=${_lo_iof0}/pwm${_lo_port}
  if [ ! -d "$_lo_iof" ]; then
    echo "$_lo_port" > "${_lo_iof0}/export"
    [ -d "$_lo_iof" ] || { log_e "Failed export PWM${_lo_port}"; return 1; }
  fi

  echo "$_lo_period" > "${_lo_iof}/period"

  [ -n "$3" ] && pwm_out "$_lo_port" "$3"
}

boot_tiboot3() {
  _lo_tiboot3=${1:-tiboot3.bin}
  [ -f "${_lo_tiboot3}" ] || { log_e "Miss ${_lo_tiboot3}"; return 1; }

  # Enable Boot0 boot
  cmd_run mmc bootpart enable 1 1 /dev/mmcblk0 || { log_e "Failed"; return 1; }
  cmd_run mmc bootbus set single_backward x1 x8 /dev/mmcblk0 || { log_e "Failed"; return 1; }
  # cmd_run mmc hwreset enable /dev/mmcblk0 || { log_e "Failed"; return 1; }

  # Clear eMMC boot0
  cmd_run eval "echo 0 >> /sys/class/block/mmcblk0boot0/force_ro" || { log_e "Failed"; return 1; }
  cmd_run eval "dd if=/dev/zero of=/dev/mmcblk0boot0 count=1 bs=4M ${_pri_dd_args2}" || { log_e "Failed"; return 1; }
  # Write tiboot3.bin
  cmd_run eval "dd if=${_lo_tiboot3} of=/dev/mmcblk0boot0 bs=4M ${_pri_dd_args2}" || { log_e "Failed"; return 1; }
}

do_insmod() {
  [ -n "$1" ] || { log_e "do_insmod invalid parameter"; return 1; }
  _lo_modname=$(basename $1)
  _lo_modname="$(echo "${_lo_modname%.*}" | tr '-' '_')"
  if [ -e "/sys/module/${_lo_modname}" ]; then
    log_d "module already loaded: ${_lo_modname}, skip $*"
  elif cmd_run insmod $*; then
    log_d "module loaded: $*"
  else
    return 1
  fi
}

add_ok() {
  rm_list _pri_listfailed "$@"
  add_list _pri_listok "$@"
}

add_failed() {
  rm_list _pri_listok "$@"
  add_list _pri_listfailed "$@"
}

# mount device, ie. not nfs
devmount() {
  [ "$#" -ge 1 ] || { log_e "Invalid argument"; return 1; }
  _lo_src="$1"
  _lo_tgt="${2:-/media/$(basename ${_lo_src})}"

  find_mount "*" "${_lo_tgt}" >/dev/null && { log_d "already mounted ${_lo_tgt}"; return 0; }

  [ -d "${_lo_tgt}" ] || mkdir -p "${_lo_tgt}"
  cmd_run eval "$_pri_mount \"${_lo_src}\" \"${_lo_tgt}\"" || return 1
  log_d "mounted ${_lo_tgt}"
  return 0
}

nfsumount() {
  [ "$#" -ge 1 ] || { log_e "Invalid arguments"; return 1; }
  _lo_tgt="${1:-$(get_nfsroot)/02_dev}"
  find_mount "*" "${_lo_tgt}" >/dev/null || { return 0; }
  cmd_run eval "$_pri_umount $_lo_tgt" || {
    cmd_run eval "$_pri_umount -f $_lo_tgt" || { return 1; }
    log_d "forced un-mount ${_lo_tgt}"
    return 0
  }
  log_d "un-mounted ${_lo_tgt}"
  return 0
}

nfsmount() {
  _lo_src="${1:-/home/joelai/02_dev}"
  _lo_tgt="${2:-$(get_nfsroot)/$(basename ${_lo_src})}"
  _lo_ip="${3:-$(get_ip)}" || { log_e "Failed get IP"; return 1; }

  find_mount "*" "${_lo_tgt}" >/dev/null && { log_d "already mounted ${_lo_tgt}"; return 0; }

  [ -d "${_lo_tgt}" ] || mkdir -p "${_lo_tgt}"
  cmd_run eval "$_pri_mount -o nolock \"${_lo_ip}:${_lo_src}\" \"${_lo_tgt}\"" || { log_e "Failed nfsmount"; return 1; }
  log_d "mounted ${_lo_tgt}"
  return 0
}

nfsget_n() {
  [ "$#" -ge 1 ] || { log_e "Invalid arguments"; return 1; }
  _lo_tgt="${2:-$(basename "$1")}"

  if ! cmd_run cp -dpR "$1" "${_lo_tgt}"; then
    add_failed "$_lo_tgt"
  	return 1
  fi
  add_ok "$_lo_tgt"
  return 0
}

nfsget_s() {
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

nfsget_x() {
  [ "$#" -ge 1 ] || { log_e "Invalid arguments"; return 1; }
  _lo_tgt="${2:-$(basename "$1")}"
  nfsget_n "$@" && cmd_run chmod +x "$_lo_tgt"
}

flash_tiboot3() {
  [ $# -ge 1 ] || { log_e "Invalid argument"; return 1; }

  _lo_tiboot3=$1
  [ -f "${_lo_tiboot3}" ] ||  { log_e "Miss ${_lo_tiboot3}"; return 1; }

  _lo_emmcdev=mmcblk0
  _lo_emmcdevpart=${_lo_emmcdev}boot${2:-0}

  log_d "Enable ${_lo_emmcdevpart}"
  cmd_run mmc bootpart enable 1 2 /dev/${_lo_emmcdev} \
    || { log_e "Failed"; return 1; }
  cmd_run mmc bootbus set single_backward x1 x8 /dev/${_lo_emmcdev} \
    || { log_e "Failed"; return 1; }

  # NOTE!  This is a one-time programmable (unreversible) change.
  # cmd_run mmc hwreset enable /dev/${_lo_emmcdev} || { log_e "Failed"; return 1; }

  log_d "Clearing eMMC ${_lo_emmcdevpart}"
  cmd_run eval "echo '0' >>/sys/class/block/${_lo_emmcdevpart}/force_ro" \
    || { log_e "Failed"; return 1; }
  # shellcheck disable=SC2086
  dd if=/dev/zero of=/dev/"${_lo_emmcdevpart}" count=1 bs=4M ${_pri_dd_args2} \
    || { log_e "Failed"; return 1; }

  log_d "Write tiboot3"
  # shellcheck disable=SC2086
  dd if="${_lo_tiboot3}" of=/dev/"${_lo_emmcdevpart}" bs=4M ${_pri_dd_args2} \
    || { log_e "Failed"; return 1; }
}

do_sfdisk() {
  sfdisk /dev/mmcblk0 <<-EOSFDISK || { log_e Failed; return 1; }
label:gpt
-,200M,uefi,*
-,2G,linux,-
-,2G,linux,-
-,-,linux,-
EOSFDISK
}

rootfs_cmdline() {
  sed -nE "s/.*root=\/dev\/(mmcblk[0-9]p[0-9]).*/\1/p" /proc/cmdline
}

destpkg_install() {
  if nfsget_n "${_pri_nfsalgaews}"/build/$1; then
    tar -Jxvf $1 --strip-components=1 -C /
  fi
}

gadget_cdcacm() {
  [ -d "/sys/kernel/config/usb_gadget" ] \
    || { log_e "invalid usb configfs"; return 1; }

  _lo_g1dir=/sys/kernel/config/usb_gadget/g1

  [ -d "$_lo_g1dir" ] || mkdir $_lo_g1dir || { log_e "mkdir g1"; return 1; }
  echo 0x1d6b >${_lo_g1dir}/idVendor
  echo 0x0104 >${_lo_g1dir}/idProduct

  [ -d "${_lo_g1dir}/strings/0x409" ] \
    || mkdir -p ${_lo_g1dir}/strings/0x409 \
    || { log_e "mkdir g1/strings/0x409"; return 1; }
  echo "12345678" >${_lo_g1dir}/strings/0x409/serialnumber
  echo "BeaglePlay" >${_lo_g1dir}/strings/0x409/manufacturer
  echo "USB Serial" >${_lo_g1dir}/strings/0x409/product

  [ -d "${_lo_g1dir}/configs/c.1/strings/0x409" ] \
    || mkdir -p ${_lo_g1dir}/configs/c.1/strings/0x409 \
    || { log_e "mkdir g1/configs/c.1/strings/0x409"; return 1; }
  echo "CDC ACM" >${_lo_g1dir}/configs/c.1/strings/0x409/configuration

  [ -d "${_lo_g1dir}/functions/acm.usb0" ] \
    || mkdir -p ${_lo_g1dir}/functions/acm.usb0 \
    || { log_e "mkdir g1/functions/acm.usb0"; return 1; }
  [ -e ${_lo_g1dir}/configs/c.1/acm.usb0 ] \
    && rm ${_lo_g1dir}/configs/c.1/acm.usb0
  ln -sfn ${_lo_g1dir}/functions/acm.usb0 ${_lo_g1dir}/configs/c.1/

  _lo_udc=$("ls" /sys/class/udc | head -n 1)
  [ -n "$_lo_udc" ] \
    || { log_e "empty $_lo_udc/"; return 1; }
  echo "$_lo_udc" > ${_lo_g1dir}/UDC
}

show_help() {
cat <<-EOHELP
USAGE
  ${1:-$(basename "$0")} [OPTIONS] [COMMANDS]

OPTIONS
  --help         Show this help
  -m, --nfsmount=[2]
      Mount NFS
      arg 2 will also mount 'dw'
      arg 0 will do umount
  -t, --test

COMMANDS
  wifi_conn <SSID> <PW> [open|wpa3-only]
  wpa_conf <SSID> <PW> [open|wpa3-only]
  gen_wpa_def [country]
  flash_tiboot3 <tiboot3.bin>
  flash_tispl <tispl.bin>
  flash_uboot <u-boot.img>
  destpkg_install <mesa3d-aarch64-destpkg.tar.xz>
EOHELP
}

_pri_opts="$(getopt -l "help,nfsmount::,test,applet:" -- hm::t "$@")" || exit 1

eval set -- "$_pri_opts"

opt_nfsmount=
opt_test=
opt_applet=

while [ -n "$1" ]; do
  case "$1" in
  -h|--help)
    show_help
    return 1
    ;;
  -m|--nfsmount)
    opt_nfsmount=${2:-1}
    shift 2
    ;;
  --)
    shift
    break
    ;;
  esac
done

if [ -n "$opt_test" ]; then
  do_test=$1
  shift
  ${do_test} "$@"
  return
fi

if [ -n "$opt_nfsmount" ]; then
  case "$opt_nfsmount" in
  0)
    nfsumount ${_pri_nfsdw}
    nfsumount $(get_nfsroot)/02_dev
    nfsumount $(get_nfsroot)/02_exdev
    for i in ${_pri_nfsdw} /media/mmcblk0p1 /media/mmcblk0p2 /media/mmcblk1p1; do 
      nfsumount "$i" || exit
    done
    ;;
  2)
    nfsmount || exit
    nfsmount /home/joelai/Downloads "${_pri_nfsdw}"  || exit
    for i in /dev/mmcblk0p1 /dev/mmcblk0p2 /dev/mmcblk1p1; do 
      devmount $i || exit
    done
    ;;
  *)
    nfsmount || exit
    ;;
  esac
fi

log_d "args: $*"

while test -n "$1"; do
  opt1="$1"
  shift
  
  # generic
  case "$opt1" in
  test)
    cmd_run "do_${opt1}" "${opt1}" "$@"
    exit
    ;;
  wifi_conn)
    cmd_run "$opt1" "" "$@"
    exit
    ;;
  wpa_conf)
    cmd_run "$opt1" "$@"
    exit
    ;;
  esac

  # bp only
  case "$opt1" in
  flash_tiboot3|flash_tispl|flash_uboot)
    cmd_run "$opt1" "$@"
    exit
    ;;
  uenv)
    nfsmount || exit
    devmount /dev/mmcblk0p1 || exit

    cmd_run cp -Hv "${_pri_nfsalgaebp}/build/uboot-bp-a53-emmc.env" \
        /media/mmcblk0p1/uboot.env \
      && cmd_run cp -Hv "${_pri_nfsalgaebp}/build/uboot-bp-a53-emmc.env" \
        /media/mmcblk0p1/uboot-redund.env \
      || { log_e "Failed"; exit 1; }

    sync; sync
    exit
    ;;
  sfdisk)
    shift
    do_sfdisk
    exit
    ;;
  spi|spioff)
    if [ "${opt1#spi}" = "off" ]; then
      for i in spi-omap2-mcspi spidev; do
        modprobe -r $i
      done
      exit
    fi
    for i in spi-omap2-mcspi spidev; do
      modprobe  $i
    done
    exit
    ;;
  gpio_init|gpio_out)
    ${opt1} $*
    exit
    ;;
  sh)
    nfsmount || exit
    cmd_run cp -Hv ${_pri_nfsalgaebp}/prebuilt/bp/common/etc/init.d/imx219 /etc/init.d/ \
      || { log_e "Failed"; exit 1; }
    exit
    ;;
  v4l2dump)
    nfsmount || exit
    nfsmount /home/joelai/Downloads "${_pri_nfsdw}"  || exit
    _lo_destdir=v4l2dump
    mkdir -p $_lo_destdir

    _lo_dst=${_lo_destdir}/media_ctl-topology.txt
    rm -rf ${_lo_dst}
    script -c "media-ctl -p" ${_lo_dst}

    _lo_dst=${_lo_destdir}/v4l2_ctl-links.txt
    rm -rf ${_lo_dst}
    script -c "v4l2-ctl -d /dev/video0 --all" ${_lo_dst}

    _lo_dst=${_lo_destdir}/v4l2_ctl-fmts.txt
    rm -rf ${_lo_dst}
    script -c "v4l2-ctl -d /dev/video0 --list-formats-ext" ${_lo_dst}

    exit
    ;;
  v4l2rg10)
    cmd_run media-ctl -d /dev/media0 --set-v4l2 '"imx219 4-0010":0[fmt:SRGGB10_1X10/3280x2464]' || exit
    cmd_run media-ctl -d /dev/media0 --set-v4l2 '"cdns_csi2rx.30101000.csi-bridge":0[fmt:SRGGB10_1X10/3280x2464]' || exit
    cmd_run v4l2-ctl -d /dev/video0 --set-fmt-video=width=3280,height=2464,pixelformat=RG10 || exit
    ;;
  bl|bl[2-3])
    nfsmount || exit
    devmount /dev/mmcblk0p1 || exit

    _lo_bl_num="$(( ${opt1#bl} ))"

    # tispl.bin, uboot.env, u-boot.img, uboot-redund.env
    cmd_run cp -Hv "${_pri_nfsalgaebp}"/destdir/bp/boot_emmc/* \
        "/media/mmcblk0p1/" \
      || { log_e "Failed"; exit 1; }

    # bl3 -> tiboot3
    if [ "${_lo_bl_num}" -ge 3 ]; then
      flash_tiboot3 "${_pri_nfsalgaebp}"/destdir/bp/boot/tiboot3.bin \
        || { log_e "Failed"; exit 1; }
    fi
    sync; sync
    exit
    ;;
  otakernel)
    nfsmount || exit
    devmount /dev/mmcblk0p1 || exit
    # kernel, dtb
    cmd_run cp -Hv "${_pri_nfsalgaebp}"/destdir/bp/boot/Image.gz \
        "${_pri_nfsalgaebp}"/destdir/bp/boot/linux.itb \
        "${_pri_nfsalgaebp}"/destdir/bp/boot/k3-am625-beagleplay.dtb \
        "/media/mmcblk0p1/" \
      || { log_e "Failed"; exit 1; }

    # dt-overlay
    cmd_run cp -Hv \
        "${_pri_nfsalgaebp}"/destdir/bp/boot/k3-am625-beagleplay-csi2-imx219.dtbo \
        "/media/mmcblk0p1/" \
      || { log_e "Failed"; exit 1; }
    sync; sync
    exit
    ;;
  ota|ota[2-3]|"ota?")
    nfsmount || exit
    devmount /dev/mmcblk0p1 || exit

    # _pri_uenv_txt="uenv.txt"
    _pri_uenv_txt="/media/mmcblk0p1/uenv.txt"
    _pri_uenv_bootset="$(( $(sed -n "s/^[[:space:]]*bootset=\(\d*\)/\1/p" ${_pri_uenv_txt} 2>/dev/null) ))"
    if [ "$opt1" = "ota" ]; then
      _lo_ota_num="0"
    else
      # number or ?
      _lo_ota_num="${opt1#ota}"
    fi
    _lo_curr_rootfs="$(rootfs_cmdline)"

    # boot from sdcard mmcblk1p2 -> _lo_curr_bootset=0
    # boot from emmc mmcblk0p2 -> _lo_curr_bootset=2
    # boot from emmc mmcblk0p3 -> _lo_curr_bootset=3
    _lo_curr_bootset="$(( ${_lo_curr_rootfs#mmcblk0p} ))"

    log_d "_pri_uenv_bootset: ${_pri_uenv_bootset}"
    log_d "_lo_ota_num: ${_lo_ota_num}"
    log_d "_lo_curr_rootfs: ${_lo_curr_rootfs}"
    log_d "_lo_curr_bootset: ${_lo_curr_bootset}"

    # decision priority: cli -> curr boot from emmc -> uenv_bootset
    if test [ "${_lo_ota_num}" -ge "2" ]>/dev/null 2>&1; then
      # if _lo_ota_num == "?" -> above test will failed with error message
      _lo_tgt_bootset=${_lo_ota_num}
    elif [ "${_lo_curr_bootset}" -eq 2 ]; then
      _lo_tgt_bootset=3
    elif [ "${_lo_curr_bootset}" -ge 3 ]; then
      # if _lo_curr_bootset == 0 -> curr boot from sdcard -> above test will failed
      _lo_tgt_bootset=2
    elif [ "${_pri_uenv_bootset}" -eq 2 ]; then
      _lo_tgt_bootset=3
    else
      _lo_tgt_bootset=2
    fi
    log_d "ota target emmc: mmcblk0p${_lo_tgt_bootset}"

    # show info only
    [ "$_lo_ota_num" == "?" ] && exit

    # kernel, dtb
    cmd_run cp -Hv "${_pri_nfsalgaebp}"/destdir/bp/boot/Image.gz \
        "${_pri_nfsalgaebp}"/destdir/bp/boot/linux.itb \
        "${_pri_nfsalgaebp}"/destdir/bp/boot/k3-am625-beagleplay.dtb \
        "/media/mmcblk0p1/" \
      || { log_e "Failed"; exit 1; }

    # dt-overlay
    cmd_run cp -Hv \
        "${_pri_nfsalgaebp}"/destdir/bp/boot/k3-am625-beagleplay-csi2-imx219.dtbo \
        "/media/mmcblk0p1/" \
      || { log_e "Failed"; exit 1; }

    # shellcheck disable=SC2086
    cmd_run dd if="${_pri_nfsalgaebp}/destdir/bp/rootfs.img" \
        of=/dev/mmcblk0p${_lo_tgt_bootset} bs=4M ${_pri_dd_args2} \
      || { log_e "Failed"; exit 1; }

    touch $_pri_uenv_txt
    if [ "$_lo_tgt_bootset" -ge 2 ]; then
        if cmd_run eval "grep '^[[:space:]]*bootset=' $_pri_uenv_txt"; then
          # existed
          cmd_run eval "sed -i -e 's/^[[:space:]]*bootset=.*$/bootset=${_lo_tgt_bootset}/' $_pri_uenv_txt"
        else
          # append
          cmd_run eval "echo 'bootset=${_lo_tgt_bootset}' >> $_pri_uenv_txt"
        fi
    fi
    sync; sync
    exit
    ;;
  dtb)
    nfsmount || exit
    devmount /dev/mmcblk0p1 || exit
    cmd_run cp -Hv \
        "${_pri_nfsalgaebp}"/destdir/bp/boot/k3-am625-beagleplay.dtb \
        "/media/mmcblk0p1/" \
      || { log_e "Failed"; exit 1; }
    exit
    ;;
  dtbo)
    nfsmount || exit
    devmount /dev/mmcblk0p1 || exit
    cmd_run cp -Hv \
        "${_pri_nfsalgaebp}"/destdir/bp/boot/k3-am625-beagleplay-csi2-imx219.dtbo \
        "/media/mmcblk0p1/" \
      || { log_e "Failed"; exit 1; }
    exit
    ;;
  gadget_cdcacm)
    gadget_cdcacm
    exit
    ;;
  gdb|gdbserver)
    nfsmount || exit
    # "${_pri_nfsalgaebp}/tool/gcc-arm/arm-none-linux-gnueabihf/libc/usr/bin/gdbserver"
    cmd_run cp -Hv "${_pri_nfsalgaebp}/tool/gdbserver" /usr/bin/
    ;;
  destpkg_install)
    nfsmount || exit
    for i in $*; do
      destpkg_install $i
    done
    exit
    ;;
  esac

  case "$opt1" in
  devsync|devsync.sh|$(basename "$0")|$(basename -s .sh "$0"))
    nfsmount || exit
    nfsget_s "${_pri_nfsalgaebp}/builder/$(basename "$0")"
    ;;
  locale)
    nfsmount || exit
    if nfsget_n "${_pri_nfsalgaebp}/build/locale-aarch64-destpkg.tar.xz"; then
      rm -rf /usr/share/i18n/i18n/charmaps/
      tar -Jxvf locale-aarch64-destpkg.tar.xz --strip-components=1 -C /
    fi
    ;;
  openocd)
    nfsget_n "${_pri_nfsalgaebp}"/builder/bpgpioswd.cfg
    ;;
  mesa3d)
    nfsmount || exit
    destpkg_install mesa3d-aarch64-destpkg.tar.xz
    ;;
  tester1)
    nfsmount || exit
    _lo_tgt=""
    _lo_tgt="${_lo_tgt} tester_ev3 tester_fb2"
    for i in ${_lo_tgt}; do
      nfsget_x "${_pri_nfsalgaebp}"/build/${i}-aarch64/${i}
    done
    nfsget_n "${_pri_nfsalgaebp}"/docs/sample1.bmp
    ;;
  sh|sh[2-3])
    nfsmount || exit
    lo_tgt="etc/init.d/func_involved etc/init.d/eth etc/init.d/wifi"
    lo_tgt="${lo_tgt} etc/init.d/persist etc/init.d/syslogd-initd"
    lo_tgt="${lo_tgt} etc/init.d/sshd-initd"
    lo_tgt="${lo_tgt} etc/init.d/cputemp"
    lo_tgt="${lo_tgt} etc/init.d/tidss"
    lo_tgt="${lo_tgt} usr/share/udhcpc/default.script"
    lo_tgt="${lo_tgt} usr/share/zcip/default.script"
    for i in $lo_tgt; do
      if [ -f "${_pri_nfsalgaebp}"/prebuilt/bp/common/"${i}" ]; then
        nfsget_x "${_pri_nfsalgaebp}"/prebuilt/bp/common/"${i}" /"${i}"
      else
        nfsget_x "${_pri_nfsalgaebp}"/prebuilt/common/"${i}" /"${i}"
      fi
    done

    lo_tgt="etc/skel/.profile"
    lo_tgt="${lo_tgt}"
    for i in $lo_tgt; do
      if [ -f "${_pri_nfsalgaebp}"/prebuilt/bp/common/"${i}" ]; then
        nfsget_n "${_pri_nfsalgaebp}"/prebuilt/bp/common/"${i}" /"${i}"
      else
        nfsget_n "${_pri_nfsalgaebp}"/prebuilt/common/"${i}" /"${i}"
      fi
    done
    ;;
  esac
done  

# if [ -n "$_pri_listfailed" ]; then
#   log_d "Failed list:"
#   _lo_cnt=0
#   for i in $_pri_listfailed; do
#     _lo_cnt=$(( $_lo_cnt + 1 ))
#     log_d "Failed item${_lo_cnt}: $i"
#   done
#   log_d "Total failed $_lo_cnt items"
# fi
[ -n "$_pri_listok" ] && echo && echo "Ok: " && ls -l $_pri_listok
[ -n "$_pri_listfailed" ] && {
  echo && echo "Failed:" && ls -l $_pri_listfailed
}
