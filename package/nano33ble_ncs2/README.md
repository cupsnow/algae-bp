# Nano 33 BLE NCS: nRF Connect SDK migration example

This is a separate copy of `../nano33ble_test2` for the Nano 33 BLE Sense Rev2
(nRF52840). It retains the LED blink, statistics, `nano info` / `nano echo`
shell commands, BLE SMP server, LittleFS and SMP over the physical UART shell.
The original application and its Makefile targets are unchanged.

**Status:** NCS v3.4.2 pristine configuration and full MCUboot/application build
passed with the Sense Rev2 overlay. UART output and sensor operation still
require hardware validation. The application uses an overlay on the bundled
Arduino board rather than a new Nordic-supported board target.

## 1. Install on Linux

nRF Connect SDK (NCS) contains Nordic's Zephyr fork, Nordic libraries, MCUboot
and a west manifest pinning their revisions. Use its matching toolchain and
workspace, separately from this repository's existing upstream Zephyr setup.
Do not source the existing Zephyr environment in the NCS terminal.

Download the Linux executable for your host architecture from
[Nordic nRF Util](https://www.nordicsemi.com/Products/Development-tools/nRF-Util).
Follow the host prerequisites in
[Nordic's installation guide](https://nrfconnectdocs.nordicsemi.com/ncs/latest/nrf/installation/install_ncs.html).
For a file downloaded as `~/Downloads/nrfutil`:

```sh
mkdir -p "$HOME/.local/bin"
install -m 755 "$HOME/Downloads/nrfutil" "$HOME/.local/bin/nrfutil"
export PATH="$HOME/.local/bin:$PATH"
nrfutil --version
nrfutil install sdk-manager
nrfutil sdk-manager search
```

Choose a released SDK version from the search output, not a preview. Use the
same exact version everywhere below. Replace `vX.Y.Z` before running:

```sh
NCS_VERSION=vX.Y.Z
nrfutil sdk-manager install "$NCS_VERSION"
nrfutil sdk-manager list
nrfutil sdk-manager toolchain launch --ncs-version "$NCS_VERSION" --shell
```

The install command installs both SDK sources and matching toolchain. Linux's
default SDK location is `~/ncs/`; inspect `list` for the actual version directory.
The launched shell supplies the compiler, Python, CMake, Ninja and west.
SDK-manager toolchain installation is not supported on Linux ARM64 hosts;
consult Nordic's supported host requirements if using one.

In the **new toolchain shell**, set the workspace path from `list`:

```sh
export NCS_DIR="$HOME/ncs/vX.Y.Z"
cd "$NCS_DIR"
west topdir
west list zephyr
west boards | rg arduino_nano_33_ble
```

`west topdir` must identify the selected NCS workspace. If this release does
not contain the Arduino board, stop and port/import its board definition for
that release; selecting `nrf52840dk` is not a substitute for Nano pin mapping.
Do not run `west init` over the workspace already installed by sdk-manager.

For VS Code, install Nordic's **nRF Connect for VS Code Extension Pack**, open
this application folder, select the same installed SDK and toolchain, and add
a sysbuild configuration for `arduino_nano_33_ble`. The CLI below is useful
for learning and diagnosing the actual build independently of editor settings.

## 2. Configure and build the copy

In that same NCS toolchain shell:

```sh
cd /home/joelai/02_dev/algae-ws/algae-bp
sh package/nano33ble_ncs2/build.sh configure
sh package/nano33ble_ncs2/build.sh build
```

`configure` creates a pristine configuration without compiling. `build` compiles
MCUboot and the application. Outputs default to `../build/nano33ncs2`.
Override with an absolute `NCS_BUILD_DIR` when needed. Use a separate directory
for every SDK version; do not reuse `../build/nano33test2`.

Equivalent direct command, run from inside `$NCS_DIR`:

```sh
west build --sysbuild -p always -b arduino_nano_33_ble \
  -d /tmp/nano33ncs2 \
  /home/joelai/02_dev/algae-ws/algae-bp/package/nano33ble_ncs2
```

The copy's CMake file defaults to `bt.conf;uart.conf`. `sysbuild.conf` enables
MCUboot; `sysbuild/mcuboot.conf` enables its physical UART console. The CMake
minimum is lowered from the original 3.28 to 3.20, the unused legacy USB header
is removed, and `<stdarg.h>` is included explicitly. No Nordic-only application
API is required for this first migration.

Treat Kconfig warnings as migration issues: inspect the selected release's
symbol definitions instead of suppressing warnings. If necessary, explicitly
choose that release's Zephyr Bluetooth controller instead of Nordic's default
controller, or adjust its controller options after inspecting the error.

## 3. Inspect before flashing

After a successful build, inspect:

```sh
BUILD=/home/joelai/02_dev/algae-ws/build/nano33ncs2
cat "$BUILD/domains.yaml"
rg 'CONFIG_(BOOTLOADER_MCUBOOT|MCUMGR|SHELL|BT|PARTITION_MANAGER)' \
  "$BUILD/nano33ble_ncs2/zephyr/.config"
rg -n 'chosen|console|uart0|partition|slot|storage' \
  "$BUILD/nano33ble_ncs2/zephyr/zephyr.dts"
find "$BUILD" -type f \( -name '*signed*' -o -name '*merged*.hex' \
  -o -name 'partitions.yml' -o -name 'pm.config' \)
```

Check that console UART0 is enabled at 115200 and that the generated layout
contains a bootloader, primary/secondary image slots and LittleFS storage.
Depending on the NCS release, Partition Manager may control placement rather
than the board's fixed DTS partitions. Inspect its generated `partitions.yml`
and the bootloader layout too; both images must agree. Do not assume the old
upstream build's flash addresses or signing configuration remain compatible.

Provision the new bootloader and application together through SWD after
reviewing that layout. See [the repository MCUboot/SWD guide](../../docs/nano33ble/mcuboot.md)
for the FT232H wiring and OpenOCD setup. Its mass erase deletes the Arduino
bootloader, installed firmware and stored data. Adapt the program paths to
the new build's actual artifacts; the old `nano33test2_flash` target still
programs the original application's outputs. No flashing is performed by
this example's build script. Do not use the board's default bossac runner to
provision this MCUboot layout.

Do not upload this first migration through the old application's OTA service
until slot layout, signature key, image header and swap algorithm compatibility
are verified. This example inherits development signing and unauthenticated SMP.

## 4. UART and firmware updates

Use a **3.3 V** USB-UART adapter: adapter RX to D1/TX (P1.03), adapter TX to
D0/RX (P1.10), common GND, 115200 8N1 with no flow control. The Nano's USB
connector is not this physical UART.

```sh
picocom -b 115200 /dev/ttyUSB0
# At the nano33> prompt:
# nano info
# nano echo hello NCS
```

After initial provisioning and boot verification, close picocom and use your
existing Go `mcumgr` CLI to update with a different build. If generated, the
usual Zephyr update artifact is
`$BUILD/nano33ble_ncs2/zephyr/zephyr.signed.bin`; confirm the signing output
for the selected NCS release before using it.

```sh
mcumgr --conntype serial --connstring 'dev=/dev/ttyUSB0,baud=115200' image list
mcumgr --conntype serial --connstring 'dev=/dev/ttyUSB0,baud=115200' image upload "$BUILD/nano33ble_ncs2/zephyr/zephyr.signed.bin"
mcumgr --conntype serial --connstring 'dev=/dev/ttyUSB0,baud=115200' image list
# Replace HASH with the uploaded secondary-slot hash:
mcumgr --conntype serial --connstring 'dev=/dev/ttyUSB0,baud=115200' image test HASH
mcumgr --conntype serial --connstring 'dev=/dev/ttyUSB0,baud=115200' reset
# Wait for successful boot and verify the new version, then:
mcumgr --conntype serial --connstring 'dev=/dev/ttyUSB0,baud=115200' image confirm
```

BLE advertises as `Algae-Nano33` and exposes the SMP service. UART SMP depends
on a working application; the bootloader configuration here supplies logging,
not serial recovery. An unconfirmed test image can revert after another reset.

## NCS v3.4.2 configuration check

The bundled Arduino board has no `@rev2` selector; use
`arduino_nano_33_ble` for this LED/UART/BLE example. Sense Rev2 sensor
support is not established by this selection. The application overlay in
`boards/arduino_nano_33_ble.overlay` matches the existing MCUboot overlay:
bootloader 0x00000–0x0bfff, slot 0 0x0c000–0x81fff, slot 1
0x82000–0xf7fff, storage 0xf8000–0xfffff. Both images need that layout.

## Sense Rev2 hardware and UART check

`boards/sense-rev2.dtsi` ports the upstream Rev2 BMI270/BMM150 definitions,
400 kHz internal sensor bus and P0.22 high-drive sensor supply enable. It also
adds the Sense HS3003, LPS22HB, APDS9960 and PDM microphone wiring from the
upstream Sense definitions. Sensor nodes describe wiring; this application
still does not enable or exercise their sensor drivers. The MCUboot overlay
keeps the same flash map and explicitly selects physical UART0.

Both images select UART0 at 115200 baud without hardware flow control,
TX D1/P1.03 and RX D0/P1.10. The application now enables its startup banner.
The pre-change generated settings already selected this UART, so these changes
do not establish the cause of a silent board.

After rebuilding, flash both images using your established SWD workflow.
The `nano33ncs2_flash` target mass-erases firmware and storage. Open a 3.3 V
USB-UART adapter at 115200 8N1 (flow control off), then press reset. Adapter RX
must connect to D1/TX, adapter TX to D0/RX, and GND to GND. Do not expect these
messages on the Nano USB connector. Expect MCUboot messages, the application
banner and `nano33>`; press Enter and try `nano info`. If completely silent,
check whether the LED blinks, test the adapter by loopback, and inspect D1 with
a logic analyzer during reset. No serial device was available in the build
session, so live capture and target boot diagnosis remain outstanding.
