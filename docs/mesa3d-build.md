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

From algae-bp, activate your build-tools environment, for example:

```sh
. .venv/bin/activate
./builder/build_mesa3d.sh check
JOBS=4 ./builder/build_mesa3d.sh all
./builder/build_mesa3d.sh package
```

Build PC prerequisites: GCC/G++, CMake, Ninja, Meson, pkg-config, Python with
Mako/PyYAML/packaging, Bison, Flex, Rust and bindgen-cli. Host development
packages include zlib, libdrm, libexpat and libzstd; Mesa's host configuration
reports any other missing dependencies. Use the checked-out Mesa requirements
as the authority: currently Meson >=1.7 and Rust >=1.82, bindgen >=0.71.1 with
some versions explicitly rejected in `meson.build`. LLVM 24 recommends CMake
>=3.31. Host bindgen needs a native libclang (the host LLVM stage supplies it).
Rust must include target standard libraries:

```sh
rustup target add aarch64-unknown-linux-gnu
```

The script does not install host packages automatically. `check` is a basic
prerequisite check; each project's configure step performs version and API
compatibility checks. Building LLVM twice needs substantial disk space and
memory; lower JOBS if the build runs out of memory.

Defaults:

- Sources: parent of algae-bp (`llvm-project`, `mesa3d`, `spirv-tools`,
  `spirv-llvm-translator`, `zlib`, `libdrm`). SPIRV-Headers defaults to
  `spirv-tools/external/spirv-headers`; override `HEADERS` if needed.
- Compiler: `cross/aarch64-linux-gnu/bin/aarch64-linux-gnu-` beneath algae-bp.
- Output: `algae-bp/build/mesa3d-stack`; override `OUT` to start a fresh build.
- Native tools/libraries: `$OUT/host`.
- Target staging tree: `$OUT/root`, installed with prefix `/usr` and DESTDIR.
- Compiler libc sysroot: queried from GCC; override `GCC_SYSROOT` if required.

The compiler sysroot supplies libc and the C++ runtime. The staging tree supplies
project dependencies. They remain separate. Target pkg-config searches only
explicit target directories and rewrites `/usr` paths into the staging tree.
Use a fresh OUT after source-version, compiler, architecture or sysroot changes;
the script never removes an existing cache automatically. Paths must be absolute
and contain no spaces, quotes, backslashes or semicolons.

## Stages and architecture

| Stage | Executes on build PC | Installed for AArch64 |
| --- | --- | --- |
| deps-target | Meson/CMake, cross GCC | zlib, libdrm |
| llvm-host | LLVM/Clang, table generators, llvm-config, libclang | none |
| spirv-host | native libraries for Mesa's compiler tools | none |
| mesa-host | mesa_clc, vtn_bindgen2 | none |
| libclc | host Clang, opt, llvm-link | 32/64-bit SPIR-V builtins |
| llvm-target | host table generators, cross GCC | LLVM/Clang libraries and headers |
| spirv-target | cross GCC | SPIRV-Tools and LLVMSPIRVLib |
| mesa-target | Meson, bindgen, Rust, host Mesa tools, cross GCC | Mesa, Rusticl, Vulkan ICD |

Individual stage names are CLI commands and do not recursively build their
prerequisites. `all` runs them in the table's order. LLVM target installation may
also include AArch64 tools; these are never used as build-PC tools.

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
plus a Vulkan loader, OpenCL ICD loader and API test applications. These loaders
and applications are not built by this script. Mesa installs its ICD manifests.
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

Validation on creation: Bash syntax and command dispatch checked. AArch64 zlib
and libdrm were built and staged successfully; their ELF architecture was
verified. The current
build environment lacks Rust/bindgen, so the complete LLVM/Mesa/Rusticl build
and BeaglePlay/QEMU runtime tests have not been completed.
