# AArch64 Mesa software stack

`builder/build_mesa3d.sh` builds the sibling upstream checkouts without applying
patches, fetching, changing branches, or reusing the older experimental caches.
It targets Linux/glibc on BeaglePlay and full-system QEMU AArch64. It uses the
current checkouts, not a promise that these are the latest upstream commits.
Update them deliberately before building, keeping LLVM and the translator on
matching major/minor versions. `manifest` prints commits and dirty state.

The initial profile provides OpenGL/OpenGL ES through llvmpipe, Vulkan through
lavapipe (`swrast` in Mesa's Vulkan build options), and OpenCL through
Rusticl/llvmpipe. EGL surfaceless and GBM are enabled. X11, GLX and Wayland are
not included; this is suitable for headless API tests, not a desktop image.

## Build

From algae-bp, create the build-tools environment once if needed:

```sh
make pyvenv
./builder/build_mesa3d.sh check
JOBS=4 ./builder/build_mesa3d.sh all
./builder/build_mesa3d.sh package
```

The script automatically activates `algae-bp/.venv` for `check` and every
build/package stage. Set `PYVENVDIR=/absolute/path/to/venv` to use another
environment. `help` and `manifest` do not require a venv.

Build PC prerequisites: GCC/G++, CMake, Ninja, Meson, pkg-config, Python with
Mako/PyYAML/packaging, Bison, Flex, Rust and bindgen-cli. General development
packages come from `HOST_SYSROOT` for native builds and `TARGET_SYSROOT` for
AArch64 builds. Supply zlib in both roots and libdrm in the target root; the
script no longer builds zlib or libdrm. Native pkg-config may also use the build
PC's system packages as a fallback. Mesa's configuration reports any additional
missing dependencies. Use the checked-out Mesa requirements
as the authority: currently Meson >=1.7 and Rust >=1.82, bindgen >=0.71.1 with
some versions explicitly rejected in `meson.build`. LLVM 24 recommends CMake
>=3.31. Host bindgen needs a native libclang (the host LLVM stage supplies it).
Rust must include target standard libraries:

> Ubuntu 26.04 install rust
> 
> ```sh
> apt install rustup
> rustup default stable
> ```

```sh
rustup target add aarch64-unknown-linux-gnu
```

The script does not install host packages automatically. `check` is a basic
prerequisite check; each project's configure step performs version and API
compatibility checks. Building LLVM twice needs substantial disk space and
memory; lower JOBS if the build runs out of memory.

Defaults:

- Sources: parent of algae-bp (`llvm-project`, `mesa3d`, `spirv-tools`,
  `spirv-llvm-translator`, `glslang`). SPIRV-Headers defaults to
  `spirv-tools/external/spirv-headers`; override `HEADERS` if needed.
- Compiler: `cross/aarch64-linux-gnu/bin/aarch64-linux-gnu-` beneath algae-bp.
- Output: `algae-bp/build/mesa3d-stack`; override `OUT` to start a fresh build.
- Native tools/libraries: `$OUT/host`.
- Target staging tree: `$OUT/root`, installed with prefix `/usr` and DESTDIR.
- Compiler libc sysroot: queried from GCC; override `GCC_SYSROOT` if required.
- General target packages: `TARGET_SYSROOT=$SRC/build/sysroot-$APP_PLATFORM`,
  with `APP_PLATFORM=qemuarm64` by default.
- General native packages: `HOST_SYSROOT=$SRC/build/sysroot-ub20`.

The compiler sysroot supplies libc and the C++ runtime. `TARGET_SYSROOT` and
`HOST_SYSROOT` are dependency inputs, not installation destinations or replacements
for GCC's libc sysroot. Both root-level `include`/`lib` and `usr/include`/`usr/lib`
layouts are supported, including lib64 and compiler-triplet library directories.
Mesa/LLVM/SPIR-V outputs remain under `$OUT/root` and `$OUT/host` and take
precedence over general dependencies.

CMake, Meson, bindgen and direct compiler invocations use the appropriate input
root. The script generates per-architecture pkg-config metadata under
`$OUT/pkgconfig-{host,target}` through `builder/mesa3d-pkgconfig.py`. Each file's
paths are relocated relative to its owning root, so `prefix=`, `prefix=/` and
`prefix=/usr` can coexist without a global `PKG_CONFIG_SYSROOT_DIR`. Source
metadata is never edited. Target discovery excludes build-PC system packages;
native discovery retains system fallback.

Use a fresh `OUT` when adopting these dependency roots or changing sources,
compiler, architecture or sysroot. Existing CMake/Meson caches and old staged
zlib/libdrm files can otherwise retain the previous dependency selection. The
script never removes an existing build cache or staged library automatically.
Paths must be absolute and contain no whitespace, quotes, backslashes or semicolons.

```sh
TARGET_SYSROOT=/path/to/target-packages \
HOST_SYSROOT=/path/to/native-packages \
OUT="$PWD/build/mesa3d-stack-sysroots" ./builder/build_mesa3d.sh all
```

## Stages and architecture

| Stage | Executes on build PC | Installed for AArch64 |
| --- | --- | --- |
| deps-target | pkg-config checks supplied target zlib/libdrm and host zlib | none |
| llvm-host | LLVM/Clang, table generators, llvm-config, libclang | none |
| spirv-host | native libraries for Mesa's compiler tools | none |
| glslang-host | native glslangValidator for lavapipe's Vulkan shaders | none |
| mesa-host | mesa_clc, vtn_bindgen2 | none |
| libclc | host Clang, opt, llvm-link | 32/64-bit SPIR-V builtins |
| llvm-target | host table generators, cross GCC | LLVM/Clang libraries and headers |
| spirv-target | cross GCC | SPIRV-Tools and LLVMSPIRVLib |
| mesa-target | Meson, bindgen, Rust, host Mesa tools, cross GCC | Mesa, Rusticl, Vulkan ICD |

Individual stage names are CLI commands and do not recursively build their
prerequisites. `all` runs them in the table's order. LLVM target installation may
also include AArch64 tools; these are never used as build-PC tools.

The target CMake linker flags include the cross compiler's C++ runtime directory
via `-rpath-link`. This lets C executables resolve `libLLVM.so`'s indirect
`libstdc++.so.6` dependency even when GCC installs it outside its libc sysroot.
The script refreshes these flags in existing CMake caches on each run.
The Mesa cross file also passes the compiler's libc sysroot and GCC installation
prefix to bindgen's native libclang, so Rusticl bindings use AArch64 C and C++
headers rather than build-PC headers.

Mesa's LLVM dependency requires a runnable `llvm-config`. The generated Python
adapter queries native LLVM and rewrites its installation prefix to the staged
AArch64 prefix. Both LLVM builds therefore deliberately use the same source,
target backends, RTTI and shared-library settings. This is intended for shared
LLVM only. Do not point HOST at an unrelated LLVM installation or use this
adapter for a static LLVM build. `--host-target` explicitly reports AArch64.

Standalone LLVM 24 libclc no longer emits the pkg-config metadata Mesa expects.
The script creates `libclc.pc` in the staging tree, describing only its SPIR-V
data directory. Mesa already recognizes `spirv32-unknown-unknown/libclc.spv` and
`spirv64-unknown-unknown/libclc.spv`; no source patch or renamed SPIR-V module is
needed. Dynamic libclc loading is used. Upstream Mesa warns that upstream libclc
is less reliable than its recommended Mesa libclc fork. This script honors the
request to use upstream LLVM, so OpenCL kernel correctness needs runtime tests.

## Deployment and testing

The package is a staging overlay, not a complete root filesystem. Inspect it
before extracting its `usr/` tree into your BeaglePlay or QEMU guest rootfs.
It includes development files and LLVM tools and is not size optimized.
The guest needs compatible glibc/libstdc++/libgcc from the cross toolchain,
plus runtime dependencies (including target zlib/libdrm), a Vulkan loader,
OpenCL ICD loader and API test applications. `test-tools` builds the loaders and
`eglinfo`, `vulkaninfo`, `clinfo` from `TEST_SRC`; it also runs as part of `all`.
General packages from the input sysroots are not copied into the Mesa archive.
Mesa installs its ICD manifests.
QEMU user emulation is not used during the cross build. Full-system QEMU can
run the same software stack without GPU passthrough.

After installing into the guest's `/usr`:

```sh
. /mesa-software-env.sh
vulkaninfo --summary
clinfo
# Run an EGL surfaceless OpenGL/OpenGL ES test, e.g. your own EGL pbuffer test.
```

Expect llvmpipe for GL, lavapipe/llvmpipe for Vulkan, and a Rusticl llvmpipe
OpenCL device. Enumeration alone is insufficient: exercise a rendered frame,
a Vulkan compute or rendering workload, and an OpenCL vector-add kernel with
results checked on the CPU. The generated environment forces CPU rendering and
selects the lavapipe ICD. It assumes deployment under `/usr`; changing the
installation location requires adjusting the environment and ICD paths.

## BeaglePlay hardware follow-up

BeaglePlay has an AXE-1-16M PowerVR Rogue GPU. Upstream Mesa's `imagination`
Vulkan driver supports this family in active development; check the actual GPU
BVNC, kernel powervr DRM support, firmware and permissions. Current Mesa docs
require `PVR_I_WANT_A_BROKEN_VULKAN_DRIVER=1` for nonconformant GPUs, including
AXE-1-16M. OpenGL can use Zink over that Vulkan driver. That profile also needs
native Mesa shader compiler/precomp tools and additional dependencies; it is
not enabled by this software script. Rusticl's llvmpipe device provides CPU
OpenCL and does not establish OpenCL acceleration on PowerVR. Hardware support
should be validated independently before adding a GPU profile.

References:

- https://docs.mesa3d.org/drivers/powervr.html
- https://www.beagleboard.org/boards/beagleplay
- https://docs.mesa3d.org/rusticl.html
- https://docs.mesa3d.org/envvars.html

Build validation (2026-10-18): LLVM, SPIRV-Tools, LLVM-SPIRV-Translator,
glslang and Mesa/Rusticl built and installed into the staging directories.
BeaglePlay/QEMU runtime tests have not been completed.
