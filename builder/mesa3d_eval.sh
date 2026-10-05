#!/bin/bash

# Usage
#   ./mesa3d_eval.sh <command>
# 
# Description:
#   This script try build mesa3d from ground, support opengl, vulkan and opencl
#   Staging: qemu arm64 simulator -> beagleplay (bp) framebuffer -> bp gpu
# 
# Source:
#   LLVM 24.0.0

_lo_free_mem=$(free -g | awk '/^Mem:/ {print $7}')
if [ "$_lo_free_mem" -ge 20 ]; then
  _pri_memory_max=15G
  _pri_cpu_quota=1400%
  _pri_parallel="15"
elif [ "$_lo_free_mem" -ge 8 ]; then
  _pri_memory_max=4G
  _pri_cpu_quota=480%
  _pri_parallel="4"
else 
  _pri_memory_max=2G
  _pri_cpu_quota=190%
  _pri_parallel="2"
fi

# prevent low memeory ub26.04 out of memory (oom)
_pri_runner="systemd-run --user --scope \
  ${_pri_memory_max:+-p MemoryMax=$_pri_memory_max} \
  ${_pri_cpu_quota:+-p CPUQuota=$_pri_cpu_quota}"

log_ts() {
  date "+%y%m%d %H:%M:%S"
}

log_d() {
  _lo_ts="$(log_ts)"
  echo "${_lo_ts:+[${_lo_ts}]}[Debug] $*"
}

log_e() {
  _lo_ts="$(log_ts)"
  echo "${_lo_ts:+[${_lo_ts}]}[Error] $*" >&2
}

cmd_run() {
  # echo "================================"
  log_d "Execute: $*"
  echo ""
  "$@"
  _lo_ret=$?
  echo ""
  log_d "Exit code $_lo_ret ($1 $2 $3 ...)"
  echo ""
  return $_lo_ret
}

setenv_base() {
  export WS="${WS:-$HOME/02_dev/algae-ws}"
  export TOP="${TOP:-$WS/algae-bp}"
  export CROSS="${CROSS:-$TOP/cross/aarch64-linux-gnu}"
  export GCC_SYSROOT="${GCC_SYSROOT:-$CROSS/aarch64-linux-gnu/sysroot}"
  export BP_SYSROOT="${BP_SYSROOT:-$WS/build/sysroot-qemuarm64}"
  export LLVM_HOST="${LLVM_HOST:-$TOP/tool/llvm-host}"
  export BUILD="${BUILD:-$WS/build}"
  export SRC="${SRC:-$WS}"

  export LLVM_TARGET_STAGE="${LLVM_TARGET_STAGE:-$BUILD/llvm-aarch64-staging}"
  export SPIRV_TOOLS_TARGET_STAGE="${SPIRV_TOOLS_TARGET_STAGE:-$BUILD/spirv-tools-aarch64-staging}"
  export SPIRV_TRANSLATOR_TARGET_STAGE="${SPIRV_TRANSLATOR_TARGET_STAGE:-$BUILD/spirv-llvm-translator-aarch64-staging}"
  export MESA_LIBCLC_STAGE="${MESA_LIBCLC_STAGE:-$BUILD/mesa-libclc-staging}"
  export MESA_BUILD="${MESA_BUILD:-$BUILD/mesa-aarch64-build}"
  export MESA_STAGE="${MESA_STAGE:-$BUILD/mesa-aarch64-staging}"
  export MESA_CROSS_FILE="${MESA_CROSS_FILE:-$BUILD/mesa-aarch64.ini}"
}

_pri_envlist="CROSS_COMPILE CC CXX AR RANLIB STRIP \
    PKG_CONFIG_PATH PKG_CONFIG_SYSROOT_DIR PKG_CONFIG_LIBDIR"

setenv_host() {
  unset $_pri_envlist
}

setenv_cross() {
  unset $_pri_envlist
  export CROSS_COMPILE=$CROSS/bin/aarch64-linux-gnu-
  export CC="${CROSS_COMPILE}gcc"
  export CXX="${CROSS_COMPILE}g++"
  export AR="${CROSS_COMPILE}ar"
  export RANLIB="${CROSS_COMPILE}ranlib"
  export STRIP="${CROSS_COMPILE}strip"

  export PKG_CONFIG_SYSROOT_DIR="$BP_SYSROOT"
  export PKG_CONFIG_LIBDIR="$BP_SYSROOT/lib/pkgconfig:$BP_SYSROOT/usr/lib/pkgconfig:$BP_SYSROOT/usr/share/pkgconfig"
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || {
    log_e "Required command not found: $1"
    return 1
  }
}

require_file() {
  [ -f "$1" ] || {
    log_e "Required file not found: $1"
    return 1
  }
}

activate_venv() {
  require_file "$TOP/.venv/bin/activate" || return 1
  # shellcheck disable=SC1091
  . "$TOP/.venv/bin/activate"
}

