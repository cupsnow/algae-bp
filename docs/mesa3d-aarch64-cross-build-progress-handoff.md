# Mesa3D AArch64 Cross-Compilation --- Progress Handoff

## Purpose

This document summarizes the previous debugging session so a new chat
can restart the Mesa3D cross-compilation work with a cleaner
architecture.

Target: cross-compile Mesa3D on an **x86-64 Ubuntu PC** for **BeaglePlay
/ AArch64 Linux**, providing:

-   OpenGL software rendering via **llvmpipe**
-   Vulkan software rendering via **lavapipe**
-   OpenCL via **Rusticl**

This is a progress handoff, not a statement that the complete build
succeeds.

## 1. Workspace and toolchain

Approximate variables used:

``` bash
export WS="$HOME/02_dev/algae-ws"
export TOP="$WS/algae-bp"
export SRC="$WS"
export BUILD="$WS/build"

export CROSS="$TOP/cross/aarch64-linux-gnu"
export GCC_SYSROOT="$CROSS/aarch64-linux-gnu/sysroot"
export LLVM_HOST="$TOP/tool/llvm-host"
```

The environment for compile native tool

``` bash
unset CC
unset CXX
unset AR
unset RANLIB
unset STRIP
```

The environment for compile cross tool

``` bash
export CC="$CROSS/bin/aarch64-linux-gnu-gcc"
export CXX="$CROSS/bin/aarch64-linux-gnu-g++"
export AR="$CROSS/bin/aarch64-linux-gnu-ar"
export RANLIB="$CROSS/bin/aarch64-linux-gnu-ranlib"
export STRIP="$CROSS/bin/aarch64-linux-gnu-strip"
```

There is also a separate BeaglePlay development/platform sysroot
containing target headers, libraries, and pkg-config files.

Important design choice:

``` text
GCC sysroot != BeaglePlay development/platform sysroot
```

Do not merge these blindly.

Known source/tool versions are approximately:

``` text
Cross compiler: aarch64-linux-gnu GCC 13.2.0
Mesa source:    Mesa 26.3.0-devel
LLVM source:    LLVM 24.0.0git
```

## 2. Most important architectural lesson

Classify every component according to **where it executes**.

There are three worlds:

``` text
x86-64 BUILD PC
    |
    +-- HOST tools
    |
    | cross compile
    v
AArch64 TARGET
    |
    +-- target programs/libraries

Separately:
    LLVM IR / LLVM bitcode / SPIR-V
    are compiler/device representations, not native ELF executables.
```

### HOST

Programs executed on the x86-64 build PC, for example:

``` text
clang
llvm-config
llvm-tblgen
llvm-min-tblgen
spirv-as
spirv-dis
spirv-opt
spirv-link
spirv-val
possibly llvm-spirv
```

These must be x86-64 executables when used during the cross build.

### TARGET

Programs/libraries that execute on BeaglePlay:

``` text
Mesa
libGL / EGL
llvmpipe
lavapipe
Rusticl
target LLVM libraries
target libraries required by Mesa/Rusticl
```

These must be AArch64.

### SPIR-V artifacts

OpenCL/device code may exist as LLVM IR, LLVM bitcode, or SPIR-V. These
are not x86-64 or AArch64 ELF executables.

## 3. Host LLVM --- confirmed working

Host LLVM was built/installed at approximately:

``` text
$LLVM_HOST
```

It was rebuilt with targets:

``` text
X86 AArch64 SPIRV
```

Verification:

``` bash
"$LLVM_HOST/bin/llvm-config" --version
"$LLVM_HOST/bin/llvm-config" --targets-built
```

produced:

``` text
24.0.0git
X86 AArch64 SPIRV
```

This is a confirmed good result.

Host Clang also successfully accepted an OpenCL C compilation targeting
SPIR-V:

``` bash
"$LLVM_HOST/bin/clang" \
    --target=spirv64-unknown-unknown \
    -x cl -c -flto -disable-llvm-passes \
    test.cl -o test-spv.o
```

