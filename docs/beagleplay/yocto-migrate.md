# Migrating algae-bp to Yocto

This document is the living record for the `algae-bp` Yocto migration. Relevant
decisions, commands, findings, and validation results from the associated chat
should be synchronized here as the work progresses.

## Status

- The Yocto project was moved from `algae-bp/yocto` to the sibling
  `/home/joelai/02_dev/algae-ws/algae-yocto` directory.
- The initial `yocto.mk`, `meta-algae` layer, build configuration template, and
  BusyBox-centered image recipe have been created.
- The pinned upstream layers have been downloaded, and the relocated
  `bootstrap` target successfully reports all configured layers.
- The Yocto image has not been built yet.
- The existing Makefile-based build remains the reference implementation.
- No Yocto-built image has been boot-tested on BeaglePlay or QEMU yet.

## Migration objective

Convert the current custom distribution builder into a reproducible Yocto
project while preserving the existing BeaglePlay boot flow, kernel and device
tree behavior, runtime package set, firmware, system services, applications,
and image layout.

The migration should not wrap the complete top-level Makefile in one BitBake
recipe. The current Makefile owns several responsibilities that Yocto separates:

- cross-toolchain and sysroot management;
- U-Boot and Linux builds;
- target package builds and dependency ordering;
- root filesystem assembly;
- boot partition and disk image generation;
- product application builds.

These responsibilities should become machine and distro configuration, normal
recipes, package groups, an image recipe, and a Wic disk layout.

## Proposed source layout

Use the existing `algae-bp` repository as the project root. Keep Yocto metadata,
upstream checkouts, and generated build output under a dedicated `yocto/`
directory so they do not collide with the current Makefile build.

```text
algae-bp/
|-- Makefile                    # existing build remains usable
|-- builder/                    # existing build helpers
|-- package/                    # existing local sources
|-- docs/
`-- yocto/
    |-- kas/                    # optional reproducible checkout/build config
    |   `-- algae-bp.yml
    |-- meta-algae/             # tracked product layer
    |   |-- conf/
    |   |   |-- layer.conf
    |   |   |-- machine/algae-bp.conf
    |   |   `-- distro/algae.conf
    |   |-- recipes-bsp/
    |   |-- recipes-kernel/
    |   |-- recipes-core/
    |   |   |-- images/algae-image.bb
    |   |   `-- packagegroups/
    |   |-- recipes-connectivity/
    |   |-- recipes-apps/
    |   `-- wic/algae-bp.wks.in
    |-- sources/                # ignored upstream checkouts
    |   |-- poky/
    |   |-- meta-arm/
    |   `-- meta-ti/
    `-- build/                  # ignored BitBake output