llvm_host_defconfig() {
  [ -d "$BUILD/llvm-host-build" ] \
      || cmd_run mkdir -p "$BUILD/llvm-host-build"
  activate_venv \
      && cmd_run cmake -G Ninja \
          -S $SRC/llvm-project/llvm \
          -B "$BUILD/llvm-host-build" \
          -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_INSTALL_PREFIX="$LLVM_HOST" \
          -DLLVM_TARGETS_TO_BUILD="X86;AArch64;SPIRV" \
          -DLLVM_ENABLE_PROJECTS="clang" \
          -DLLVM_INCLUDE_TESTS=OFF \
          -DLLVM_INCLUDE_EXAMPLES=OFF \
          -DLLVM_INCLUDE_BENCHMARKS=OFF \
          -DLLVM_ENABLE_ASSERTIONS=OFF \
          -DLLVM_ENABLE_DUMP=ON || {
    log_e "Failed to configure llvm host build"
    return 1
  }
}

# build llvm host
# hint to prevent oom
# systemd-run --user --scope -p MemoryMax=4G -p CPUQuota=500% ./builder/mesa3d_eval.sh llvm_host_build -j3
llvm_host_build() {
  [ -f "$BUILD/llvm-host-build/build.ninja" ] \
      || llvm_host_defconfig || {
    log_e "Failed to configure llvm host build"
    return 1
  }

  activate_venv \
      && cmd_run $_pri_runner ninja ${_pri_parallel:+-j$_pri_parallel} -C "$BUILD/llvm-host-build" || {
    log_e "Failed to build llvm host"
    return 1
  }

  # sanity check for expecting x86_64 executable
  cmd_run eval "file $BUILD/llvm-host-build/bin/llvm-config \
        | grep \"ELF 64-bit LSB .*executable, x86-64\" >/dev/null 2>&1" || {
    log_e "Failed to check llvm host"
    return 1
  }

  cmd_run eval "file $BUILD/llvm-host-build/bin/llvm-tblgen \
        | grep \"ELF 64-bit LSB .*executable, x86-64\" >/dev/null 2>&1" || {
    log_e "Failed to check llvm host"
    return 1
  }
}

llvm_host_install() {
  [ -e "$LLVM_HOST/bin/llvm-config" ] \
      || llvm_host_build || {
    log_e "Failed to build llvm host"
    return 1
  }

  activate_venv \
      && cmd_run ninja -C "$BUILD/llvm-host-build" install || {
    log_e "Failed to install llvm host"
    return 1
  }

  cmd_run cp "$BUILD/llvm-host-build/bin/llvm-min-tblgen" \
      "$LLVM_HOST/bin/llvm-min-tblgen"

  # sanity check for expecting x86_64 executable

  # cmd_run eval "file $LLVM_HOST/bin/llvm-config | grep \"ELF 64-bit LSB .*executable, x86-64\" >/dev/null 2>&1" || {
  #   log_e "Failed to check llvm host"
  #   return 1
  # }

  # cmd_run eval "file $LLVM_HOST/bin/llvm-tblgen | grep \"ELF 64-bit LSB .*executable, x86-64\" >/dev/null 2>&1" || {
  #   log_e "Failed to check llvm host"
  #   return 1
  # }

  # cmd_run eval "file $LLVM_HOST/bin/llvm-min-tblgen | grep \"ELF 64-bit LSB .*executable, x86-64\" >/dev/null 2>&1" || {
  #   log_e "Failed to check llvm host"
  #   return 1
  # }

  # # expect output the version and info
  # cmd_run "$LLVM_HOST/bin/llvm-config" --version
  # cmd_run "$LLVM_HOST/bin/llvm-config" --host-target
  # cmd_run "$LLVM_HOST/bin/llvm-config" --targets-built
  # cmd_run "$LLVM_HOST/bin/llvm-min-tblgen" --version
}

