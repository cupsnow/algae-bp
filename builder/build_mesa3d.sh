#!/usr/bin/env bash
# Unpatched upstream Mesa software stack for AArch64 Linux. See docs/mesa3d-build.md.
set -euo pipefail
trap 'echo "mesa-build: failed at line $LINENO" >&2' ERR
TOP=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
SRC=${SRC:-$(dirname -- "$TOP")}
OUT=${OUT:-$TOP/build/mesa3d-stack}
HOST=${HOST:-$OUT/host}
ROOT=${ROOT:-$OUT/root}
CROSS=${CROSS:-$TOP/cross/aarch64-linux-gnu/bin/aarch64-linux-gnu-}
GCC_SYSROOT=${GCC_SYSROOT:-$("${CROSS}gcc" -print-sysroot)}
PYVENVDIR=${PYVENVDIR:-$TOP/.venv}
LLVM_TARGETS='X86;AArch64;SPIRV'
HEADERS=${HEADERS:-$SRC/spirv-tools/external/spirv-headers}
# Native LLVM tools take priority over other host tools.
export PATH="$HOST/bin:$PATH"
unset CC CXX CPP AR AS LD RANLIB STRIP CFLAGS CXXFLAGS CPPFLAGS LDFLAGS \
  CMAKE_TOOLCHAIN_FILE CMAKE_PREFIX_PATH LLVM_CONFIG PKG_CONFIG_PATH \
  PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR DESTDIR

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

JOBS=${JOBS:-$_pri_parallel}
# Limit build resources to reduce host memory pressure.
_pri_runner=(systemd-run --user --scope
  -p "MemoryMax=$_pri_memory_max"
  -p "CPUQuota=$_pri_cpu_quota")

fail() {
  echo "mesa-build: $*" >&2; exit 1;
}

run() {
  printf '+ '; printf '%q ' "$@"; printf '\n'; "$@";
}

activate_venv() {
  [[ -f $PYVENVDIR/bin/activate && -x $PYVENVDIR/bin/python3 ]] ||
    fail "missing Python venv: $PYVENVDIR (run: make -C \"$TOP\" PYVENVDIR=\"$PYVENVDIR\" pyvenv)"
  source "$PYVENVDIR/bin/activate"
  export PATH="$HOST/bin:$PATH"
}

help() {
  cat <<'HELP'
Usage: builder/build_mesa3d.sh COMMAND
  check           check source trees, cross compiler and build prerequisites
  manifest        print current source revisions (does not fetch or checkout)
  all             check, then build/install all stages in dependency order
  deps-target     AArch64 zlib and libdrm from sibling source trees
  llvm-host       native LLVM/Clang tools and libraries
  spirv-host      native SPIRV-Tools and LLVM-SPIRV-Translator
  glslang-host    native GLSL to SPIR-V compiler for lavapipe
  mesa-host       native mesa_clc and vtn_bindgen2
  libclc          host Clang -> 32/64-bit SPIR-V builtins, staged for target
  llvm-target     AArch64 LLVM/Clang libraries
  spirv-target    AArch64 SPIRV-Tools and LLVM-SPIRV-Translator
  mesa-configure  configure llvmpipe + lavapipe + Rusticl
  mesa-target     configure, build and stage Mesa
  package         tar the staged target files and runtime environment
Variables: SRC OUT HOST ROOT CROSS GCC_SYSROOT HEADERS JOBS (default based on available RAM).
Builds run in a systemd user scope with RAM-based memory and CPU limits.
PYVENVDIR defaults to algae-bp/.venv; activated for check and all build/package stages.
Create it with make pyvenv. Help and manifest do not require a venv.
All outputs default to algae-bp/build/mesa3d-stack. No source updates or patches.
Individual stages require their preceding dependencies to have been installed.
HELP
}

manifest() {
  for tree in llvm-project mesa3d spirv-tools spirv-llvm-translator glslang; do
    printf '%s ' "$tree"
    git -C "$SRC/$tree" describe --always --dirty
    git -C "$SRC/$tree" rev-parse HEAD
  done
  printf 'SPIRV-Headers '; git -C "$HEADERS" rev-parse HEAD
}

