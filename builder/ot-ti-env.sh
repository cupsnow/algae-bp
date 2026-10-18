
setenv() {
  export GCC_ARMCOMPILER="$(realpath $HOME/07_sw/pkg/gcc-arm-none-eabi-9-2020-q2-update)"
  export PATH="$GCC_ARMCOMPILER/bin:$PATH"

  arm-none-eabi-gcc --version

  export TI_SYSCONFIG_ROOT="$HOME/ti/sysconfig_1.22.0"

  "$TI_SYSCONFIG_ROOT/sysconfig_cli.sh" --version
}

build_ot_ti() {
  otti_DIR="/home/joelai/02_dev/algae-ws/algae-bp/package/ot-ti"

  cd "$otti_DIR"

  set -o pipefail

  ./script/build LP_CC1352P7_4 \
      -DOT_APP_RCP=ON \
      -DOT_RCP=ON \
      -DOT_APP_CLI=OFF \
      -DOT_FTD=OFF \
      -DOT_MTD=OFF \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5
      2>&1 | tee build-rcp.log

  ls -lh build/bin/
  arm-none-eabi-size build/bin/ot-rcp.out
  arm-none-eabi-readelf -h build/bin/ot-rcp.out
}

show_help() {
cat <<-EOH
USAGE: $(basename $0) [OPTIONS]

OPTIONS:
  help
  setenv
  build_ot_ti

EOH
}

case "$1" in
-h|--help)
  show_help
  return 1
  ;;
setenv)
  $@
  return
  ;;
build_ot_ti)
  setenv
  $@
  return
  ;;
esac