llvm_aarch64_defconfig() {

  [ -d "$BUILD/llvm-aarch64-build" ] \
      || cmd_run mkdir -p "$BUILD/llvm-aarch64-build"
  activate_venv \
      && cmd_run cmake -G Ninja \
          -S "$SRC/llvm-project/llvm" \
          -B "$BUILD/llvm-aarch64-build" \
          -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_INSTALL_PREFIX=/usr \
          \
          -DCMAKE_C_COMPILER="$CC" \
          -DCMAKE_CXX_COMPILER="$CXX" \
          -DCMAKE_AR="$AR" \
          -DCMAKE_RANLIB="$RANLIB" \
          -DCMAKE_SYSROOT="$GCC_SYSROOT" \
          \
          -DCMAKE_C_FLAGS="-I$BP_SYSROOT/include -I$BP_SYSROOT/usr/include" \
          -DCMAKE_CXX_FLAGS="-I$BP_SYSROOT/include -I$BP_SYSROOT/usr/include" \
          -DCMAKE_EXE_LINKER_FLAGS="-L$BP_SYSROOT/lib -L$BP_SYSROOT/usr/lib -Wl,-rpath-link,$BP_SYSROOT/lib -Wl,-rpath-link,$BP_SYSROOT/usr/lib -Wl,-rpath-link,$CROSS/aarch64-linux-gnu/lib64" \
          -DCMAKE_SHARED_LINKER_FLAGS="-L$BP_SYSROOT/lib -L$BP_SYSROOT/usr/lib -Wl,-rpath-link,$BP_SYSROOT/lib -Wl,-rpath-link,$BP_SYSROOT/usr/lib -Wl,-rpath-link,$CROSS/aarch64-linux-gnu/lib64" \
          \
          -DCMAKE_FIND_ROOT_PATH="$BP_SYSROOT;$GCC_SYSROOT" \
          -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
          -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
          -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
          -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
          \
          -DLLVM_USE_HOST_TOOLS=ON \
          -DLLVM_NATIVE_TOOL_DIR="$LLVM_HOST/bin" \
          -DLLVM_TABLEGEN="$LLVM_HOST/bin/llvm-tblgen" \
          \
          -DLLVM_HOST_TRIPLE=x86_64-unknown-linux-gnu \
          -DLLVM_DEFAULT_TARGET_TRIPLE=aarch64-linux-gnu \
          -DLLVM_TARGETS_TO_BUILD=AArch64 \
          \
          -DLLVM_ENABLE_PROJECTS=clang \
          \
          -DLLVM_BUILD_LLVM_DYLIB=ON \
          -DLLVM_LINK_LLVM_DYLIB=ON \
          -DLLVM_ENABLE_DUMP=ON \
          \
          -DCLANG_TOOL_DRIVER_BUILD=ON \
          -DCLANG_TOOL_LIBCLANG_BUILD=ON \
          \
          -DLLVM_INCLUDE_TESTS=OFF \
          -DLLVM_INCLUDE_EXAMPLES=OFF \
          -DLLVM_INCLUDE_BENCHMARKS=OFF \
          -DLLVM_ENABLE_ASSERTIONS=OFF
}

llvm_aarch64_build() {
  [ -f "$BUILD/llvm-aarch64-build/build.ninja" ] \
      || llvm_aarch64_defconfig || {
    log_e "Failed to configure llvm aarch64 build"
    return 1
  }

  . .venv/bin/activate \
      && cmd_run $_pri_runner ninja ${_pri_parallel:+-j$_pri_parallel} -C "$BUILD/llvm-aarch64-build" || {
    log_e "Failed to build llvm aarch64"
    return 1
  }

  if [ -f "$BUILD/llvm-aarch64-build/bin/clang" ]; then
    cmd_run realpath $BUILD/llvm-aarch64-build/bin/clang

    cmd_run eval "file \$(realpath $BUILD/llvm-aarch64-build/bin/clang) | grep \"ELF 64-bit LSB executable, ARM aarch64\" >/dev/null 2>&1" || {
      log_e "Failed to check llvm aarch64"
      return 1
    }
  else
    log_e "\$BUILD/llvm-aarch64-build/bin/clang not found"
    return 1
  fi

  if [ -f "$BUILD/llvm-aarch64-build/lib/libLLVM.so" ]; then
    cmd_run realpath $BUILD/llvm-aarch64-build/lib/libLLVM.so

    cmd_run eval "file \$(realpath $BUILD/llvm-aarch64-build/lib/libLLVM.so) | grep \"ELF 64-bit LSB shared object, ARM aarch64\" >/dev/null 2>&1" || {
      log_e "Failed to check llvm aarch64"
      return 1
    }
  else
    log_e "\$BUILD/llvm-aarch64-build/lib/libLLVM.so not found"
    return 1
  fi
}

llvm_aarch64_install() {

  [ -f "$BUILD/llvm-aarch64-build/build.ninja" ] \
      || llvm_aarch64_build || return 1

  cmd_run rm -rf "$LLVM_TARGET_STAGE"
  cmd_run mkdir -p "$LLVM_TARGET_STAGE"

  activate_venv \
      && cmd_run env DESTDIR="$LLVM_TARGET_STAGE" \
          ninja -C "$BUILD/llvm-aarch64-build" install
}

aarch64_cross_file_generate() {
  _lo_crossfile="${1:-$BUILD/mesa_aarch64.cmake}"
  cmd_run mkdir -p "$(dirname "$_lo_crossfile")"

  cat > "$_lo_crossfile" <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER "$CROSS/bin/aarch64-linux-gnu-gcc")
set(CMAKE_CXX_COMPILER "$CROSS/bin/aarch64-linux-gnu-g++")
set(CMAKE_AR "$CROSS/bin/aarch64-linux-gnu-ar")
set(CMAKE_RANLIB "$CROSS/bin/aarch64-linux-gnu-ranlib")
set(CMAKE_STRIP "$CROSS/bin/aarch64-linux-gnu-strip")

set(CMAKE_SYSROOT "$GCC_SYSROOT")