check() {
  for tree in llvm-project/llvm mesa3d spirv-tools spirv-llvm-translator glslang libdrm zlib; do
    [[ -d $SRC/$tree ]] || fail "missing $SRC/$tree"
  done
  [[ -f $HEADERS/include/spirv/unified1/spirv.hpp ]] || fail "missing SPIRV-Headers: $HEADERS"
  for tool in cmake ninja meson pkg-config python3 gcc g++ rustc bindgen bison flex; do
    command -v "$tool" >/dev/null || fail "missing host tool: $tool (see docs/mesa3d-build.md)"
  done
  for tool in gcc g++ ar ranlib strip; do
    [[ -x ${CROSS}$tool ]] || fail "missing ${CROSS}$tool"
  done
  [[ $("${CROSS}gcc" -dumpmachine) == aarch64* ]] || fail 'compiler must target AArch64'
  [[ -d $GCC_SYSROOT ]] || fail "missing compiler sysroot: $GCC_SYSROOT"
  python3 -c 'import mako, yaml, packaging'
  rustc --print target-libdir --target aarch64-unknown-linux-gnu | while IFS= read -r dir; do
    [[ -d $dir ]] || fail 'install Rust aarch64-unknown-linux-gnu standard libraries'
  done
  run cmake --version
  run meson --version
  run rustc --version
  run bindgen --version
  manifest
}

