#!/bin/sh
# shellcheck disable=SC2120
self=$0
selfdir="$(cd $(dirname $self); pwd)"

ts_up () {
  _lo_ts1="$(cat /proc/uptime | awk '{ print $1 }')"
  echo "${_lo_ts1} * 1000 / 1" | bc
}

ts_dt () {
  date "+%y-%m-%d %H:%M:%S"
}

log_d () {
  # func_rel && return
  echo "[$(ts_dt)][Debug]${_pri_tag:+"[$_pri_tag]"} $*"
}

log_e () {
  echo "[$(ts_dt)][ERROR]${_pri_tag:+"[$_pri_tag]"} $*"
}

cmd_run () {
  log_d "Execute: $*"
  "$@"
}

_pri_ip="192.168.31.16"
_pri_listok=""
_pri_listfailed=""

# _lo_iptest="192.168.12.16"
# if ! ping -c 1 -W 1 ${_pri_ip} >/dev/null 2>&1; then
#   for i in $_lo_iptest; do
#     if cmd_run eval "ping -c 1 -W 1 ${i} >/dev/null 2>&1"; then
#       _pri_ip=${i}
#       break
#     fi
#   done
# fi

[ -z "$_pri_nfsroot" ] && _pri_nfsroot="/media/nfsroot"
_pri_nfsalgaews="${_pri_nfsroot}/02_dev/algae-ws"
_pri_nfsalgaebp="${_pri_nfsalgaews}/algae-bp"
_pri_nfsdw="${_pri_nfsroot}/dw"

wpa_state () {
  _lo_st=$(wpa_cli ${1:+-i${1}} status 2>/dev/null | sed -n "s/^wpa_state\s*=\s*\(.*\)/\1/p")
  echo $_lo_st
  [ "$_lo_st" == "COMPLETED" ]
}

wpa_wait () {
  _lo_ct=${1:-3}
  while ! _lo_st="$(wpa_state)" && [ $_lo_ct -gt 0 ]; do
    log_d "wait wpa complete in $_lo_ct"
    sleep 1
    _lo_ct="$(( $_lo_ct - 1 ))"
  done
  log_d "wpa_state: $_lo_st"
  [ "$_lo_st" == "COMPLETED" ]
}

gen_wpa_def () {
  _lo_pri_wpa_conf="${1:-wpa_supplicant.conf}"

  # shellcheck disable=SC2154
  if [ -f "$_pri_wpa_base" ]; then
    cp "$_pri_wpa_base" "$_lo_pri_wpa_conf"
    return
  fi

  cat <<EOWPADEF > "$_lo_pri_wpa_conf"
country=US
ctrl_interface=/var/run/wpa_supplicant
update_config=1
EOWPADEF
}

gen_wpa_conf () {
  _lo_netcfg=${1:-wpa_network.txt}
  _lo_ssid=$2
  _lo_pw=$3
  _lo_auth=$4
  _lo_psk=

  if [ -n "${_lo_pw}" ]; then
    _lo_psk="$(wpa_passphrase "${_lo_ssid}" "${_lo_pw}")" \
      || { log_e "Failed compose psk"; return 1; }
    _lo_psk="$(echo "$_lo_psk" | sed -n 's/^\s*psk=\(.*\)$/\1/p')"
  fi

  {
    echo "network={"
    echo "  scan_ssid=1"
    echo "  ssid=\"$_lo_ssid\""
    [ -n "$_lo_psk" ] && echo "  psk=$_lo_psk"
    if [ "$_lo_auth" = "open" ]; then
      echo "  key_mgmt=NONE"
    elif [ "$_lo_auth" = "wpa3-only" ]; then
      echo "  ieee80211w=2"
      echo "  key_mgmt=SAE"
    fi
    echo "}"
  } > "${_lo_netcfg}"
}

wpa_cmd () {
  _lo_st="$(wpa_cli "$@" 2>/dev/null)" || return 1
  # log_d "Execute wpa_cli $* -> $_lo_st"
  echo "$_lo_st" | tail -n 1 | grep -i "OK" >/dev/null
}