set(CMAKE_C_FLAGS_INIT
    "--sysroot=$GCC_SYSROOT -I$BP_SYSROOT/include -I$BP_SYSROOT/usr/include")
set(CMAKE_CXX_FLAGS_INIT
    "--sysroot=$GCC_SYSROOT -I$BP_SYSROOT/include -I$BP_SYSROOT/usr/include")

set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-L$BP_SYSROOT/lib -L$BP_SYSROOT/usr/lib -L$CROSS/aarch64-linux-gnu/lib64 -Wl,-rpath-link,$BP_SYSROOT/lib -Wl,-rpath-link,$BP_SYSROOT/usr/lib -Wl,-rpath-link,$CROSS/aarch64-linux-gnu/lib64")

set(CMAKE_SHARED_LINKER_FLAGS_INIT
    "-L$BP_SYSROOT/lib -L$BP_SYSROOT/usr/lib -L$CROSS/aarch64-linux-gnu/lib64 -Wl,-rpath-link,$BP_SYSROOT/lib -Wl,-rpath-link,$BP_SYSROOT/usr/lib -Wl,-rpath-link,$CROSS/aarch64-linux-gnu/lib64")

set(CMAKE_FIND_ROOT_PATH
    "$BP_SYSROOT"
    "$GCC_SYSROOT"
)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF

  cmd_run cat "$_lo_crossfile"
}

# TARGET/AArch64 libraries for Mesa.  The installed CLI programs are target
# programs too; they must not be executed while building on the x86-64 PC.
spirvtools_aarch64_defconfig() {
  _lo_crossfile="$BUILD/spirv-tools-aarch64.cmake"
  [ -f "$_lo_crossfile" ] || aarch64_cross_file_generate "$_lo_crossfile" || return 1

  activate_venv \
    && cmd_run cmake -S "$SRC/spirv-tools" \
      -B "$BUILD/spirv-tools-aarch64-build" \
      -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$_lo_crossfile" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr \
      -DCMAKE_INSTALL_LIBDIR=lib \
      -DSPIRV_SKIP_EXECUTABLES=ON \
      -DSPIRV_SKIP_TESTS=ON \
      -DSPIRV_WERROR=OFF
}

spirvtools_aarch64_build() {
  [ -f "$BUILD/spirv-tools-aarch64-build/build.ninja" ] \
      || spirvtools_aarch64_defconfig || return 1
  activate_venv \
      && cmd_run $_pri_runner ninja ${_pri_parallel:+-j$_pri_parallel} \
          -C "$BUILD/spirv-tools-aarch64-build"
}

spirvtools_aarch64_install() {
  [ -f "$BUILD/spirv-tools-aarch64-build/build.ninja" ] \
      || spirvtools_aarch64_build || return 1
  cmd_run mkdir -p "$SPIRV_TOOLS_TARGET_STAGE" \
      && activate_venv \
      && cmd_run env DESTDIR="$SPIRV_TOOLS_TARGET_STAGE" \
          ninja -C "$BUILD/spirv-tools-aarch64-build" install
}

spirvtranslator_aarch64_defconfig() {
  _lo_crossfile="$BUILD/spirv-tools-aarch64.cmake"
  _lo_spirv_headers="${SPIRV_HEADERS_SOURCE:-$SRC/spirv-tools/external/spirv-headers}"
  [ -f "$_lo_crossfile" ] || aarch64_cross_file_generate "$_lo_crossfile" || return 1
  require_file "$LLVM_TARGET_STAGE/usr/lib/cmake/llvm/LLVMConfig.cmake" || return 1
  require_file "$SPIRV_TOOLS_TARGET_STAGE/usr/lib/pkgconfig/SPIRV-Tools.pc" || return 1
  require_file "$_lo_spirv_headers/include/spirv/unified1/spirv.hpp" || return 1

  activate_venv \
      && cmd_run env \
          PKG_CONFIG_SYSROOT_DIR="$SPIRV_TOOLS_TARGET_STAGE" \
          PKG_CONFIG_LIBDIR="$SPIRV_TOOLS_TARGET_STAGE/usr/lib/pkgconfig" \
          cmake -S "$SRC/spirv-llvm-translator" \
          -B "$BUILD/spirv-llvm-translator-aarch64-build" \
          -G Ninja \
          -DCMAKE_TOOLCHAIN_FILE="$_lo_crossfile" \
          -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_INSTALL_PREFIX=/usr \
          -DLLVM_DIR="$LLVM_TARGET_STAGE/usr/lib/cmake/llvm" \
          -DLLVM_EXTERNAL_SPIRV_HEADERS_SOURCE_DIR="$_lo_spirv_headers" \
          -DLLVM_SPIRV_INCLUDE_TESTS=OFF \
          -DCCACHE_ALLOWED=OFF
}

spirvtranslator_aarch64_build() {
  [ -f "$BUILD/spirv-llvm-translator-aarch64-build/build.ninja" ] \
      || spirvtranslator_aarch64_defconfig || return 1
  activate_venv \
      && cmd_run $_pri_runner ninja ${_pri_parallel:+-j$_pri_parallel} \
          -C "$BUILD/spirv-llvm-translator-aarch64-build"
}

