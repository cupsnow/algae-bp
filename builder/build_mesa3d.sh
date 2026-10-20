#!/bin/sh
# Unpatched upstream Mesa software stack for AArch64 Linux. See docs/mesa3d-build.md.
set -eu
# POSIX exit trap: report failures while preserving the original exit status.
trap 'status=$?; if [ "$status" -ne 0 ]; then
  printf "mesa-build: failed (status %s)\n" "$status" >&2
fi' 0
APP_PLATFORM=${APP_PLATFORM:-qemuarm64}
TOP=$(cd -- "$(dirname -- "$0")/.." && pwd)
SRC=${SRC:-$(dirname -- "$TOP")}
OUT=${OUT:-$TOP/build/mesa3d-stack}
HOST=${HOST:-$OUT/host}
ROOT=${ROOT:-$OUT/root}
CROSS=${CROSS:-$TOP/cross/aarch64-linux-gnu/bin/aarch64-linux-gnu-}
GCC_SYSROOT=${GCC_SYSROOT:-$("${CROSS}gcc" -print-sysroot)}
TARGET_SYSROOT=${TARGET_SYSROOT:-${SRC}/build/sysroot-${APP_PLATFORM}}
HOST_SYSROOT=${HOST_SYSROOT:-${SRC}/build/sysroot-ub20}
PYVENVDIR=${PYVENVDIR:-$TOP/.venv}
LLVM_TARGETS='X86;AArch64;SPIRV'
HEADERS=${HEADERS:-$SRC/spirv-tools/external/spirv-headers}
TEST_SRC=${TEST_SRC:-$TOP/build/mesa3d-test-sources}
TEST_TREES='vulkan-headers vulkan-loader vulkan-tools opencl-headers opencl-icd-loader clinfo mesa-demos'
# Native LLVM tools take priority over other host tools.
export PATH="$HOST/bin:$PATH"
unset CC CXX CPP AR AS LD RANLIB STRIP CFLAGS CXXFLAGS CPPFLAGS LDFLAGS \
  CMAKE_TOOLCHAIN_FILE CMAKE_PREFIX_PATH LLVM_CONFIG PKG_CONFIG_PATH \
  PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR DESTDIR

# Capture free separately so a failure cannot be hidden by a pipeline.
_lo_mem_info=$(free -g)
_lo_free_mem=$(printf '%s\n' "$_lo_mem_info" | awk '/^Mem:/ {print $7}')
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
run_limited() {
  # Prepend optional scope properties without losing argument boundaries.
  [ -z "$_pri_cpu_quota" ] || set -- -p "CPUQuota=$_pri_cpu_quota" "$@"
  [ -z "$_pri_memory_max" ] || set -- -p "MemoryMax=$_pri_memory_max" "$@"
  run systemd-run --user --scope "$@"
}

fail() {
  echo "mesa-build: $*" >&2; exit 1;
}

run() {
  printf '+'
  printf ' [%s]' "$@"
  printf '\n'
  "$@"
}

activate_venv() {
  [ -f "$PYVENVDIR/bin/activate" ] && [ -x "$PYVENVDIR/bin/python3" ] ||
    fail "missing Python venv: $PYVENVDIR (run: make -C \"$TOP\" PYVENVDIR=\"$PYVENVDIR\" pyvenv)"
  . "$PYVENVDIR/bin/activate"
  export PATH="$HOST/bin:$PATH"
}