```

All upstream layers must use mutually compatible, pinned release branches and
revisions. The first-stage implementation selects the `scarthgap` LTS branch
and locks Poky, meta-arm, and meta-ti to commits recorded in `yocto.mk`. Only
`meta-algae`, setup configuration, and project documentation should be
committed here; upstream checkouts and generated `yocto/build` content are
ignored.

Downloaded Yocto source archives are shared at:

```text
/home/joelai/02_dev2/iq9/qli191/downloads
```

This is BitBake's `DL_DIR`; Git checkouts of Poky and the metadata layers remain
under `yocto/sources`.

## Mapping from the current build

| Current mechanism | Yocto mechanism |
| --- | --- |
| `APP_PLATFORM=bp` | `MACHINE = "algae-bp"` |
| `APP_ATTR_bp` | machine/distro features and package groups |
| bundled cross-toolchain variables | Yocto-provided cross-toolchain |
| `BUILD_SYSROOT` and `BUILD_SYSROOT2` | per-recipe `recipe-sysroot` populated from `DEPENDS` |
| `<package>_DEP` | recipe `DEPENDS` |
| `<package>_install` | recipe `do_install()` |
| `<package>_destpkg` | normal BitBake package generation |
| `dist_rootfs_phase*` | image recipe and package groups |
| `dist-bp_phase*` | image deployment, boot files, and Wic |
| `linux-*.config` | kernel `defconfig` and/or configuration fragments |
| `uboot-*.defconfig` | U-Boot recipe append and configuration |
| `prebuilt/bp/...` | files owned and installed by individual recipes |
| `genimage` logic | `.wks.in` Wic description |
| `builder/run_qemu.sh` | `runqemu` configuration or a thin project wrapper |

Do not reproduce the shared dual-sysroot design in Yocto. Each recipe should
declare its build dependencies and consume only the sysroot BitBake constructs
for it. In particular, do not globally combine pkg-config metadata from roots
that require different rebasing rules.

## Migration strategy

### 1. Capture the current baseline

Create a known-good `APP_PLATFORM=bp` image and record:

- source revisions and build configuration;
- kernel release, final `.config`, DTBs, and overlays;
- U-Boot configurations, environment, and boot scripts;
- boot partition contents;
- root filesystem file and package manifests;
- enabled systemd units;
- firmware files;
- partition offsets, types, sizes, and filesystems;
- boot logs and hardware test results.

Candidate baseline commands:

```sh
cd /home/joelai/02_dev/algae-ws/algae-bp
make APP_PLATFORM=bp dist-bp
find destdir/bp -type f -printf '%P\n' | sort > /tmp/algae-bp-files.txt
```

These commands are proposed and have not yet been run for this migration.

### 2. Establish the upstream Yocto build

Use pinned, compatible revisions of:

- Poky/OpenEmbedded Core;
- `meta-openembedded`;
- the appropriate TI BSP layer for AM62x/BeaglePlay;
- `meta-arm` when required by the selected TI BSP;
- the new `meta-algae` product layer.

First build and boot an unmodified upstream BeaglePlay console image. Prefer
extending the upstream BeaglePlay machine over duplicating the AM62x BSP.

Useful discovery commands after initializing the build environment:

```sh
bitbake-layers show-machines
bitbake-layers show-recipes virtual/kernel
bitbake-layers show-recipes virtual/bootloader
```

### 3. Create `meta-algae`

```sh
bitbake-layers create-layer yocto/meta-algae
bitbake-layers add-layer yocto/meta-algae
```

Run these commands from the `algae-bp` project root after initializing the
Poky environment. The eventual environment wrapper should set the build
directory explicitly to `yocto/build`.

The layer should contain product-specific metadata only. Existing upstream
recipes should be used for standard components such as systemd, BlueZ, Python,
GLib, OpenSSL, BusyBox, Mesa, and common command-line utilities. Customize them
with narrowly scoped `.bbappend` files only when necessary.

The initial layer and build wrapper are now present. Bootstrap and build with:

```sh
make -f yocto.mk bootstrap
make -f yocto.mk all
```

`bootstrap` clones the `scarthgap` branches of Poky, meta-arm, and meta-ti,
checks out their locked revisions, creates `yocto/build` from the Algae
configuration template, and displays the active layers. `all` builds
`algae-busybox-image`.

`meta-openembedded` is intentionally not downloaded in the first stage because
the minimal image does not need it. Add it when a migrated package introduces
a concrete dependency on one of its layers.

The first stage uses TI's `beagleplay-ti` machine from `meta-ti-bsp`. In the
current `scarthgap` branch, the plain `beagleplay` machine is supplied by the
additional `meta-beagle` layer and selects BeagleBoard.org BSP policy. That
extra layer is not needed for the initial TI-BSP boot test.

### First-stage minimal image

The first-stage image is `algae-busybox-image`. A bootable root filesystem
cannot contain literally only the BusyBox package: it also needs base files,
libc, initialization configuration, and device setup. The image therefore uses
`packagegroup-core-boot` with BusyBox selected for init, login, and mdev.

Size controls applied at this stage are:

- no extra image features;
- no recommended packages;
- no image locales;
- no package manager installed in the target image;
- `rm_work` enabled to reclaim completed recipe work directories;
- Poky's default `create-spdx` inheritance disabled for this storage-limited
  first stage; restore it when release SBOM artifacts are required;
- shared downloads stored outside the project workspace.

### 4. Define the machine and distro

Start `conf/machine/algae-bp.conf` by requiring the selected upstream
BeaglePlay machine configuration, then add only product differences such as:

- machine overrides;
- DTB and overlay selection;
- bootloader variants;
- firmware requirements;
- serial console and image-format settings.

Put product-wide policy in `conf/distro/algae.conf`, not in the machine file.
Examples include the init system, package format, security policy, and release
defaults.

### 5. Migrate Linux and device tree changes

Candidate current inputs include:

- `linux-v7.1.3-bp.config`;
- `linux-v6-bp.config`;
- `linux-bp-bb.config`;
- `k3-am625-beagleplay.dts`;
- `linux-bb-bp.dts`.

Compare these against the selected upstream TI kernel. Prefer small kernel
configuration fragments and DTS patches or overlays over copied whole files.
The final generated kernel configuration and DTBs must be compared with the
baseline artifacts.

### 6. Migrate U-Boot and image construction

Move product-specific changes from the following inputs into an appropriate
U-Boot `.bbappend` and recipe-owned files:

- `uboot-bp-a53.defconfig`;
- `uboot-bp-a53-emmc.defconfig`;
- U-Boot environment files;
- boot scripts and FIT image configuration.

Replace `dist-bp_bootpart`, manual filesystem creation, and `genimage` assembly
with a Wic layout. Preserve separate SD/eMMC behavior where the current product
requires it.

### 7. Replace distribution phases with package groups

Define package groups for independently understandable runtime roles:

- base operating system;
- networking and connectivity;
- multimedia and graphics;
- diagnostics and development tools;
- algae product applications.

The image recipe should select packages through package groups and
`IMAGE_INSTALL`. Avoid copying package payloads directly into the rootfs.

### 8. Add product application recipes

Create one recipe for each independently versioned local application, including
applications such as `admin2` and `tester1`.

Application build systems must honor the BitBake-provided `CC`, `CXX`, `AR`,
`CFLAGS`, `CXXFLAGS`, `LDFLAGS`, `PKG_CONFIG`, and `DESTDIR`. They must not
select the algae-bp bundled compiler or manually consume `BUILD_SYSROOT`.

Use pinned source revisions for reproducible builds. `devtool` or
`externalsrc` may be used during active development, but should not replace
pinned release sources.

## Proposed milestones

1. Capture and archive the current Makefile-built BeaglePlay baseline.
2. Build and boot an upstream Yocto BeaglePlay console image.
3. Create `meta-algae`, `algae-bp.conf`, and `algae-image.bb`.
4. Migrate kernel configuration and device tree differences.
5. Reproduce the U-Boot and boot partition behavior.
6. Reproduce the SD/eMMC partition layout with Wic.
7. Add package groups until the runtime manifest matches the baseline.
8. Port local applications one at a time.
9. Add and test Wi-Fi, Bluetooth, graphics, and required firmware.
10. Run boot, service, application, and hardware regression tests.
11. Retire corresponding Makefile sections only after their Yocto replacements
    pass the agreed validation.

## First implementation slice

The recommended first slice is deliberately small:

1. select and pin the Yocto and TI BSP release;
2. build the upstream BeaglePlay image;
3. create `meta-algae`;
4. create an initial `algae-bp` machine derived from upstream;
5. create a minimal `algae-image`;
6. boot it on BeaglePlay;
7. port one small local application as an end-to-end recipe example.

This proves fetching, cross-compilation, packaging, image creation, deployment,
and boot before migrating the full package set.

## Verification rules

- Keep proposals, successful builds, emulator tests, and real-hardware tests
  explicitly separate.
- Inspect generated configuration and deployed artifacts, not only task exit
  status.
- Use a clean build when changing the machine, compiler, architecture, kernel
  provider, or other configuration that can leave stale state.
- Preserve the existing Makefile path until the corresponding Yocto path is
  verified.
- Record exact commands and relevant artifact paths for every completed
  milestone.

## Decision log

### 2026-11-02

- Use `docs/beagleplay/yocto-migrate.md` as the living synchronization document
  for the migration discussion.
- Migrate by responsibility into Yocto metadata; do not wrap the complete
  Makefile in a single recipe.
- Prefer an upstream TI BeaglePlay/AM62x BSP machine as the starting point.
- Do not reproduce the current shared dual-sysroot design.
- Keep the current build operational during incremental migration.
- Use the existing `algae-bp` repository as the project root. Store the tracked
  product layer at `yocto/meta-algae`, upstream checkouts at `yocto/sources`,
  and generated BitBake state at `yocto/build`.
- Add `yocto.mk` with `bootstrap` and `all` entry points matching the existing
  Makefile-oriented project workflow.
- Use Yocto `scarthgap` as the initial LTS branch, with Poky, meta-arm, and
  meta-ti locked to the verified commits in `yocto.mk`.
- Use the `beagleplay-ti` machine from `meta-ti-bsp` for the first-stage image;
  do not add the optional `meta-beagle` layer until its BSP policy is wanted.
- Store BitBake downloads in
  `/home/joelai/02_dev2/iq9/qli191/downloads` to conserve workspace storage.
- Make `algae-busybox-image` a minimal BusyBox-centered bootable first stage.
- Move the complete Yocto project to the sibling `algae-yocto` directory. The
  relocated `yocto.mk` now treats that directory as `YOCTO_DIR`; the existing
  `algae-bp` Makefile build remains separate.

## Build log

### 2026-11-02: first bootstrap attempt

- Poky, meta-arm, and meta-ti were downloaded successfully and checked out at
  the revisions locked in `yocto.mk`.
- Build-directory initialization failed because `oe-init-build-env` was sourced
  while the shell remained in the project root. Poky's script consequently
  looked for `oe-init-build-env` in the project root.
- `yocto.mk` was corrected to enter the Poky checkout before sourcing
  `oe-init-build-env` in both `bootstrap` and `all`.
- The next attempt showed that Debian `dash` does not pass arguments to a
  sourced script, so Poky selected its default `build` directory inside the
  checkout. The wrapper now uses Poky's documented POSIX workaround: set the
  positional build-directory argument before sourcing `oe-init-build-env`.

### 2026-11-02: bootstrap success and first image build

- `make -f yocto.mk bootstrap` completed successfully. BitBake recognized the
  core, Poky, meta-arm, arm-toolchain, meta-ti-bsp, and meta-algae layers.
- The image parsed 2,056 recipes with no parse errors for `beagleplay-ti` and
  its required `beagleplay-ti-k3r5` firmware multiconfig.
- The build identified itself as Poky 5.0.21, targeting AArch64 with TI Linux
  6.12.57. Ubuntu 26.04 produced an unvalidated-host warning, not an error.
- The first build was stopped deliberately after confirming progress because
  Poky's default SPDX generation was producing avoidable tasks and artifacts.
  `create-spdx` is now disabled for the storage-constrained first stage; the
  build is to be resumed with completed non-SPDX work reused.

### 2026-11-02: relocation to `algae-yocto`

- `make -f yocto.mk print-config` resolves `sources`, `build`, and
  `meta-algae` beneath `/home/joelai/02_dev/algae-ws/algae-yocto` and retains
  the shared download directory at
  `/home/joelai/02_dev2/iq9/qli191/downloads`.
- Poky, meta-arm, and meta-ti still match the three revisions pinned by
  `yocto.mk`.
- `make -f yocto.mk bootstrap` completed successfully from `algae-yocto` and
  listed core, Poky, meta-arm, arm-toolchain, meta-ti-bsp, and meta-algae from
  their relocated paths.
- `build/conf/templateconf.cfg` still contains the former absolute template
  path. This does not prevent the existing build configuration from loading,
  but it should be updated or regenerated before treating the relocation as
  clean.
- The moved `build/tmp/saved_tmpdir`, `build/tmp-k3r5/saved_tmpdir`, and cached
  task scripts contain absolute paths beneath the former
  `algae-bp/yocto/build`. Those generated work directories must not be assumed
  resumable after the move. Preserve `build/sstate-cache` for reuse, then use
  fresh `tmp` and `tmp-k3r5` directories before continuing the image build.
- A deeper `bitbake -p algae-busybox-image` relocation check was requested but
  not run, so parsing and image construction from the new location remain
  unverified.