spirvtranslator_aarch64_install() {
  [ -f "$BUILD/spirv-llvm-translator-aarch64-build/build.ninja" ] \
      || spirvtranslator_aarch64_build || return 1
  cmd_run mkdir -p "$SPIRV_TRANSLATOR_TARGET_STAGE" \
      && activate_venv \
      && cmd_run env DESTDIR="$SPIRV_TRANSLATOR_TARGET_STAGE" \
          ninja -C "$BUILD/spirv-llvm-translator-aarch64-build" install
}

mesa_libclc_dl() {
  _lo_ci_script="$SRC/mesa3d/.gitlab-ci/container/build-libclc.sh"
  _lo_destination="${1:-${MESA_LIBCLC_SOURCE:-$BUILD/mesa-libclc}}"
  require_command curl || return 1
  require_file "$_lo_ci_script" || return 1

  _lo_project_id="${MESA_LIBCLC_PROJECT_ID:-$(
    sed -n 's/^MESA_LIBCLC_PROJECT_ID=//p' "$_lo_ci_script" | head -n 1
  )}"
  _lo_version="${MESA_LIBCLC_VERSION:-$(
    sed -n 's/^MESA_LIBCLC_VERSION=//p' "$_lo_ci_script" | head -n 1
  )}"
  if [ -z "$_lo_project_id" ] || [ -z "$_lo_version" ]; then
    log_e "Could not read mesa-libclc project/version from $_lo_ci_script"
    return 1
  fi

  _lo_base_url="https://gitlab.freedesktop.org/api/v4/projects/$_lo_project_id/packages/generic/mesa-libclc/$_lo_version"
  cmd_run mkdir -p "$_lo_destination" || return 1

  for _lo_name in \
      spirv-mesa3d-.spv \
      spirv64-mesa3d-.spv \
      mesa-libclc.pc.in; do
    _lo_output="$_lo_destination/$_lo_name"
    _lo_partial="${_lo_output}.part.$$"
    if ! cmd_run curl --fail --location --retry 3 \
        --connect-timeout 30 --output "$_lo_partial" \
        "$_lo_base_url/$_lo_name"; then
      rm -f "$_lo_partial"
      log_e "Failed to download mesa-libclc artifact: $_lo_name"
      return 1
    fi
    cmd_run mv "$_lo_partial" "$_lo_output" || return 1
  done

  for _lo_spirv in \
      "$_lo_destination/spirv-mesa3d-.spv" \
      "$_lo_destination/spirv64-mesa3d-.spv"; do
    _lo_magic=$(od -An -tx4 -N4 "$_lo_spirv" | tr -d ' ')
    if [ "$_lo_magic" != "07230203" ]; then
      log_e "Downloaded file does not have SPIR-V magic: $_lo_spirv"
      return 1
    fi
  done
  grep -q '^libexecdir=' "$_lo_destination/mesa-libclc.pc.in" || {
    log_e "Downloaded mesa-libclc.pc.in has no libexecdir entry"
    return 1
  }

  log_d "Downloaded mesa-libclc $_lo_version to $_lo_destination"
  cmd_run ls -lh \
      "$_lo_destination/spirv-mesa3d-.spv" \
      "$_lo_destination/spirv64-mesa3d-.spv" \
      "$_lo_destination/mesa-libclc.pc.in"

}

# Mesa 26 does not consume upstream LLVM's libclc.spv directly.  It requires
# mesa-libclc's two Mesa-specific SPIR-V modules.  Supply either mesa-libclc.pc
# or mesa-libclc.pc.in together with those modules in MESA_LIBCLC_SOURCE.
mesa_libclc_stage() {
  _lo_source="${MESA_LIBCLC_SOURCE:-$BUILD/mesa-libclc}"
  _lo_pc=
  for _lo_candidate in \
      "$_lo_source/mesa-libclc.pc" \
      "$_lo_source/mesa-libclc.pc.in"; do
    if [ -f "$_lo_candidate" ]; then
      _lo_pc="$_lo_candidate"
      break
    fi
  done

  require_file "$_lo_source/spirv-mesa3d-.spv" || return 1
  require_file "$_lo_source/spirv64-mesa3d-.spv" || return 1
  [ -n "$_lo_pc" ] || {
    log_e "mesa-libclc.pc or mesa-libclc.pc.in is missing from $_lo_source"
    return 1
  }

  _lo_libexec="$MESA_LIBCLC_STAGE/usr/lib/mesa-libclc"
  _lo_pcdir="$MESA_LIBCLC_STAGE/usr/share/pkgconfig"
  cmd_run mkdir -p "$_lo_libexec" "$_lo_pcdir" || return 1
  cmd_run cp "$_lo_source/spirv-mesa3d-.spv" \
      "$_lo_source/spirv64-mesa3d-.spv" "$_lo_libexec/" || return 1
  sed -e "s|^libexecdir=.*|libexecdir=$_lo_libexec|" \
      "$_lo_pc" > "$_lo_pcdir/mesa-libclc.pc" || return 1
  cmd_run cat "$_lo_pcdir/mesa-libclc.pc"
}