help() {
  cat <<'HELP'
Usage: builder/build_mesa3d.sh COMMAND
  check           check source trees, cross compiler and build prerequisites
  manifest        print current source revisions (does not fetch or checkout)
  all             check, then build/install all stages in dependency order
  deps-target     check zlib/libdrm in the supplied dependency sysroots
  llvm-host       native LLVM/Clang tools and libraries
  spirv-host      native SPIRV-Tools and LLVM-SPIRV-Translator
  glslang-host    native GLSL to SPIR-V compiler for lavapipe
  mesa-host       native mesa_clc and vtn_bindgen2
  libclc          host Clang -> 32/64-bit SPIR-V builtins, staged for target
  llvm-target     AArch64 LLVM/Clang libraries
  spirv-target    AArch64 SPIRV-Tools and LLVM-SPIRV-Translator
  mesa-configure  configure llvmpipe + lavapipe + Rusticl
  mesa-target     configure, build and stage Mesa
  test-tools      build/stage eglinfo, vulkaninfo, clinfo and their API loaders
  package         tar the staged target files and runtime environment
Variables: APP_PLATFORM SRC OUT HOST ROOT CROSS GCC_SYSROOT TARGET_SYSROOT HOST_SYSROOT
           HEADERS TEST_SRC JOBS (default based on available RAM).
TARGET_SYSROOT defaults to $SRC/build/sysroot-$APP_PLATFORM (qemuarm64).
HOST_SYSROOT defaults to $SRC/build/sysroot-ub20. General dependencies are not built.
TEST_SRC defaults to algae-bp/build/mesa3d-test-sources; see docs for source URLs.
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
  # Intentional splitting: TEST_TREES contains fixed, space-separated names.
  for tree in $TEST_TREES; do
    if [ -d "$TEST_SRC/$tree" ]; then
      printf '%s ' "$tree"
      git -C "$TEST_SRC/$tree" describe --always --dirty
      git -C "$TEST_SRC/$tree" rev-parse HEAD
    fi
  done
}

check() {
  for tree in llvm-project/llvm mesa3d spirv-tools spirv-llvm-translator glslang; do
    [ -d "$SRC/$tree" ] || fail "missing $SRC/$tree"
  done
  check_test_sources
  [ -f "$HEADERS/include/spirv/unified1/spirv.hpp" ] || fail "missing SPIRV-Headers: $HEADERS"
  for tool in cmake ninja meson pkg-config python3 gcc g++ rustc bindgen bison flex; do
    command -v "$tool" >/dev/null || fail "missing host tool: $tool (see docs/mesa3d-build.md)"
  done
  for tool in gcc g++ ar ranlib strip; do
    [ -x "${CROSS}$tool" ] || fail "missing ${CROSS}$tool"
  done
  machine=$("${CROSS}gcc" -dumpmachine)
  case "$machine" in
    aarch64*) ;;
    *) fail 'compiler must target AArch64' ;;
  esac
  [ -d "$GCC_SYSROOT" ] || fail "missing compiler sysroot: $GCC_SYSROOT"
  python3 -c 'import mako, yaml, packaging'
  dependency_setup
  deps_target
  dir=$(rustc --print target-libdir --target aarch64-unknown-linux-gnu)
  [ -d "$dir" ] || fail 'install Rust aarch64-unknown-linux-gnu standard libraries'
  run cmake --version
  run meson --version
  run rustc --version
  run bindgen --version
  manifest
}