The output of that particular test was LLVM bitcode, so interpret this
as confirmation that host LLVM/Clang has the necessary SPIR-V target
support and accepts the target---not that this exact command necessarily
emitted final SPIR-V.

## 4. Target LLVM --- AArch64

An AArch64 LLVM build was created for eventual target Mesa use.

Important configuration concepts:

``` text
LLVM_TARGETS_TO_BUILD=AArch64
LLVM_DEFAULT_TARGET_TRIPLE=aarch64-linux-gnu
LLVM_BUILD_LLVM_DYLIB=ON
LLVM_LINK_LLVM_DYLIB=ON
LLVM_USE_HOST_TOOLS=ON
LLVM_NATIVE_TOOL_DIR=$LLVM_HOST/bin
```

A cross-build problem occurred when an AArch64 `llvm-min-tblgen` was
executed on the x86-64 PC, producing an error similar to:

``` text
qemu-aarch64: Could not open '/lib/ld-linux-aarch64.so.1'
```

The solution was to use host LLVM tools:

``` text
LLVM_USE_HOST_TOOLS=ON
LLVM_NATIVE_TOOL_DIR=$LLVM_HOST/bin
```

A host `llvm-min-tblgen` existed in the host LLVM build tree but was not
installed in `$LLVM_HOST/bin`; a host x86-64 copy was provided there.

## 5. SPIRV-Tools

SPIRV-Tools was built during the original attempt, including an AArch64
staged build. Artifacts included:

``` text
spirv-as
spirv-dis
spirv-opt
spirv-link
spirv-val
libSPIRV-Tools*
```

The staged pkg-config version was approximately `2026.4.1`.

### Correction for restart

Do not classify the whole project as target-only.

Command-line tools used during cross compilation should be HOST/x86-64.
If Mesa/Rusticl links SPIRV-Tools libraries into target software, those
libraries must be TARGET/AArch64.

Potential architecture:

``` text
SPIRV-Tools
├── HOST build: x86-64 tools
└── TARGET build: AArch64 libraries, if required
```

Verify the exact Mesa 26.3 requirements before deciding which target
components are necessary.

## 6. LLVM-SPIRV-Translator

`spirv-llvm-translator` was built against LLVM 24 and produced artifacts
including:

``` text
llvm-spirv
LLVMSPIRVLib headers
libLLVMSPIRVLib.a
LLVMSPIRVLib.pc
```

An AArch64 `llvm-spirv` cannot serve as a build-time executable on the
x86-64 PC.

For the restart, explicitly determine whether the desired architecture
requires:

``` text
HOST:   llvm-spirv
TARGET: LLVMSPIRVLib
```

or another division.

Conceptually:

``` text
LLVM IR / LLVM bitcode <--> SPIR-V
```

LLVM bitcode is not native machine code, and SPIR-V is not an AArch64
executable.

## 7. What libclc is

`libclc` is not the OpenCL runtime. Mesa **Rusticl** is the OpenCL
implementation/runtime.

libclc provides compiler/kernel-side implementations of OpenCL/compiler
built-in functions such as functions used by kernels (`sin`, `sqrt`,
`get_global_id`, etc.).

Conceptually:

``` text
OpenCL kernel
├── kernel code
└── builtin implementations
        |
        v
      libclc
```

## 8. libclc.spv result

The nested `spirv64-unknown-unknown` libclc build reached:

``` text
[283/288] Linking CLC static library ...
[285/288] Linking CLC static library .../libclc.a
[287/288] Generating libclc-spirv64-unknown-unknown.linked.bc
[288/288] Generating .../spirv64-unknown-unknown/libclc.spv
```

Therefore a significant sub-build successfully generated `libclc.spv`.

However, **do not record the overall libclc build/install as complete**.
Broader libclc integration remained unresolved.

Safe statement:

> The nested `spirv64-unknown-unknown` libclc build successfully reached
> generation of `libclc.spv`.

## 9. Major libclc cross-build problem solved

Initially the nested runtime selected:

``` text
CMAKE_CLC_COMPILER = aarch64-linux-gnu-gcc
```

and attempted effectively:

``` text
aarch64-linux-gnu-gcc --target=spirv64-unknown-unknown
```

which failed because GCC did not recognize that Clang-style target
option.

The correct separation is:

``` text
CMAKE_C_COMPILER   = aarch64-linux-gnu-gcc
CMAKE_CLC_COMPILER = $LLVM_HOST/bin/clang
```

Normal target C code is built by the AArch64 cross compiler. OpenCL
C/CLC compilation is performed by x86-64 host Clang, targeting
`spirv64-unknown-unknown`.

## 10. Propagating CMAKE_CLC_COMPILER

LLVM runtimes CMake supports per-runtime passthrough variables:

``` text
RUNTIMES_<target>_<variable>
```

Therefore the outer configuration was given:

``` bash
-DRUNTIMES_spirv64-unknown-unknown_CMAKE_CLC_COMPILER="$LLVM_HOST/bin/clang"
```

The outer cache then showed the host Clang path, and the generated
nested configure command contained:

``` text
-DCMAKE_CLC_COMPILER=.../tool/llvm-host/bin/clang
-DLIBCLC_USE_SPIRV_BACKEND=ON
```

This fix should be retained in the revised build script.

## 11. libclc nested build directory

The ExternalProject management area is:

``` text
libclc-spirv-build/projects/runtimes-spirv64-unknown-unknown/
```

but the actual nested runtime build directory is:

``` text
libclc-spirv-build/runtimes/runtimes-spirv64-unknown-unknown-bins/
├── CMakeCache.txt
├── build.ninja
├── CMakeFiles/
└── libclc/
```

Use:

``` bash
RUNTIME_BUILD="$BUILD/libclc-spirv-build/runtimes/runtimes-spirv64-unknown-unknown-bins"
```

when inspecting the nested runtime build.

## 12. OpenCL application vs kernel

The CPU-side OpenCL application and OpenCL kernel are separate.

AArch64 application:

``` text
main.c
  |
  | aarch64-linux-gnu-gcc
  v
my_opencl_app
(AArch64 ELF)
```

Kernel:

``` text
kernel.cl
  |
  | compiler
  v
LLVM IR / SPIR-V
```

Conceptually on BeaglePlay:

``` text
AArch64 application
        |
        | OpenCL API
        v
      Rusticl
        ^
        |
      SPIR-V
        |
        v
Mesa compiler/driver infrastructure
        |
        v
     execution
```

In OpenCL terminology, "host" can mean the CPU-side application. In
cross-compilation terminology, "host/build machine" may mean the x86-64
build PC. Be explicit about which meaning is intended.

## 13. Correct descriptions

**LLVM/Clang:** compiler infrastructure. Host LLVM/Clang supplies
build-time tools; target LLVM libraries are needed by target-side Mesa
components such as llvmpipe.

**SPIRV-Tools:** tools/libraries for manipulating SPIR-V: - `spirv-as`:
SPIR-V assembly -\> SPIR-V binary - `spirv-dis`: SPIR-V binary -\>
SPIR-V assembly - `spirv-val`: validation - `spirv-opt`: optimization -
`spirv-link`: linking

**LLVM-SPIRV-Translator:** translates between LLVM IR/bitcode and SPIR-V
where supported.

**libclc:** compiler/kernel-side library providing OpenCL/compiler
built-ins. `libclc.spv` is a SPIR-V library/module, not a normal AArch64
ELF library linked into the CPU-side OpenCL application.

**Rusticl:** Mesa's OpenCL implementation/runtime. It is not a SPIR-V
simulator/emulator.

## 14. Confirmed progress

``` text
[OK] x86-64 host LLVM 24 built
[OK] host LLVM reports X86 AArch64 SPIRV
[OK] host Clang accepts spirv64 OpenCL target
[OK] AArch64 cross toolchain exists
[OK] target/platform sysroot is kept separate from GCC sysroot
[OK] LLVM host/native-tool problem understood
[OK] host llvm-min-tblgen problem worked around/resolved
[OK] nested libclc CMAKE_CLC_COMPILER problem understood
[OK] CMAKE_CLC_COMPILER successfully changed to host Clang
[OK] nested spirv64 libclc build generated libclc.spv
```