mesa_cross_file_generate() {
  _lo_pc_wrapper="$BUILD/mesa-aarch64-pkg-config"
  _lo_llvm_wrapper="$BUILD/mesa-aarch64-llvm-config"
  cmd_run mkdir -p "$BUILD" || return 1

  cat > "$_lo_pc_wrapper" <<EOF
#!/bin/sh
unset PKG_CONFIG_PATH PKG_CONFIG_SYSROOT_DIR
export PKG_CONFIG_LIBDIR="$MESA_LIBCLC_STAGE/usr/share/pkgconfig:$SPIRV_TRANSLATOR_TARGET_STAGE/usr/lib/pkgconfig:$SPIRV_TOOLS_TARGET_STAGE/usr/lib/pkgconfig:$BP_SYSROOT/lib/pkgconfig:$BP_SYSROOT/usr/lib/pkgconfig:$BP_SYSROOT/usr/share/pkgconfig"
exec pkg-config --define-prefix "\$@"
EOF

  cat > "$_lo_llvm_wrapper" <<EOF
#!/bin/sh
exec qemu-aarch64 -L "$GCC_SYSROOT" \
  -E LD_LIBRARY_PATH="$BP_SYSROOT/lib:$BP_SYSROOT/usr/lib:$LLVM_TARGET_STAGE/usr/lib:$CROSS/aarch64-linux-gnu/lib64" \
  "$LLVM_TARGET_STAGE/usr/bin/llvm-config" "\$@"
EOF
  cmd_run chmod +x "$_lo_pc_wrapper" "$_lo_llvm_wrapper" || return 1

  _lo_rustc="${RUSTC:-}"
  if [ -z "$_lo_rustc" ] && [ -x "$BUILD/br2-aarch64/host/bin/rustc" ]; then
    _lo_rustc="$BUILD/br2-aarch64/host/bin/rustc"
  fi
  [ -n "$_lo_rustc" ] || _lo_rustc=rustc

  cat > "$MESA_CROSS_FILE" <<EOF
[binaries]
c = '$CC'
cpp = '$CXX'
ar = '$AR'
strip = '$STRIP'
pkg-config = '$_lo_pc_wrapper'
llvm-config = '$_lo_llvm_wrapper'
rust = ['$_lo_rustc', '--target=aarch64-unknown-linux-gnu', '-Clinker=$CC']

[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'armv8a'
endian = 'little'

[properties]
needs_exe_wrapper = true
sys_root = '$GCC_SYSROOT'

[built-in options]
c_args = ['-I$BP_SYSROOT/include', '-I$BP_SYSROOT/usr/include']
cpp_args = ['-I$BP_SYSROOT/include', '-I$BP_SYSROOT/usr/include']
c_link_args = ['-L$BP_SYSROOT/lib', '-L$BP_SYSROOT/usr/lib', '-Wl,-rpath-link,$BP_SYSROOT/lib', '-Wl,-rpath-link,$BP_SYSROOT/usr/lib', '-Wl,-rpath-link,$CROSS/aarch64-linux-gnu/lib64']
cpp_link_args = ['-L$BP_SYSROOT/lib', '-L$BP_SYSROOT/usr/lib', '-Wl,-rpath-link,$BP_SYSROOT/lib', '-Wl,-rpath-link,$BP_SYSROOT/usr/lib', '-Wl,-rpath-link,$CROSS/aarch64-linux-gnu/lib64']
EOF
  cmd_run cat "$MESA_CROSS_FILE"
}

mesa_aarch64_preflight() {
  require_command qemu-aarch64 || return 1
  require_command pkg-config || return 1
  require_file "$LLVM_TARGET_STAGE/usr/bin/llvm-config" || return 1
  require_file "$SPIRV_TOOLS_TARGET_STAGE/usr/lib/pkgconfig/SPIRV-Tools.pc" || return 1
  require_file "$SPIRV_TRANSLATOR_TARGET_STAGE/usr/lib/pkgconfig/LLVMSPIRVLib.pc" || return 1
  require_file "$MESA_LIBCLC_STAGE/usr/share/pkgconfig/mesa-libclc.pc" || return 1

  command -v bindgen >/dev/null 2>&1 || {
    log_e "Rusticl requires bindgen >= 0.71.1 in PATH"
    return 1
  }

  mesa_cross_file_generate || return 1
  cmd_run "$BUILD/mesa-aarch64-llvm-config" --version --targets-built || return 1
  cmd_run "$BUILD/mesa-aarch64-pkg-config" --modversion \
      SPIRV-Tools LLVMSPIRVLib mesa-libclc
}

mesa_aarch64_defconfig() {
  mesa_aarch64_preflight || return 1
  activate_venv \
      && cmd_run meson setup "$MESA_BUILD" "$SRC/mesa3d" \
          --cross-file "$MESA_CROSS_FILE" \
          --prefix /usr \
          --libdir lib \
          --buildtype release \
          -Dplatforms=[] \
          -Dglx=disabled \
          -Degl=enabled \
          -Dgbm=enabled \
          -Dopengl=true \
          -Dgles1=enabled \
          -Dgles2=enabled \
          -Dgallium-drivers=llvmpipe \
          -Dvulkan-drivers=swrast \
          -Dgallium-rusticl=true \
          -Dllvm=enabled \
          -Dshared-llvm=enabled \
          -Dspirv-tools=enabled \
          -Dstatic-libclc=spirv64 \
          -Dbuild-tests=false \
          -Dvalgrind=disabled \
          -Dlibunwind=disabled
}

mesa_aarch64_build() {
  [ -f "$MESA_BUILD/build.ninja" ] || mesa_aarch64_defconfig || return 1
  activate_venv \
      && cmd_run $_pri_runner ninja ${_pri_parallel:+-j$_pri_parallel} -C "$MESA_BUILD"
}

mesa_aarch64_install() {
  [ -f "$MESA_BUILD/build.ninja" ] || mesa_aarch64_build || return 1
  cmd_run mkdir -p "$MESA_STAGE" \
      && activate_venv \
      && cmd_run env DESTDIR="$MESA_STAGE" ninja -C "$MESA_BUILD" install
}

all() {
  setenv_host
  llvm_host_defconfig \
      && llvm_host_build \
      && llvm_host_install \
      || return 1

  setenv_cross
  llvm_aarch64_defconfig \
    && llvm_aarch64_build \
    && llvm_aarch64_install \
    && spirvtools_aarch64_defconfig \
    && spirvtools_aarch64_build \
    && spirvtools_aarch64_install \
    && spirvtranslator_aarch64_defconfig \
    && spirvtranslator_aarch64_build \
    && spirvtranslator_aarch64_install \
    && mesa_libclc_stage \
    && mesa_aarch64_defconfig \
    && mesa_aarch64_build \
    && mesa_aarch64_install \
    && inspect results
}

inspect_source() {
  _lo_meson="$SRC/mesa3d/meson.build"
  _lo_clc="$SRC/mesa3d/src/compiler/clc/meson.build"
  _lo_loader="$SRC/mesa3d/src/compiler/clc/nir_load_libclc.c"
  require_file "$_lo_meson" || return 1
  require_file "$_lo_clc" || return 1
  require_file "$_lo_loader" || return 1

  log_d "Mesa source dependency evidence"
  rg -n -F "dep_llvmspirvlib = dependency('LLVMSPIRVLib'" "$_lo_meson" \
      && rg -n -F "'SPIRV-Tools'" "$_lo_meson" \
      && rg -n -F "dep_clc = dependency('mesa-libclc'" "$_lo_meson" \
      && rg -n -F "native : not meson.can_run_host_binaries()" "$_lo_clc" \
      && rg -n -F 'DYNAMIC_LIBCLC_PATH "spirv64-mesa3d-.spv"' "$_lo_loader" || {
    log_e "Mesa dependency assumptions changed; review the source before building"
    return 1
  }

  cat <<'EOF'
[source OK] HOST:    Mesa generators (including mesa_clc), bindgen, build tools
[source OK] TARGET:  LLVM, LLVMSPIRVLib, SPIRV-Tools, Mesa/Rusticl libraries
[source OK] ARTIFACT: mesa-libclc spirv-mesa3d-.spv and spirv64-mesa3d-.spv
EOF
}

inspect_arch() {
  _lo_path="$1"
  _lo_arch="$2"
  _lo_name="$3"
  if [ ! -e "$_lo_path" ]; then
    log_e "[$_lo_name] missing: $_lo_path"
    return 1
  fi
  if file -L "$_lo_path" | grep -F "$_lo_arch" >/dev/null 2>&1; then
    log_d "[$_lo_name] architecture OK: $_lo_arch"
    return 0
  fi
  log_e "[$_lo_name] expected $_lo_arch: $(file -L "$_lo_path")"
  return 1
}

inspect_archive_arch() {
  _lo_path="$1"
  _lo_machine="$2"
  _lo_name="$3"
  require_file "$_lo_path" || return 1
  if readelf -h "$_lo_path" 2>/dev/null | grep -F "Machine:                           $_lo_machine" >/dev/null; then
    log_d "[$_lo_name] archive members OK: $_lo_machine"
    return 0
  fi
  log_e "[$_lo_name] has no $_lo_machine ELF member: $_lo_path"
  return 1
}

inspect_results() {
  _lo_failed=0
  inspect_arch "$LLVM_HOST/bin/llvm-config" "x86-64" "host llvm-config" || _lo_failed=1
  inspect_arch "$LLVM_HOST/bin/clang" "x86-64" "host clang" || _lo_failed=1
  inspect_arch "$LLVM_TARGET_STAGE/usr/lib/libLLVM.so" "ARM aarch64" "target LLVM" || _lo_failed=1
  inspect_arch "$SPIRV_TOOLS_TARGET_STAGE/usr/lib/libSPIRV-Tools-shared.so" "ARM aarch64" "target SPIRV-Tools" || _lo_failed=1
  inspect_archive_arch "$SPIRV_TRANSLATOR_TARGET_STAGE/usr/lib/libLLVMSPIRVLib.a" "AArch64" "target LLVMSPIRVLib" || _lo_failed=1

  require_file "$MESA_LIBCLC_STAGE/usr/lib/mesa-libclc/spirv-mesa3d-.spv" || _lo_failed=1
  require_file "$MESA_LIBCLC_STAGE/usr/lib/mesa-libclc/spirv64-mesa3d-.spv" || _lo_failed=1
  if [ -f "$MESA_LIBCLC_STAGE/usr/lib/mesa-libclc/spirv64-mesa3d-.spv" ]; then
    _lo_magic=$(od -An -tx4 -N4 "$MESA_LIBCLC_STAGE/usr/lib/mesa-libclc/spirv64-mesa3d-.spv" | tr -d ' ')
    [ "$_lo_magic" = "07230203" ] || {
      log_e "mesa-libclc file does not have SPIR-V magic: $_lo_magic"
      _lo_failed=1
    }
  fi

  _lo_mesa_driver="$MESA_STAGE/usr/lib/dri/swrast_dri.so"
  _lo_lavapipe="$MESA_STAGE/usr/lib/libvulkan_lvp.so"
  _lo_rusticl="$MESA_STAGE/usr/lib/libRusticlOpenCL.so"
  inspect_arch "$_lo_mesa_driver" "ARM aarch64" "llvmpipe DRI" || _lo_failed=1
  inspect_arch "$_lo_lavapipe" "ARM aarch64" "lavapipe" || _lo_failed=1
  inspect_arch "$_lo_rusticl" "ARM aarch64" "Rusticl OpenCL ICD" || _lo_failed=1

  [ "$_lo_failed" -eq 0 ] || return 1
  log_d "All inspected build results passed"
}

inspect() {
  case "${1:-all}" in
    source)
      inspect_source
      ;;
    results)
      inspect_results
      ;;
    all)
      inspect_source && inspect_results
      ;;
    *)
      log_e "Usage: $(basename "$0") inspect [source|results|all]"
      return 2
      ;;
  esac
}

