#!/bin/bash
SELF=${BASH_SOURCE[0]}
SELFDIR=`dirname $SELF`
# SELFDIR=`realpath -L -s $SELFDIR`
SELFDIR=`cd $SELFDIR && pwd -L`

# size of 1st partition in MB
SZ1=234

log_ts () {
  "echo" -n "`date '+%0y/%0m/%0d %0H:%0M:%0S:%0N'`"
}

log_e () {
  "echo" -e "\033[31;1m`log_ts` ERROR $*\033[0m"
}

log_d () {
  "echo" -e "\033[36;1m`log_ts` DEBUG $*\033[0m"
}

# usage: error_exit ERRNO ERRMSG
# print ERRMSG then exit when ERRNO != 0
error_exit () {
  [ "$1" = "0" ] && return 0
  log_e "$*"
  exit
}

trap "housekeeping" SIGINT SIGTERM EXIT

# enable to use temp directory
# tmpdir=`mktemp -d`

# housekeeping before exit
housekeeping () {
  # housekeeping whatever temp directory
  [ "$tmpdir" ] && [ -e $tmpdir ] && (log_d "remove $tmpdir"; rm -rf $tmpdir)

  # housekeeping more

  # done
  exit 255
}

show_help() {
local rc=$?
cat <<EOF
SYNOPSIS
  $1 [OPTIONS]

OPTIONS
  -h, --help       Show help
  -d, --dev=<DEV>  USB SD card device [<>]
  -s, --sz1=<SZ1>  Size of 1st partition in MB[<$SZ1>]

EXAMPLES
  $1 -d/dev/sdc -s77

EOF
return $rc
}

commentary() {
read -p "$*"
cat <<EOF
$*
EOF
read
}

OPT_SAVED="$*"

OPT_PARSED=`getopt -l "help,dev:,sz1:,nocommentary" -- "hd:s:n" $@` || {
  log_e "Failed parse cli args"
  exit
}

log_d "\$#: $#, getopt: $OPT_PARSED"

# re-assign positional parameter
eval set -- "$OPT_PARSED"
while true; do
  log_d "\$1: $1 \$2: $2"
  case "$1" in
  -h|--help)
    show_help $0
    shift
    exit 1
    ;;
  -d|--dev)
    DEV=$2
    shift 2
    ;;
  -s|--sz1)
    SZ1=$2
    shift 2
    ;;
  -n|--nocommentary)
    NOCOMMENTARY="y"
    shift
    ;;
  --)
    break
    ;;
  *)
    show_help $0
    exit 1
  esac
done

if [ -z "$DEV" ] || [ ! -e "$DEV" ]; then
  log_e "Miss device"
  show_help $0
  exit 1
fi

# such as ${DEV}${MMC_P}1 for /dev/mmcblk1p1 or /dev/sdc1
[ -n "`expr $DEV : '\(/dev/mmcblk[0-9]*$\)'`" ] && MMC_P="p"

log_d "DEV: $DEV, SZ1: $SZ1, SZ1 + 1: $(( $SZ1 + 1 )), MMC_P: $MMC_P"

if ! { udevadm info -q path $DEV | grep "/usb[0-9]*"; }; then
  log_e "Not usb disk"
  exit 1
fi

log_d "sudo to access '$DEV'"
sudo -k \
  sfdisk -l $DEV

if [ -z "$NOCOMMENTARY" ]; then
  echo ""
  if ! read -t 5 -p "Keep going to re-partition the device ..."; then
    log_e "Timeout"
    exit 1
  fi
fi

# FS1_FAT=fat16
case $FS1_FAT in
6|0x6|fat16)
  FS1_FATID=0x6
  FS1_FATSZ=16
  ;;
*)
  FS1_FATID=0xc
  FS1_FATSZ=32
  ;;
esac

sudo sfdisk $DEV <<EOF
1M,${SZ1}M,${FS1_FATID},*
$(( $SZ1 + 1 ))M,,,-
EOF

# for slow machine
sync
sleep 1

sudo mkfs.fat -F ${FS1_FATSZ} -n BOOT ${DEV}${MMC_P}1

sudo mkfs.ext4 -L rootfs ${DEV}${MMC_P}2

tmpdir=`mktemp -d`
sudo mount ${DEV}${MMC_P}2 $tmpdir || { log_e "Failed mount ${DEV}${MMC_P}2 to $tmpdir"; exit 1; }
sudo chmod 0777 $tmpdir || { log_e "Failed chmod 0777 to root of ${DEV}${MMC_P}2"; exit 1; }
sudo umount $tmpdir || { log_e "Failed unmount $tmpdir (${DEV}${MMC_P}2)"; exit 1; }
rm -rf $tmpdir