Not complete:

``` text
[NOT COMPLETE] overall libclc build/install
[NOT COMPLETE] Rusticl
[NOT COMPLETE] final Mesa build
[NOT COMPLETE] final BeaglePlay runtime test
```

## 15. Items to re-evaluate

Before continuing the old build blindly, determine the exact HOST/TARGET
role of:

``` text
SPIRV-Tools command-line tools
SPIRV-Tools libraries
llvm-spirv
LLVMSPIRVLib
libclc installation/discovery
Mesa's libclc discovery
Mesa's LLVMSPIRVLib discovery
Rusticl dependencies
```

Use the actual checked-out Mesa 26.3.0-devel and LLVM source as the
authority.

## 16. Recommended restart matrix

  Component                   x86-64 HOST   AArch64 TARGET   SPIR-V artifact
  ------------------------- ------------- ---------------- -----------------
  LLVM build tools                    Yes                  
  Clang used during build             Yes                  
  LLVM libraries for Mesa                              Yes 
  SPIRV-Tools CLI                     Yes                  
  SPIRV-Tools libraries       Investigate      Investigate 
  `llvm-spirv`                     Likely      Investigate 
  `LLVMSPIRVLib`              Investigate           Likely 
  libclc build tools                  Yes                  
  `libclc.spv`                                                           Yes
  Mesa                                                 Yes 
  llvmpipe                                             Yes 
  lavapipe                                             Yes 
  Rusticl                                              Yes 

The `Investigate` entries are deliberate and should be resolved from the
actual source/build requirements.

## 17. Suggested clean dependency model

``` text
                     x86-64 BUILD PC
                           |
              +------------+------------+
              |                         |
         Host LLVM               Host SPIR-V tools
         Host Clang                     |
              |                         |
              +------------+------------+
                           |
                    cross compilation
                           |
              +------------+-------------+
              |                          |
         Target LLVM             target dependencies
              |                          |
              +------------+-------------+
                           |
                     libclc/SPIR-V
                           |
                           v
                          Mesa
               +-----------+-----------+
               |           |           |
               v           v           v
           llvmpipe     lavapipe     Rusticl
               |           |           |
               +-----------+-----------+
                           |
                           v
                   AArch64 BeaglePlay
```

Adjust exact dependency edges after inspecting Mesa 26.3's build
definitions.

## 18. Suggested build-script organization

Revise `builder/mesa3d_eval.sh` so stages explicitly indicate execution
architecture:

``` text
build_llvm_host
build_llvm_target

build_spirv_tools_host
build_spirv_tools_target       # only if required

build_spirv_llvm_host
build_spirv_llvm_target        # only if required

build_libclc_spirv

build_mesa_target
```

For every stage document: - compiler used - where resulting executables
run - where resulting libraries run - sysroot used - pkg-config
environment - install/staging prefix - dependencies

This should prevent target executables from accidentally being used as
host build tools.

## 19. Recommended first task in the next chat

Do not immediately continue rebuilding libclc.

First answer precisely from the checked-out source:

> For Mesa 26.3.0-devel with llvmpipe + lavapipe + Rusticl, which
> components of LLVM, SPIRV-Tools, LLVM-SPIRV-Translator, and libclc are
> required as HOST tools, which are required as AArch64 TARGET
> libraries, and which are SPIR-V artifacts?

Turn that answer into a dependency matrix, then revise
`builder/mesa3d_eval.sh`.

Only then begin a clean dependency-by-dependency rebuild.

## 20. Final goal

``` text
                    BeaglePlay / AArch64
                           |
                          Mesa
              +------------+------------+
              |            |            |
              v            v            v
          OpenGL         Vulkan       OpenCL
              |            |            |
              v            v            v
          llvmpipe      lavapipe      Rusticl
```

The immediate objective of the next session should be a correct and
reproducible **HOST / TARGET / SPIR-V** dependency architecture before
continuing compilation.