show_help() {
  cat <<EOHELP
Usage: $(basename "$0") <COMMAND> [ARGUMENTS]

HOST tools (x86-64):
  llvm_host_defconfig | llvm_host_build | llvm_host_install

TARGET dependencies (AArch64):
  llvm_aarch64_defconfig | llvm_aarch64_build | llvm_aarch64_install
  spirvtools_aarch64_defconfig | spirvtools_aarch64_build | spirvtools_aarch64_install
  spirvtranslator_aarch64_defconfig | spirvtranslator_aarch64_build | spirvtranslator_aarch64_install

Mesa-specific SPIR-V artifacts:
  mesa_libclc_dl [DESTINATION]
    Downloads the version selected by Mesa's build-libclc.sh.
  mesa_libclc_stage
    Reads MESA_LIBCLC_SOURCE (default: \$BUILD/mesa-libclc).

Mesa target:
  mesa_cross_file_generate | mesa_aarch64_preflight
  mesa_aarch64_defconfig | mesa_aarch64_build | mesa_aarch64_install

Orchestration and validation:
  all
  inspect [source|results|all]

No command uses sudo or modifies a source tree.
EOHELP
}

dispatch() {
  _lo_command="${1:-help}"
  [ "$#" -eq 0 ] || shift

  case "$_lo_command" in
    llvm_host_defconfig|llvm_host_build|llvm_host_install)
      setenv_host
      "$_lo_command" "$@"
      ;;
    llvm_aarch64_defconfig|llvm_aarch64_build|llvm_aarch64_install|\
    aarch64_cross_file_generate|\
    spirvtools_aarch64_defconfig|spirvtools_aarch64_build|spirvtools_aarch64_install|\
    spirvtranslator_aarch64_defconfig|spirvtranslator_aarch64_build|spirvtranslator_aarch64_install|\
    mesa_cross_file_generate|mesa_aarch64_preflight|\
    mesa_aarch64_defconfig|mesa_aarch64_build|mesa_aarch64_install)
      setenv_cross
      "$_lo_command" "$@"
      ;;
    mesa_libclc_dl|mesa_libclc_stage|inspect|all)
      "$_lo_command" "$@"
      ;;
    help|-h|--help)
      show_help
      ;;
    *)
      log_e "Unknown command: $_lo_command"
      show_help >&2
      return 2
      ;;
  esac
}

setenv_base
dispatch "$@"