prepare() {
  dependency_setup
  # Paths go into CMake/Meson files. Reject characters requiring special quoting.
  for path in "$SRC" "$OUT" "$HOST" "$ROOT" "$CROSS" "$GCC_SYSROOT" "$HEADERS" "$TEST_SRC" "$TARGET_SYSROOT" "$HOST_SYSROOT" "$cxx_runtime"; do
    case "$path" in
      *[[:space:]]*|*\'*|*\"*|*\;*|*\\*)
        fail "use absolute paths without whitespace/quotes/semicolons/backslashes: $path" ;;
      /*) ;;
      *) fail "use absolute paths: $path" ;;
    esac
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
set(CMAKE_FIND_ROOT_PATH "$ROOT" "$TARGET_SYSROOT" "$GCC_SYSROOT")
set(CMAKE_PREFIX_PATH "/usr" "/")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_C_FLAGS_INIT "$TARGET_INCLUDE_FLAGS")
set(CMAKE_CXX_FLAGS_INIT "$TARGET_INCLUDE_FLAGS")
set(CMAKE_EXE_LINKER_FLAGS_INIT "$TARGET_LINK_FLAGS")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "$TARGET_LINK_FLAGS")
CMAKE
}

# Keep package roots separate from the compiler's libc sysroot and install roots.
dependency_setup() {
  [ -d "$TARGET_SYSROOT" ] || fail "missing TARGET_SYSROOT: $TARGET_SYSROOT"
  [ -d "$HOST_SYSROOT" ] || fail "missing HOST_SYSROOT: $HOST_SYSROOT"
  TARGET_INCLUDE_FLAGS="-I$ROOT/usr/include -I$TARGET_SYSROOT/include -I$TARGET_SYSROOT/usr/include"
  HOST_INCLUDE_FLAGS="-I$HOST/include -I$HOST_SYSROOT/include -I$HOST_SYSROOT/usr/include"
  TARGET_LIBRARY_FLAGS="-L$ROOT/usr/lib -Wl,-rpath-link,$ROOT/usr/lib"
  HOST_LIBRARY_FLAGS="-L$HOST/lib -Wl,-rpath-link,$HOST/lib"
  HOST_LIBRARY_PATH="$HOST/lib"
  target_triplet=$("${CROSS}gcc" -dumpmachine)
  host_triplet=$(gcc -dumpmachine)
  for libdir in "$TARGET_SYSROOT/lib" "$TARGET_SYSROOT/lib64" \
      "$TARGET_SYSROOT/usr/lib" "$TARGET_SYSROOT/usr/lib64" \
      "$TARGET_SYSROOT/lib/$target_triplet" "$TARGET_SYSROOT/usr/lib/$target_triplet"; do
    TARGET_LIBRARY_FLAGS="$TARGET_LIBRARY_FLAGS -L$libdir -Wl,-rpath-link,$libdir"
  done
  for libdir in "$HOST_SYSROOT/lib" "$HOST_SYSROOT/lib64" \
      "$HOST_SYSROOT/usr/lib" "$HOST_SYSROOT/usr/lib64" \
      "$HOST_SYSROOT/lib/$host_triplet" "$HOST_SYSROOT/usr/lib/$host_triplet"; do
    HOST_LIBRARY_FLAGS="$HOST_LIBRARY_FLAGS -L$libdir -Wl,-rpath-link,$libdir"
    HOST_LIBRARY_PATH="$HOST_LIBRARY_PATH:$libdir"
  done
  # C programs linking target C++ DSOs also need their indirect dependencies.
  # GCC's C++ runtime can live outside its libc sysroot; -L alone does not
  # let ld resolve a DSO's DT_NEEDED entries when linking with the C driver.
  cxx_runtime=$("${CROSS}g++" --sysroot="$GCC_SYSROOT" -print-file-name=libstdc++.so.6)
  case "$cxx_runtime" in
    /*) [ -f "$cxx_runtime" ] || fail 'cannot locate target libstdc++.so.6' ;;
    *) fail 'cannot locate target libstdc++.so.6' ;;
  esac
  TARGET_LINK_FLAGS="$TARGET_LIBRARY_FLAGS -Wl,-rpath-link,$(dirname -- "$cxx_runtime")"
  TARGET_MESON_LINK_ARGS=$(python3 -c 'import shlex, sys; print(repr(shlex.split(sys.argv[1])))' "--sysroot=$GCC_SYSROOT $TARGET_LINK_FLAGS")
  HOST_MESON_LINK_ARGS=$(python3 -c 'import shlex, sys; print(repr(shlex.split(sys.argv[1])))' "$HOST_LIBRARY_FLAGS")
  mkdir -p "$OUT"
  for arch in host target; do
    cat > "$OUT/pkg-config-$arch" <<'WRAPPER'
#!/bin/sh
# Metadata is already relocated per package. Disable automatic prefix guessing.
exec pkg-config --dont-define-prefix "$@"
WRAPPER
    chmod +x "$OUT/pkg-config-$arch"
  done
}

deps_target() (
  target_env
  run "$PKG_CONFIG" --print-errors --exists zlib libdrm
  native_env
  run "$PKG_CONFIG" --print-errors --exists zlib
)

native_env() {
  python3 "$TOP/builder/mesa3d-pkgconfig.py" "$OUT/pkgconfig-host" \
    native "$HOST" relocate "$HOST_SYSROOT"
  unset PKG_CONFIG_SYSROOT_DIR PKG_CONFIG_LIBDIR
  export PKG_CONFIG_PATH="$OUT/pkgconfig-host"
  export PKG_CONFIG="$OUT/pkg-config-host"
  export CMAKE_PREFIX_PATH="$HOST:$HOST_SYSROOT:$HOST_SYSROOT/usr"
  export LD_LIBRARY_PATH="$HOST_LIBRARY_PATH${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  export LIBCLANG_PATH="$HOST/lib"
}

target_env() {
  python3 "$TOP/builder/mesa3d-pkgconfig.py" "$OUT/pkgconfig-target" \
    relocate "$ROOT" relocate "$TARGET_SYSROOT" relocate "$GCC_SYSROOT"
  # Each .pc file already points to its owning root; never rebase them globally.
  unset PKG_CONFIG_SYSROOT_DIR PKG_CONFIG_PATH CMAKE_PREFIX_PATH
  export PKG_CONFIG_LIBDIR="$OUT/pkgconfig-target"
  export PKG_CONFIG="$OUT/pkg-config-target"
  export LIBCLANG_PATH="$HOST/lib"
  export LD_LIBRARY_PATH="$HOST_LIBRARY_PATH${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
}

# Subshells keep helper variables and environment changes scoped to each build.
cmake_build() (
  name=$1 source=$2 arch=$3; shift 3
  set -- -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_POSITION_INDEPENDENT_CODE=ON "$@"
  if [ "$arch" = target ]; then
    target_env
    set -- -DCMAKE_TOOLCHAIN_FILE="$OUT/target.cmake" -DCMAKE_INSTALL_PREFIX=/usr \
      -DCMAKE_C_FLAGS="$TARGET_INCLUDE_FLAGS" -DCMAKE_CXX_FLAGS="$TARGET_INCLUDE_FLAGS" \
      -DCMAKE_EXE_LINKER_FLAGS="$TARGET_LINK_FLAGS" \
      -DCMAKE_SHARED_LINKER_FLAGS="$TARGET_LINK_FLAGS" "$@"
  else
    native_env
    set -- -DCMAKE_C_COMPILER="$(command -v gcc)" -DCMAKE_CXX_COMPILER="$(command -v g++)" \
      -DCMAKE_AR="$(command -v ar)" -DCMAKE_RANLIB="$(command -v ranlib)" -DCMAKE_INSTALL_PREFIX="$HOST" \
      -DCMAKE_PREFIX_PATH="$HOST;$HOST_SYSROOT;$HOST_SYSROOT/usr" \
      -DCMAKE_C_FLAGS="$HOST_INCLUDE_FLAGS" -DCMAKE_CXX_FLAGS="$HOST_INCLUDE_FLAGS" \
      -DCMAKE_EXE_LINKER_FLAGS="$HOST_LIBRARY_FLAGS" \
      -DCMAKE_SHARED_LINKER_FLAGS="$HOST_LIBRARY_FLAGS" "$@"
  fi
  set -- -DPKG_CONFIG_EXECUTABLE="$PKG_CONFIG" "$@"
  run cmake -G Ninja -S "$source" -B "$OUT/$name" "$@"
  run_limited cmake --build "$OUT/$name" --parallel "$JOBS"
  if [ "$arch" = target ]; then
    run env DESTDIR="$ROOT" cmake --install "$OUT/$name"
  else
    run cmake --install "$OUT/$name"
  fi
)

llvm() (
  arch=$1
  set --
  if [ "$arch" = target ]; then
    [ -x "$HOST/bin/llvm-tblgen" ] || fail 'run llvm-host first'
    set -- -DLLVM_USE_HOST_TOOLS=ON -DLLVM_NATIVE_TOOL_DIR="$OUT/llvm-host/bin" \
      -DLLVM_TABLEGEN="$OUT/llvm-host/bin/llvm-tblgen" \
      -DCLANG_TABLEGEN="$OUT/llvm-host/bin/clang-tblgen" \
      -DLLVM_DEFAULT_TARGET_TRIPLE=aarch64-unknown-linux-gnu \
      -DLLVM_HOST_TRIPLE=aarch64-unknown-linux-gnu
  fi
  cmake_build "llvm-$arch" "$SRC/llvm-project/llvm" "$arch" \
    -DLLVM_ENABLE_PROJECTS=clang -DLLVM_TARGETS_TO_BUILD="$LLVM_TARGETS" \
    -DLLVM_DEFAULT_TARGET_TRIPLE=aarch64-unknown-linux-gnu -DLLVM_BUILD_LLVM_DYLIB=ON -DLLVM_LINK_LLVM_DYLIB=ON \
    -DCLANG_LINK_CLANG_DYLIB=ON -DLLVM_ENABLE_RTTI=ON -DLLVM_ENABLE_DUMP=ON \
    -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF \
    -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF \
    -DLLVM_ENABLE_TERMINFO=OFF -DLLVM_ENABLE_LIBEDIT=OFF "$@"
)

spirv() (
  arch=$1 prefix=$HOST
  [ "$arch" = target ] && prefix="$ROOT/usr"
  cmake_build "spirv-tools-$arch" "$SRC/spirv-tools" "$arch" \
    -DSPIRV-Headers_SOURCE_DIR="$HEADERS" -DSPIRV_SKIP_TESTS=ON \
    -DSPIRV_SKIP_EXECUTABLES=ON -DSPIRV_TOOLS_BUILD_STATIC=ON
  cmake_build "translator-$arch" "$SRC/spirv-llvm-translator" "$arch" \
    -DLLVM_DIR="$prefix/lib/cmake/llvm" -DLLVM_EXTERNAL_SPIRV_HEADERS_SOURCE_DIR="$HEADERS" \
    -DLLVM_SPIRV_INCLUDE_TESTS=OFF -DLLVM_SPIRV_ENABLE_LIBSPIRV_DIS=OFF \
    -DBUILD_SHARED_LIBS=ON -DLLVM_LINK_LLVM_DYLIB=ON
)

meson_setup() (
  dir=$1; shift
  [ ! -f "$dir/meson-private/coredata.dat" ] || set -- --reconfigure "$@"
  run meson setup "$dir" "$SRC/mesa3d" --wrap-mode=nofallback "$@"
)

glslang_host() {
  # Mesa's Vulkan BVH shaders need a native compiler, without optimizer passes.
  cmake_build glslang-host "$SRC/glslang" host \
    -DBUILD_EXTERNAL=OFF -DENABLE_OPT=OFF -DGLSLANG_TESTS=OFF \
    -DGLSLANG_ENABLE_INSTALL=ON -DENABLE_GLSLANG_BINARIES=ON
}

mesa_host() {
  native_env
  cat > "$OUT/host-native.ini" <<MESON
[binaries]
c = '$(command -v gcc)'
cpp = '$(command -v g++)'
pkg-config = '$OUT/pkg-config-host'
[built-in options]
c_args = ['-I$HOST/include', '-I$HOST_SYSROOT/include', '-I$HOST_SYSROOT/usr/include']
cpp_args = ['-I$HOST/include', '-I$HOST_SYSROOT/include', '-I$HOST_SYSROOT/usr/include']
c_link_args = $HOST_MESON_LINK_ARGS
cpp_link_args = $HOST_MESON_LINK_ARGS
MESON
  meson_setup "$OUT/mesa-host" --native-file="$OUT/host-native.ini" --prefix="$HOST" --libdir=lib --buildtype=release \
    -Dgallium-drivers=[] -Dvulkan-drivers=[] -Dplatforms=[] -Dglx=disabled \
    -Dopengl=false -Degl=disabled -Dgbm=disabled -Dgles1=disabled -Dgles2=disabled \
    -Dllvm=enabled -Dshared-llvm=enabled -Dmesa-clc=enabled -Dinstall-mesa-clc=true
  run_limited meson compile -C "$OUT/mesa-host" -j "$JOBS"
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
  [ -x "$HOST/bin/mesa_clc" ] || fail 'run mesa-host first'
  [ -f "$ROOT/usr/lib/libLLVM.so" ] || fail 'run llvm-target first'
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
pkg-config = '$OUT/pkg-config-target'
llvm-config = '$OUT/llvm-config-target'
rust = ['rustc', '--target', 'aarch64-unknown-linux-gnu']
[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
[properties]
needs_exe_wrapper = true
pkg_config_libdir = ['$OUT/pkgconfig-target']
bindgen_clang_arguments = ['-I$ROOT/usr/include', '-I$TARGET_SYSROOT/include', '-I$TARGET_SYSROOT/usr/include', '--sysroot=$GCC_SYSROOT', '--gcc-toolchain=$(dirname -- "$(dirname -- "${CROSS}gcc")")']
[built-in options]
c_args = ['--sysroot=$GCC_SYSROOT', '-I$ROOT/usr/include', '-I$TARGET_SYSROOT/include', '-I$TARGET_SYSROOT/usr/include']
cpp_args = ['--sysroot=$GCC_SYSROOT', '-I$ROOT/usr/include', '-I$TARGET_SYSROOT/include', '-I$TARGET_SYSROOT/usr/include']
c_link_args = $TARGET_MESON_LINK_ARGS
cpp_link_args = $TARGET_MESON_LINK_ARGS
rust_link_args = ['-C', 'linker=${CROSS}gcc', '-C', 'link-args=--sysroot=$GCC_SYSROOT $TARGET_LINK_FLAGS']
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
  run_limited meson compile -C "$OUT/mesa-target" -j "$JOBS"
  run env DESTDIR="$ROOT" meson install --no-rebuild -C "$OUT/mesa-target"
  runtime_env
}

check_test_sources() {
  # Intentional splitting: TEST_TREES contains fixed, space-separated names.
  for tree in $TEST_TREES; do
    [ -d "$TEST_SRC/$tree" ] || fail "missing $TEST_SRC/$tree (see test-tools source setup in docs/mesa3d-build.md)"
  done
}

test_tools() {
  check_test_sources
  [ -f "$ROOT/usr/lib/pkgconfig/egl.pc" ] || fail 'run mesa-target before test-tools'
  set -- -DBUILD_WSI_XCB_SUPPORT=OFF -DBUILD_WSI_XLIB_SUPPORT=OFF \
    -DBUILD_WSI_WAYLAND_SUPPORT=OFF -DBUILD_WSI_DIRECTFB_SUPPORT=OFF
  cmake_build vulkan-headers-target "$TEST_SRC/vulkan-headers" target -DBUILD_TESTING=OFF
  cmake_build vulkan-loader-target "$TEST_SRC/vulkan-loader" target \
    -DVulkanHeaders_DIR="$ROOT/usr/share/cmake/VulkanHeaders" \
    -DBUILD_TESTS=OFF -DBUILD_WSI_XLIB_XRANDR_SUPPORT=OFF "$@"
  cmake_build vulkan-tools-target "$TEST_SRC/vulkan-tools" target \
    -DVulkanHeaders_DIR="$ROOT/usr/share/cmake/VulkanHeaders" \
    -DVulkanLoader_DIR="$ROOT/usr/lib/cmake/VulkanLoader" \
    -DBUILD_CUBE=OFF -DBUILD_ICD=OFF -DBUILD_TESTS=OFF -DBUILD_VULKANINFO=ON "$@"
  cmake_build opencl-headers-target "$TEST_SRC/opencl-headers" target \
    -DOPENCL_HEADERS_BUILD_TESTING=OFF
  cmake_build opencl-loader-target "$TEST_SRC/opencl-icd-loader" target \
    -DOPENCL_ICD_LOADER_HEADERS_DIR="$ROOT/usr/include" \
    -DOPENCL_ICD_LOADER_BUILD_TESTING=OFF -DBUILD_TESTING=OFF -DENABLE_OPENCL_LAYERS=OFF

  # clinfo is a single translation unit. Compile out of tree, leaving its
  # upstream checkout untouched (the upstream Makefile builds in source).
  mkdir -p "$OUT/clinfo-target" "$ROOT/usr/bin"
  # Intentional splitting: validated paths in TARGET_LINK_FLAGS contain no spaces.
  run_limited "${CROSS}gcc" --sysroot="$GCC_SYSROOT" \
    -O2 -std=c99 -I"$ROOT/usr/include" -I"$TARGET_SYSROOT/include" -I"$TARGET_SYSROOT/usr/include" \
    "$TEST_SRC/clinfo/src/clinfo.c" \
    $TARGET_LINK_FLAGS \
    -o "$OUT/clinfo-target/clinfo" -lOpenCL -ldl
  run install -m 755 "$OUT/clinfo-target/clinfo" "$ROOT/usr/bin/clinfo"

  target_env
  cat > "$OUT/test-tools-cross.ini" <<MESON
[binaries]
c = '${CROSS}gcc'
cpp = '${CROSS}g++'
ar = '${CROSS}ar'
strip = '${CROSS}strip'
pkg-config = '$OUT/pkg-config-target'
[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
[properties]
needs_exe_wrapper = true
[built-in options]
c_args = ['--sysroot=$GCC_SYSROOT', '-I$ROOT/usr/include', '-I$TARGET_SYSROOT/include', '-I$TARGET_SYSROOT/usr/include']
cpp_args = ['--sysroot=$GCC_SYSROOT', '-I$ROOT/usr/include', '-I$TARGET_SYSROOT/include', '-I$TARGET_SYSROOT/usr/include']
c_link_args = $TARGET_MESON_LINK_ARGS
cpp_link_args = $TARGET_MESON_LINK_ARGS
MESON
  set --
  [ ! -f "$OUT/mesa-demos-target/meson-private/coredata.dat" ] || set -- --reconfigure
  run meson setup "$@" "$OUT/mesa-demos-target" "$TEST_SRC/mesa-demos" \
    --cross-file="$OUT/test-tools-cross.ini" --prefix=/usr --libdir=lib \
    --buildtype=release --wrap-mode=nofallback -Dauto_features=disabled -Degl=enabled
  run_limited meson compile -C "$OUT/mesa-demos-target" -j "$JOBS"
  run env DESTDIR="$ROOT" meson install --no-rebuild -C "$OUT/mesa-demos-target"
  for tool in eglinfo vulkaninfo clinfo; do
    [ -x "$ROOT/usr/bin/$tool" ] || fail "test-tools did not install $tool"
  done
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
all|deps-target|llvm-host|llvm-target|spirv-host|spirv-target|glslang-host|mesa-host|libclc|mesa-configure|mesa-target|test-tools|package)
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
  llvm host
  spirv host
  glslang_host
  mesa_host
  libclc
  llvm target
  spirv target
  mesa_target
  test_tools
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
test-tools)
  test_tools
  ;;
package)
  [ -f "$ROOT/usr/lib/libRusticlOpenCL.so" ] || fail 'build Mesa before packaging'
  runtime_env
  run tar -C "$ROOT" -cJf "$OUT/mesa3d-aarch64.tar.xz" .
  ;;
esac