prepare() {
  # C programs linking target C++ DSOs also need their indirect dependencies.
  # GCC's C++ runtime can live outside its libc sysroot; -L alone does not
  # let ld resolve a DSO's DT_NEEDED entries when linking with the C driver.
  local cxx_runtime
  cxx_runtime=$("${CROSS}g++" --sysroot="$GCC_SYSROOT" -print-file-name=libstdc++.so.6)
  [[ $cxx_runtime == /* && -f $cxx_runtime ]] || fail 'cannot locate target libstdc++.so.6'
  TARGET_LINK_FLAGS="-L$ROOT/usr/lib -Wl,-rpath-link,$ROOT/usr/lib -Wl,-rpath-link,$(dirname -- "$cxx_runtime")"
  # Paths go into CMake/Meson files. Reject characters requiring special quoting.
  for path in "$SRC" "$OUT" "$HOST" "$ROOT" "$CROSS" "$GCC_SYSROOT" "$HEADERS" "$cxx_runtime"; do
    [[ $path == /* && $path != *[\ \'\"\;\\$'\n']* ]] || fail "use absolute paths without spaces/quotes/semicolons: $path"
  done
  mkdir -p "$OUT" "$HOST" "$ROOT/usr"
  cat > "$OUT/target.cmake" <<CMAKE
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER "${CROSS}gcc")
set(CMAKE_CXX_COMPILER "${CROSS}g++")
set(CMAKE_AR "${CROSS}ar")
set(CMAKE_RANLIB "${CROSS}ranlib")
set(CMAKE_SYSROOT "$GCC_SYSROOT")
set(CMAKE_FIND_ROOT_PATH "$ROOT" "$GCC_SYSROOT")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_C_FLAGS_INIT "-I$ROOT/usr/include")
set(CMAKE_CXX_FLAGS_INIT "-I$ROOT/usr/include")
set(CMAKE_EXE_LINKER_FLAGS_INIT "$TARGET_LINK_FLAGS")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "$TARGET_LINK_FLAGS")
CMAKE
}

deps_target() {
  cmake_build zlib-target "$SRC/zlib" target -DZLIB_BUILD_TESTING=OFF
  target_env
  cat > "$OUT/deps-cross.ini" <<MESON
[binaries]
c = '${CROSS}gcc'
cpp = '${CROSS}g++'
ar = '${CROSS}ar'
strip = '${CROSS}strip'
pkg-config = 'pkg-config'
[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
[properties]
needs_exe_wrapper = true
[built-in options]
c_args = ['--sysroot=$GCC_SYSROOT']
c_link_args = ['--sysroot=$GCC_SYSROOT']
MESON
  local reconfigure=()
  [[ ! -f $OUT/libdrm-target/meson-private/coredata.dat ]] || reconfigure=(--reconfigure)
  run meson setup "${reconfigure[@]}" "$OUT/libdrm-target" "$SRC/libdrm" \
    --cross-file="$OUT/deps-cross.ini" --prefix=/usr --libdir=lib \
    --buildtype=release --wrap-mode=nofallback -Dauto_features=disabled
  run "${_pri_runner[@]}" meson compile -C "$OUT/libdrm-target" -j "$JOBS"
  run env DESTDIR="$ROOT" meson install --no-rebuild -C "$OUT/libdrm-target"
}

native_env() {
  unset PKG_CONFIG_SYSROOT_DIR PKG_CONFIG_LIBDIR
  export PKG_CONFIG_PATH="$HOST/lib/pkgconfig:$HOST/share/pkgconfig"
  export CMAKE_PREFIX_PATH="$HOST"
  export LD_LIBRARY_PATH="$HOST/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  export LIBCLANG_PATH="$HOST/lib"
}

target_env() {
  export PKG_CONFIG_SYSROOT_DIR="$ROOT"
  export PKG_CONFIG_LIBDIR="$ROOT/usr/lib/pkgconfig:$ROOT/usr/share/pkgconfig:$GCC_SYSROOT/usr/lib/pkgconfig:$GCC_SYSROOT/usr/share/pkgconfig"
  unset PKG_CONFIG_PATH CMAKE_PREFIX_PATH
  export LIBCLANG_PATH="$HOST/lib"
  export LD_LIBRARY_PATH="$HOST/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
}

cmake_build() {
  local name=$1 source=$2 arch=$3; shift 3
  local opts=(-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_POSITION_INDEPENDENT_CODE=ON)
  if [[ $arch == target ]]; then
    target_env
    opts+=(-DCMAKE_TOOLCHAIN_FILE="$OUT/target.cmake" -DCMAKE_INSTALL_PREFIX=/usr
      -DCMAKE_EXE_LINKER_FLAGS="$TARGET_LINK_FLAGS"
      -DCMAKE_SHARED_LINKER_FLAGS="$TARGET_LINK_FLAGS")
  else
    native_env
    opts+=(-DCMAKE_C_COMPILER="$(command -v gcc)" -DCMAKE_CXX_COMPILER="$(command -v g++)"
      -DCMAKE_AR="$(command -v ar)" -DCMAKE_RANLIB="$(command -v ranlib)" -DCMAKE_INSTALL_PREFIX="$HOST")
  fi
  run cmake -G Ninja -S "$source" -B "$OUT/$name" "${opts[@]}" "$@"
  run "${_pri_runner[@]}" cmake --build "$OUT/$name" --parallel "$JOBS"
  if [[ $arch == target ]]; then
    run env DESTDIR="$ROOT" cmake --install "$OUT/$name"
  else
    run cmake --install "$OUT/$name"
  fi
}

llvm() {
  local arch=$1 extra=()
  if [[ $arch == target ]]; then
    [[ -x $HOST/bin/llvm-tblgen ]] || fail 'run llvm-host first'
    extra+=(-DLLVM_USE_HOST_TOOLS=ON -DLLVM_NATIVE_TOOL_DIR="$OUT/llvm-host/bin"
      -DLLVM_TABLEGEN="$OUT/llvm-host/bin/llvm-tblgen"
      -DCLANG_TABLEGEN="$OUT/llvm-host/bin/clang-tblgen"
      -DLLVM_DEFAULT_TARGET_TRIPLE=aarch64-unknown-linux-gnu
      -DLLVM_HOST_TRIPLE=aarch64-unknown-linux-gnu)
  fi
  cmake_build "llvm-$arch" "$SRC/llvm-project/llvm" "$arch" \
    -DLLVM_ENABLE_PROJECTS=clang -DLLVM_TARGETS_TO_BUILD="$LLVM_TARGETS" \
    -DLLVM_DEFAULT_TARGET_TRIPLE=aarch64-unknown-linux-gnu -DLLVM_BUILD_LLVM_DYLIB=ON -DLLVM_LINK_LLVM_DYLIB=ON \
    -DCLANG_LINK_CLANG_DYLIB=ON -DLLVM_ENABLE_RTTI=ON -DLLVM_ENABLE_DUMP=ON \
    -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF \
    -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF \
    -DLLVM_ENABLE_TERMINFO=OFF -DLLVM_ENABLE_LIBEDIT=OFF "${extra[@]}"
}

spirv() {
  local arch=$1 prefix=$HOST
  [[ $arch == target ]] && prefix="$ROOT/usr"
  cmake_build "spirv-tools-$arch" "$SRC/spirv-tools" "$arch" \
    -DSPIRV-Headers_SOURCE_DIR="$HEADERS" -DSPIRV_SKIP_TESTS=ON \
    -DSPIRV_SKIP_EXECUTABLES=ON -DSPIRV_TOOLS_BUILD_STATIC=ON
  cmake_build "translator-$arch" "$SRC/spirv-llvm-translator" "$arch" \
    -DLLVM_DIR="$prefix/lib/cmake/llvm" -DLLVM_EXTERNAL_SPIRV_HEADERS_SOURCE_DIR="$HEADERS" \
    -DLLVM_SPIRV_INCLUDE_TESTS=OFF -DLLVM_SPIRV_ENABLE_LIBSPIRV_DIS=OFF \
    -DBUILD_SHARED_LIBS=ON -DLLVM_LINK_LLVM_DYLIB=ON
}

meson_setup() {
  local dir=$1; shift
  local reconfigure=()
  [[ ! -f $dir/meson-private/coredata.dat ]] || reconfigure=(--reconfigure)
  run meson setup "${reconfigure[@]}" "$dir" "$SRC/mesa3d" --wrap-mode=nofallback "$@"
}

glslang_host() {
  # Mesa's Vulkan BVH shaders need a native compiler, without optimizer passes.
  cmake_build glslang-host "$SRC/glslang" host \
    -DBUILD_EXTERNAL=OFF -DENABLE_OPT=OFF -DGLSLANG_TESTS=OFF \
    -DGLSLANG_ENABLE_INSTALL=ON -DENABLE_GLSLANG_BINARIES=ON
}

mesa_host() {
  native_env
  meson_setup "$OUT/mesa-host" --prefix="$HOST" --libdir=lib --buildtype=release \
    -Dgallium-drivers=[] -Dvulkan-drivers=[] -Dplatforms=[] -Dglx=disabled \
    -Dopengl=false -Degl=disabled -Dgbm=disabled -Dgles1=disabled -Dgles2=disabled \
    -Dllvm=enabled -Dshared-llvm=enabled -Dmesa-clc=enabled -Dinstall-mesa-clc=true
  run "${_pri_runner[@]}" meson compile -C "$OUT/mesa-host" -j "$JOBS"
  run meson install --no-rebuild -C "$OUT/mesa-host"
}

libclc() {
  # Standalone LLVM 24 layout; no target C compiler is used for device IR.
  for bits in 32 64; do
    cmake_build "libclc-$bits" "$SRC/llvm-project/libclc" host \
      -DLLVM_DIR="$HOST/lib/cmake/llvm" -DLLVM_DEFAULT_TARGET_TRIPLE="spirv$bits-unknown-unknown" \
      -DCMAKE_CLC_COMPILER="$HOST/bin/clang" -DLIBCLC_USE_SPIRV_BACKEND=ON \
      -DLLVM_INCLUDE_TESTS=OFF
    mkdir -p "$ROOT/usr/share/clc/spirv$bits-unknown-unknown"
    cp "$HOST/share/clc/spirv$bits-unknown-unknown/libclc.spv" \
      "$ROOT/usr/share/clc/spirv$bits-unknown-unknown/"
  done
  # Current upstream standalone libclc does not install a .pc file. Mesa needs
  # only libexecdir, and already recognizes this upstream directory layout.
  mkdir -p "$ROOT/usr/share/pkgconfig"
  cat > "$ROOT/usr/share/pkgconfig/libclc.pc" <<'PC'
prefix=/usr
libexecdir=${prefix}/share/clc
Name: libclc
Description: Upstream LLVM SPIR-V OpenCL builtins
Version: 0.2.0
PC
}

mesa_configure() {
  target_env
  [[ -x $HOST/bin/mesa_clc ]] || fail 'run mesa-host first'
  [[ -f $ROOT/usr/lib/libLLVM.so ]] || fail 'run llvm-target first'
  # llvm-config must run on the build PC but report the staged target paths.
  # Both LLVM builds deliberately use identical source, targets, RTTI and shared
  # library settings. Native metadata queries then describe identical libraries.
  cat > "$OUT/llvm-config-target" <<WRAPPER
#!/usr/bin/env python3
import subprocess, sys
args = sys.argv[1:]
if args == ['--host-target']:
    print('aarch64-unknown-linux-gnu')
else:
    output = subprocess.check_output(['$HOST/bin/llvm-config', *args], text=True)
    sys.stdout.write(output.replace('$HOST', '$ROOT/usr'))
WRAPPER
  chmod +x "$OUT/llvm-config-target"
  cat > "$OUT/mesa-cross.ini" <<MESON
[binaries]
c = '${CROSS}gcc'
cpp = '${CROSS}g++'
ar = '${CROSS}ar'
strip = '${CROSS}strip'
pkg-config = 'pkg-config'
llvm-config = '$OUT/llvm-config-target'
rust = ['rustc', '--target', 'aarch64-unknown-linux-gnu']
[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
[properties]
needs_exe_wrapper = true
sys_root = '$ROOT'
pkg_config_libdir = ['$ROOT/usr/lib/pkgconfig', '$ROOT/usr/share/pkgconfig']
bindgen_clang_arguments = ['--sysroot=$GCC_SYSROOT', '--gcc-toolchain=$(dirname -- "$(dirname -- "${CROSS}gcc")")']
[built-in options]
c_args = ['--sysroot=$GCC_SYSROOT', '-I$ROOT/usr/include']
cpp_args = ['--sysroot=$GCC_SYSROOT', '-I$ROOT/usr/include']
c_link_args = ['--sysroot=$GCC_SYSROOT', '-L$ROOT/usr/lib', '-Wl,-rpath-link,$ROOT/usr/lib']
cpp_link_args = ['--sysroot=$GCC_SYSROOT', '-L$ROOT/usr/lib', '-Wl,-rpath-link,$ROOT/usr/lib']
rust_link_args = ['-C', 'linker=${CROSS}gcc', '-C', 'link-arg=--sysroot=$GCC_SYSROOT', '-C', 'link-arg=-L$ROOT/usr/lib']
MESON
  meson_setup "$OUT/mesa-target" --cross-file="$OUT/mesa-cross.ini" \
    --prefix=/usr --libdir=lib --buildtype=release \
    -Dgallium-drivers=llvmpipe -Dvulkan-drivers=swrast -Dgallium-rusticl=true \
    -Dllvm=enabled -Dshared-llvm=enabled -Drust_std=2021 -Dmesa-clc=system \
    -Dmesa-clc-bundle-headers=enabled -Dstatic-libclc=[] \
    -Dplatforms=[] -Dglx=disabled -Degl=enabled -Dgbm=enabled \
    -Dgles1=disabled -Dgles2=enabled -Dopengl=true -Dglvnd=disabled \
    -Dgallium-va=disabled -Dvideo-codecs=[] -Dbuild-tests=false \
    -Dexpat=disabled -Dxmlconfig=disabled -Dzstd=disabled -Dzlib=enabled
}

mesa_target() {
  mesa_configure
  run "${_pri_runner[@]}" meson compile -C "$OUT/mesa-target" -j "$JOBS"
  run env DESTDIR="$ROOT" meson install --no-rebuild -C "$OUT/mesa-target"
  runtime_env
}

runtime_env() {
  cat > "$ROOT/mesa-software-env.sh" <<'ENV'
# Source on the target after installing this archive's usr/ tree into /usr.
export LIBGL_ALWAYS_SOFTWARE=true
export GALLIUM_DRIVER=llvmpipe
export RUSTICL_ENABLE=llvmpipe
export EGL_PLATFORM=surfaceless
export LD_LIBRARY_PATH=/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
# Resolve filename instead of assuming Mesa's ICD filename convention.
for mesa_icd in /usr/share/vulkan/icd.d/lvp*.json; do
  if [ -f "$mesa_icd" ]; then export VK_DRIVER_FILES="$mesa_icd"; break; fi
done
unset mesa_icd
ENV
}

command=${1:-help}

case "$command" in
help|-h|--help)
  help; exit
  ;;
check)
  activate_venv; check; exit
  ;;
manifest)
  manifest; exit
  ;;
all|deps-target|llvm-host|llvm-target|spirv-host|spirv-target|glslang-host|mesa-host|libclc|mesa-configure|mesa-target|package)
  ;;
*)
  fail "unknown command: $command"
  ;;
esac

activate_venv

prepare
case "$command" in
all)
  check
  manifest > "$OUT/source-revisions.txt"
  deps_target
  llvm host
  spirv host
  glslang_host
  mesa_host
  libclc
  llvm target
  spirv target
  mesa_target
  ;;
deps-target)
  deps_target
  ;;
llvm-host)
  llvm host
  ;;
llvm-target)
  llvm target
  ;;
spirv-host)
  spirv host
  ;;
spirv-target)
  spirv target
  ;;
glslang-host)
  glslang_host
  ;;
mesa-host)
  mesa_host
  ;;
libclc)
  libclc
  ;;
mesa-configure)
  mesa_configure
  ;;
mesa-target)
  mesa_target
  ;;
package)
  [[ -f $ROOT/usr/lib/libRusticlOpenCL.so ]] || fail 'build Mesa before packaging'
  runtime_env
  run tar -C "$ROOT" -cJf "$OUT/mesa3d-aarch64.tar.xz" .
  ;;
esac