wpa_conn () {
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

do_ifce_down () {
  for i in "$@"; do
    cmd_run ip a flush dev $i
    cmd_run ip l set dev $i down
  done
}

do_ifce_up () {
  for i in "$@"; do
    cmd_run ip l set dev $i up
  done
}

do_wifi_conn () {
  _lo_opt_cli=${1}
  _lo_opt_ssid=${2}
  _lo_opt_pw=${3}
  _lo_opt_auth=${4}

  _lo_wpacfg="${_pri_wpa_conf:-wpa_supplicant.conf}"
  _lo_netcfg="wpa_network.txt"

  cmd_run eval "killall -9 wpa_supplicant udhcpc > /dev/null 2>&1"

  do_ifce_down wlan0
  do_ifce_up wlan0

  gen_wpa_def "${_lo_wpacfg}" || { log_e "Failed generate $_lo_wpacfg"; return 1; }

  cmd_run wpa_supplicant -Dnl80211 -iwlan0 "-c${_lo_wpacfg}" -B
  if [ -n "$_lo_opt_cli" ]; then
    sleep 0.1
    wpa_conn "$_lo_opt_ssid" "$_lo_opt_pw" "$_lo_opt_auth" || { log_e "Failed connect wifi"; return 1; }
    wpa_cmd disable_network 0
    wpa_cmd enable_network 0 || { log_e "Failed enable network"; return 1; }
  else
    gen_wpa_conf "$_lo_netcfg" "$_lo_opt_ssid" "$_lo_opt_pw" "$_lo_opt_auth" || { log_e "Failed generate $_pri_network_cfg"; return 1; }
    cat "$_lo_netcfg" >> "$_lo_wpacfg"
    wpa_cmd reconfigure || { log_e "Failed reconfigure network"; return 1; }
  fi

  wpa_wait 15 || { log_e "Failed connect $_lo_opt_ssid"; return 1; }
  udhcpc -i wlan0 -q || { log_e "Failed dhcp"; return 1; }
}

do_wpa_conf () {
  _lo_opt_ssid=${1}
  _lo_opt_pw=${2}
  _lo_opt_auth=${3}

  _lo_wpacfg="${_pri_wpa_conf:-wpa_supplicant.conf}"
  _lo_netcfg="wpa_network.txt"

  gen_wpa_def "${_lo_wpacfg}" || { log_e "Failed generate $_lo_wpacfg"; return 1; }
  gen_wpa_conf "$_lo_netcfg" "$_lo_opt_ssid" "$_lo_opt_pw" "$_lo_opt_auth" || { log_e "Failed generate $_pri_network_cfg"; return 1; }
  cat "$_lo_netcfg" >> "$_lo_wpacfg"
}

find_mount () {
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
  while read _pri_line; do
    # [ $_pri_for_iter -lt $_pri_for_count ] || break
    # echo "[$_pri_for_iter]$_pri_line"

    read _pri_dev _pri_dir _pri_fs _dommy <<-EOM
$(echo $_pri_line)
EOM
    # log_d "[#$_pri_for_iter] $_pri_dev, $_pri_dir, $_pri_fs"

    local _pri_ng=
    [ -n "$_pri_ng" ] || [ "$1" = "*" ] || [ "$1" = "$_pri_dev" ] || _pri_ng=n
    [ -n "$_pri_ng" ] || [ -z "$2" ] || [ "$2" = "*" ] || [ "$2" = "$_pri_dir" ] || _pri_ng=n
    [ -n "$_pri_ng" ] || [ -z "$3" ] || [ "$3" = "*" ] || [ "$3" = "$_pri_fs" ] || _pri_ng=n
    [ -z "$_pri_ng" ] && { echo $_pri_line; return 0; }

    _pri_for_iter="$(( $_pri_for_iter + 1 ))"
  done <<-EOR
$(cat /proc/mounts)
EOR
  return 1
}

# add_list _list_name_only fn1 fn2 fn2 fn3
add_list () {
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
rm_list () {
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

gpio_out () {
  [ "$#" -ge 2 ] || { log_e "Invalid arguments"; return 1; }
  _lo_port=$1
  _lo_val=$2
  _lo_iof0=/sys/class/gpio
  _lo_iof=${_lo_iof0}/gpio${_lo_port}
  echo $_lo_val > ${_lo_iof}/value
}

# <port> <in|out> [value]
gpio_init () {
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

pwm_en () {
  [ "$#" -ge 2 ] || { log_e "Invalid arguments"; return 1; }
  _lo_port=$1
  _lo_en=$2
  _lo_iof0=/sys/class/pwm/pwmchip0
  _lo_iof=${_lo_iof0}/pwm${_lo_port}
  echo "$_lo_en" > "${_lo_iof}/enable"
}

pwm_out () {
  [ "$#" -ge 2 ] || { log_e "Invalid arguments"; return 1; }
  _lo_port=$1
  _lo_val=$2
  _lo_iof0=/sys/class/pwm/pwmchip0
  _lo_iof=${_lo_iof0}/pwm${_lo_port}
  echo "$_lo_val" > "${_lo_iof}/duty_cycle"
}

# <port> <period> <duty>
pwm_init () {
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

boot_tiboot3 () {
  _lo_tiboot3=${1:-tiboot3.bin}
  [ -f "${_lo_tiboot3}" ] || { log_e "Miss ${_lo_tiboot3}"; return 1; }

  # Enable Boot0 boot
  cmd_run mmc bootpart enable 1 1 /dev/mmcblk0 || { log_e "Failed"; return 1; }
  cmd_run mmc bootbus set single_backward x1 x8 /dev/mmcblk0 || { log_e "Failed"; return 1; }
  # cmd_run mmc hwreset enable /dev/mmcblk0 || { log_e "Failed"; return 1; }

  # Clear eMMC boot0
  cmd_run eval "echo 0 >> /sys/class/block/mmcblk0boot0/force_ro" || { log_e "Failed"; return 1; }
  cmd_run eval "dd if=/dev/zero of=/dev/mmcblk0boot0 count=32 bs=128k" || { log_e "Failed"; return 1; }
  # Write tiboot3.bin
  cmd_run eval "dd if=${_lo_tiboot3} of=/dev/mmcblk0boot0 bs=128k" || { log_e "Failed"; return 1; }
}

do_insmod () {
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

add_ok () {
  rm_list _pri_listfailed "$@"
  add_list _pri_listok "$@"
}

add_failed () {
  rm_list _pri_listok "$@"
  add_list _pri_listfailed "$@"
}

devmount () {
  [ "$#" -ge 1 ] || { log_e "Invalid argument"; return 1; }
  _lo_src="$1"
  _lo_tgt="${2:-/media/$(basename ${_lo_src})}"

  find_mount "*" "${_lo_tgt}" >/dev/null && { log_d "already mounted ${_lo_tgt}"; return 0; }

  [ -d "${_lo_tgt}" ] || mkdir -p "${_lo_tgt}"
  cmd_run eval "mount \"${_lo_src}\" \"${_lo_tgt}\"" || return 1
  log_d "mounted ${_lo_tgt}"
  return 0
}

nfsumount () {
  _lo_tgt="${1:-${_pri_nfsroot}/02_dev}"
  find_mount "*" "${_lo_tgt}" >/dev/null || { return 0; }
  cmd_run eval "umount $_lo_tgt" || {
    cmd_run eval "umount -f $_lo_tgt" || { return 1; }
    log_d "forced un-mount ${_lo_tgt}"
    return 0
  }
  log_d "un-mounted ${_lo_tgt}"
  return 0
}

nfsmount () {
  _lo_src="${1:-/home/joelai/02_dev}"
  _lo_tgt="${2:-${_pri_nfsroot}/$(basename ${_lo_src})}"
  _lo_ip="${3:-${_pri_ip}}"

  find_mount "*" "${_lo_tgt}" >/dev/null && { log_d "already mounted ${_lo_tgt}"; return 0; }

  cmd_run eval "ping -c 1 -W 1 ${_lo_ip} >/dev/null 2>&1" || { log_e "failed ping to ${_lo_ip}"; return 1; }

  [ -d "${_lo_tgt}" ] || mkdir -p "${_lo_tgt}"
  cmd_run eval "mount -o nolock \"${_lo_ip}:${_lo_src}\" \"${_lo_tgt}\"" || return 1
  log_d "mounted ${_lo_tgt}"
  return 0
}

nfsget_n () {
  [ "$#" -ge 1 ] || { log_e "Invalid arguments"; return 1; }
  _lo_tgt="${2:-$(basename "$1")}"

  if ! cmd_run cp -dpR "$1" "${_lo_tgt}"; then
    add_failed "$_lo_tgt"
  	return 1
  fi
  add_ok "$_lo_tgt"
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

show_help () {
cat <<-EOHELP
USAGE
  ${1:-$(basename $0)} [OPTIONS]

OPTIONS
  --help         Show this help
  -m, --nfsmount=[2]
      Mount NFS
      arg 2 will also mount 'dw'
      arg 0 will do umount
  -t, --test
  --applet=<APPLET>  Run applet

APPLET
  wifi_conn <SSID> <PW> [open|wpa3-only]
  wpa_conf <SSID> <PW> [open|wpa3-only]
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
    shift
    show_help
    ;;
  -m|--nfsmount)
    opt_nfsmount=${2:-1}
    shift 2
    ;;
  -t|--test)
    shift
    opt_test=1
    ;;
  --applet)
    opt_applet=${2}
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
    nfsumount || exit
    nfsumount "${_pri_nfsdw}" || exit
    ;;
  2)
    nfsmount || exit
    nfsmount /home/joelai/Downloads "${_pri_nfsdw}"  || exit
    ;;
  *)
    nfsmount || exit
    ;;
  esac
fi

log_d "args: $*"

if [ "$opt_applet" = "wifi_conn" ]; then
  cmd_run eval "do_wifi_conn 1 $@"
  exit
elif [ -n "$opt_applet" ]; then
  cmd_run eval "do_${opt_applet} $@"
  exit
fi

for opt1 in "$@"; do
  case "$opt1" in
  test1)
    false && {
      log_d "ok1"
      log_d "ok2"
    } || {
      log_d "false1"
      log_d "false2"
    }
    ;;
  "$(basename $self)")
    nfsmount || exit

    nfsget_x "${_pri_nfsalgaebp}/builder/$(basename $self)"
    ;;
  ota)
    [ -f /etc/bp.json ] || { log_e "Maybe not board bp"; return 1; }
    nfsmount || exit
    devmount /dev/mmcblk0p1 || exit
    cmd_run eval "cp -av ${_pri_nfsalgaebp}/destdir/bp/boot/* /media/mmcblk0p1/"
    # boot_tiboot3 ${_pri_nfsalgaebp}/destdir/bp/boot/tiboot3.bin
    ;;
  sh1)
    nfsmount || exit

  	tgt="etc/init.d/func_involved"
  	tgt="${tgt} etc/init.d/rcS"
  	tgt="${tgt} etc/init.d/start"
  	tgt="${tgt} etc/init.d/persist"
    for i in $tgt; do
      nfsget_x "${_pri_nfsalgaebp}/prebuilt/common/${i}" "/${i}"
    done

  	tgt="etc/init.d/refactory"
  	tgt="${tgt} etc/init.d/restmon"
  	tgt="${tgt} etc/init.d/sysemb"
    for i in $tgt; do
      nfsget_x "${_pri_nfsalgaebp}/prebuilt/bp/common/${i}" "/${i}"
    done
    ;;
  admin)
    if [ -n "$_pri_dbg" ]; then
      admin_dir="algae-bp/build/admin-bp"
    else
      admin_dir="build/admin-bp"
    fi
    nfsmount || exit 1
    nfsget_x "${_pri_nfsalgaews}/${admin_dir}/.libs/libadmin.so.0.0.0" \
      "/lib/libadmin.so.0.0.0"
    nfsget_n "${_pri_nfsalgaews}/${admin_dir}/.libs/libadmin.so.0" \
      "/lib/libadmin.so.0"
    nfsget_n "${_pri_nfsalgaews}/${admin_dir}/.libs/libadmin.so.0" \
      "/lib/libadmin.so.0"
    nfsget_n "${_pri_nfsalgaews}/${admin_dir}/.libs/libadmin.so" \
      "/lib/libadmin.so"
    nfsget_x "${_pri_nfsalgaews}/${admin_dir}/.libs/admin" \
      "/bin/admin"
    nfsget_x "${_pri_nfsalgaews}/${admin_dir}/.libs/test1"
    nfsget_x "${_pri_nfsalgaews}/${admin_dir}/.libs/testi2c1"
    nfsget_x "${_pri_nfsalgaews}/${admin_dir}/.libs/testnl1"
    nfsget_n "${_pri_nfsalgaews}/air192/package/admin/test/admin2.html" \
      "/var/www/admin2.html"
    ;;
  esac
done

if [ -n "$_pri_listfailed" ]; then
  log_d "Failed list:"
  _lo_cnt=0
  for i in $_pri_listfailed; do
    _lo_cnt=$(( $_lo_cnt + 1 ))
    log_d "Failed item${_lo_cnt}: $i"
  done
  log_d "Total failed $_lo_cnt items"
fi
