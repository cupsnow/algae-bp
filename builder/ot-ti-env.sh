#!/bin/bash
set -euo pipefail

TOP=$(cd -- "$(dirname -- "$0")/.." && pwd)
PYVENVDIR=${PYVENVDIR:-$TOP/.venv}

log_d() {
  echo "[Debug] $*"
}

log_e() {
  echo "[ERROR] $*"
}

setenv() {
  export GCC_ARMCOMPILER="$(realpath $HOME/07_sw/pkg/gcc-arm-none-eabi-9-2020-q2-update)"
  export PATH="$GCC_ARMCOMPILER/bin:$PATH"

  arm-none-eabi-gcc --version

  export TI_SYSCONFIG_ROOT="$HOME/ti/sysconfig_1.22.0"

  "$TI_SYSCONFIG_ROOT/sysconfig_cli.sh" --version
}

venv() {
  [ -f "$PYVENVDIR/bin/activate" ] && [ -x "$PYVENVDIR/bin/python3" ] || {
    log_e "missing venv"
    return 1
  }
  . "$PYVENVDIR/bin/activate"
}

build_ot_ti() {
  otti_DIR="$TOP/package/ot-ti"

  cd "$otti_DIR"

  set -o pipefail

  venv && ./script/build LP_CC1352P7_4 \
      -DOT_APP_RCP=ON \
      -DOT_RCP=ON \
      -DOT_APP_CLI=OFF \
      -DOT_FTD=OFF \
      -DOT_MTD=OFF \
      -DOT_PING_SENDER=OFF \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      2>&1 | tee build-rcp.log

  arm-none-eabi-size build/bin/ot-rcp.out
  arm-none-eabi-readelf -h build/bin/ot-rcp.out
  # Preserve all loadable flash sections, including CCFG. Erased flash is
  # 0xff; fill the gap between the application and CCFG accordingly.
  arm-none-eabi-objcopy -O binary --gap-fill=0xff \
      build/bin/ot-rcp.out build/bin/ot-rcp.bin
  ls -lh build/bin/
  echo "Programming binary (flash address 0x00000000): $otti_DIR/build/bin/ot-rcp.bin"
}

all() {
  setenv
  build_ot_ti
}

show_help() {
cat <<-EOH
USAGE: $(basename $0) [OPTIONS]

OPTIONS:
  help
  all           Build RCP and generate ot-rcp.bin for programming
  setenv
  build_ot_ti   Build RCP and generate ot-rcp.bin for programming

VARIABLES:
TOP=$TOP
PYVENVDIR=$PYVENVDIR

EOH
}

case "${1:-help}" in
help|-h|--help)
  show_help
  exit 0
  ;;
setenv|all)
  "$@"
  ;;
print_*)
  _lo_varname=${1##print_}
  log_d "_lo_varname: $_lo_varname"
  _lo_val=$(eval echo "\$$_lo_varname")
  echo "$_lo_varname=$_lo_val"
  ;;
print2_*)
  setenv
  _lo_varname=${1##print2_}
  log_d "_lo_varname: $_lo_varname"
  _lo_val=$(eval echo "\$$_lo_varname")
  echo "$_lo_varname=$_lo_val"
  ;;
build_ot_ti)
  setenv
  "$@"
  ;;
*)
  show_help
  exit 2
  ;;
esac
