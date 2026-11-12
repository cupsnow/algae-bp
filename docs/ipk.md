# Local IPK trial: Expat and D-Bus

The Makefile provides `expat_ipk`, `dbus_ipk` (also packages Expat), and `ipk`.
They reuse the existing platform-specific `*-destpkg.tar.xz` archives, building
missing archives through the existing targets. Rebuild the destpkg archives
explicitly when changing sources or configuration; existing archives are reused.

```sh
make APP_PLATFORM=qemuarm64 ipk
```

Output: `build/ipk-qemuarm64/`. Override `IPK_DIR`, `IPK_ARCH` (defaults to
`APP_PLATFORM`), or `IPK_RELEASE` as needed. The platform architecture deliberately
separates bp and qemuarm64 packages; do not mix packages built for different ABIs.
Version metadata comes from the archive's pkg-config file. Python 3.12+ and host
`ar` are required. Development headers, static libraries, and pkg-config files
remain in the destpkg archive and are excluded from these runtime IPKs.
D-Bus declares `Depends: expat`; libc and the dynamic loader belong to the base
image and are deliberately not represented as opkg packages in this first trial.
Configuration files under `/etc` and D-Bus's system/session XML files are marked
as conffiles. There is no feed or automatic service startup in this trial.

## Build and stage opkg

Sources: `$(PKGDIR2)/opkg` and `$(PKGDIR2)/libarchive`. The latter is required
by this opkg checkout. The local libarchive checkout is used by default; no
source download or `libarchive_DIR` override is needed.
Target zlib headers and `$(BUILD_SYSROOT)/lib/libz.so` must already exist.

```sh
make APP_PLATFORM=qemuarm64 opkg
make APP_PLATFORM=qemuarm64 opkg_install \
    DESTDIR="$PWD/build/opkg-root-qemuarm64"
```

The minimal build uses the internal solver, static libopkg/libarchive, gzip
archives, and the wget download backend. Curl, signature verification, and xz
support are disabled for this local trial. The upstream internal solver emits
a deprecation warning. This configuration is intended for locally supplied IPKs.
The resulting opkg still dynamically requires libc and `libz.so.1`.
`opkg_install` stages the executable, intercept helpers, architecture config,
and package database directory. It does not change `make dist`.
Copy this staging root's contents into the target root filesystem before boot,
or onto the target as root. Do not copy over the host's root filesystem.

## On the target

Use a fresh image where Expat/D-Bus are not already installed by another method;
this trial does not register ownership of files previously copied by `make dist`.
After copying both IPKs to `/tmp/packages`:

```sh
opkg install /tmp/packages/expat_*.ipk /tmp/packages/dbus_*.ipk
opkg list-installed
```

Before starting the system bus, ensure the `messagebus` user and group exist.
For a BusyBox target, if they do not already exist:

```sh
addgroup -S messagebus
adduser -S -D -H -h / -s /bin/false -G messagebus messagebus
mkdir -p /var/run/dbus /var/lib/dbus
dbus-uuidgen --ensure
dbus-daemon --system
```

Integrate the runtime-directory creation and daemon start into the target's init
flow for subsequent boots. The base image must supply the expected libc/loader;
`opkg` cannot validate that implicit ABI contract.

## Verification in this change

For the default `qemuarm64` platform, both IPKs were generated from existing
staging archives: Expat 2.9.0 and D-Bus 1.16.99-alpha. The latter's daemon and
libdbus shared library have no shared Expat dependency (apparently linked into
the daemon statically); its explicit package dependency is retained for rebuilds.
The IPK ar members and payload/control archives were inspected.
opkg was cross-built and staged using `$(PKGDIR2)/libarchive` with:

```sh
make opkg_install opkg_BUILDDIR="$PWD/build/opkg-qemuarm64" \
    libarchive_BUILDDIR="$PWD/build/libarchive-qemuarm64" \
    DESTDIR="$PWD/build/opkg-root-qemuarm64"
```

ELF inspection confirmed AArch64 and shared dependencies `libz.so.1` and
`libc.so.6`. No device installation, package-manager execution, or D-Bus startup
has been verified. The local-source build log is `/tmp/algae-opkg-local-source.log`.
