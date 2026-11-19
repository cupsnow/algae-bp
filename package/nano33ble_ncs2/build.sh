#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu

app_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
: "${NCS_DIR:?Set NCS_DIR to the installed SDK workspace, containing nrf and zephyr}"
ncs_dir=$(CDPATH= cd -- "$NCS_DIR" && pwd)
test -f "$ncs_dir/nrf/west.yml" || {
    echo "Not an nRF Connect SDK workspace: $ncs_dir" >&2
    exit 1
}
build_dir=${NCS_BUILD_DIR:-"$app_dir/../../../build/nano33ncs2"}
mkdir -p "$build_dir"
build_dir=$(CDPATH= cd -- "$build_dir" && pwd)
# Run inside the selected west workspace, not the repository's upstream Zephyr.
cd "$ncs_dir"
export ZEPHYR_BASE="$ncs_dir/zephyr"
case ${1:-build} in
    configure)
        west build -d "$build_dir" -b arduino_nano_33_ble \
            --sysbuild --cmake-only -p always "$app_dir"
        ;;
    build)
        west build -d "$build_dir" -b arduino_nano_33_ble \
            --sysbuild "$app_dir"
        ;;
    *) echo "Usage: sh $0 [configure|build]" >&2; exit 2 ;;
esac
