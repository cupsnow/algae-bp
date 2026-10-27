# Nordic MCUboot and BLE OTA on Arduino Nano 33 BLE Sense Rev2

The recommended modern Nordic firmware-update architecture is:

```text
PC + Adafruit FT232H --SWD--> MCUboot + first Zephyr application
                                          |
Phone + nRF Connect Device Manager --BLE/SMP--> later signed applications
```

Installing this stack replaces the factory Arduino bootloader. Firmware will
normally be developed with Zephyr or the Nordic nRF Connect SDK rather than
uploaded as an ordinary Arduino sketch.

## Bootloader and OTA stack

Use:

- Nordic nRF Connect SDK or upstream Zephyr
- MCUboot as the signed-image bootloader
- MCUmgr SMP over Bluetooth LE in the application
- Nordic **nRF Connect Device Manager** on the phone

MCUboot does not normally provide the BLE connection itself. The running
application exposes the BLE SMP service, receives a new signed image into the
secondary slot, and asks MCUboot to verify and install it after reboot.

References:

- [Zephyr OTA documentation](https://docs.zephyrproject.org/latest/services/device_mgmt/ota.html)
- [Nordic nRF Connect Device Manager](https://docs.nordicsemi.com/r/bundle/mobile-dev-libs/page/index.html/nrf-connect-device-manager?contentId=C_GATG9JIza5UH4ugQMKtg)

The older alternative is the nRF5 SDK Secure DFU bootloader and Nordic's
legacy Device Firmware Update application. It uses a different DFU ZIP
protocol and must not be mixed with MCUboot/SMP images.

## Nano debug-pad pinout

The Nano exposes a 3-by-2, 0.1-inch test-pad footprint on its underside:

| Pad | Function          |
| --- | ----------------- |
| 1   | 3.3 V reference   |
| 2   | SWDIO             |
| 3   | SWCLK             |
| 4   | Missing/key       |
| 5   | GND               |
| 6   | RESET, active low |

References:

- [Arduino Nano 33 BLE Sense Rev2 datasheet](https://files.seeedstudio.com/wiki/Arduino_Nano_33_BLE_Sense_Rev2/datasheet.pdf)
- [Arduino Nano 33 BLE Rev2 schematic](https://docs.arduino.cc/resources/schematics/ABX00071-schematics.pdf)

## Adafruit FT232H SWD wiring

```text
FT232H D0 ---------------- Nano SWCLK
FT232H D1 ---------------- Nano SWDIO
FT232H D2 ---- 220-470 ohm +
                           +-- Nano SWDIO
FT232H D4 ---------------- Nano RESET       optional
FT232H GND --------------- Nano GND
```

SWDIO is bidirectional, so the FT232H input and output signals are joined as
shown. OpenOCD includes an Adafruit-specific configuration named
`interface/ftdi/ft232h-module-swd.cfg`, with D0 assigned to SWCLK, D1/D2 to
SWDIO, and D4 optionally assigned to reset.

Reference: [OpenOCD FT232H SWD configuration](https://github.com/openocd-org/openocd/blob/master/tcl/interface/ftdi/ft232h-module-swd.cfg)

Electrical precautions:

- Initially power the Nano through its USB connector.
- Always connect the FT232H and Nano grounds.
- Do not connect the FT232H 5 V output to the Nano 3.3 V rail.
- Keep the SWD wiring short.
- Start with a 100-250 kHz SWD clock and increase it only after communication
  is reliable.
- Pad 1 is primarily a target-voltage reference. It need not be connected in
  this simple arrangement when both sides already use 3.3 V signaling.

A CMSIS-DAP adapter, J-Link, or nRF52840 DK used as a debugger is simpler and
safer, but a recent OpenOCD supports the FT232H.

## Build an initial BLE DFU image

Install the nRF Connect SDK or a current upstream Zephyr workspace. The board
target for the Rev2 hardware is:

```text
arduino_nano_33_ble@rev2
```

Zephyr documents this board revision and its SWD interface in the
[Nano 33 BLE board documentation](https://docs.zephyrproject.org/latest/boards/arduino/nano_33_ble/doc/index.html).

This repository keeps a copy of Zephyr's SMP server sample under
`package/nano33ble_smp_svr`. The project wrapper activates `.venv`, loads the
existing `~/zephyrproject` environment, selects Zephyr SDK 1.0.1, and enables
MCUboot through sysbuild.

Configure and build both images with:

```sh
make nano33smp_defconfig
```

For subsequent incremental builds, use:

```sh
make nano33smp
```

The equivalent direct `west` invocation is:

```sh
west build \
    -b arduino_nano_33_ble@rev2 \
    -d build/nano33ble_smp_svr-qemuarm64 \
    --sysbuild \
    package/nano33ble_smp_svr \
    -- \
    -DEXTRA_CONF_FILE=bt.conf \
    -DSB_CONFIG_BOOTLOADER_MCUBOOT=y
```

The precise command or option defaults can vary with the selected Zephyr or
nRF Connect SDK release. The
[SMP server sample documentation](https://docs.zephyrproject.org/latest/samples/subsys/mgmt/mcumgr/smp_svr/README.html)
is the authoritative reference for that release.

`make nano33smp_defconfig` was verified with Zephyr 4.5.0-rc1 and Zephyr SDK
1.0.1. The resolved application configuration includes:

```text
CONFIG_BT=y
CONFIG_BT_PERIPHERAL=y
CONFIG_BOOTLOADER_MCUBOOT=y
CONFIG_MCUMGR_GRP_IMG=y
CONFIG_MCUMGR_TRANSPORT_BT=y
```

### Flash layout

The stock Nano board description does not provide MCUboot `slot0_partition`
and `slot1_partition` nodes. This project supplies matching overlays for the
application and bootloader:

- `package/nano33ble_smp_svr/boards/arduino_nano_33_ble.overlay`
- `package/nano33ble_smp_svr/sysbuild/mcuboot.overlay`

Both images resolve to this internal-flash layout:

| Address range | Size | Purpose |
| --- | ---: | --- |
| `0x00000-0x0bfff` | 48 KiB | MCUboot |
| `0x0c000-0x81fff` | 472 KiB | Primary application, `image-0` |
| `0x82000-0xf7fff` | 472 KiB | OTA candidate, `image-1` |
| `0xf8000-0xfffff` | 32 KiB | Settings/filesystem storage |

The two overlay files must remain synchronized. The equal-sized image slots
are required for the configured A/B update strategy.

### Generated artifacts

The verified build produces separate bootloader and application images; it
does not produce a top-level `merged.hex`:

```text
build/nano33ble_smp_svr-qemuarm64/mcuboot/zephyr/zephyr.hex
build/nano33ble_smp_svr-qemuarm64/nano33ble_smp_svr/zephyr/zephyr.signed.hex
build/nano33ble_smp_svr-qemuarm64/nano33ble_smp_svr/zephyr/zephyr.signed.bin
```

Use the two HEX files for the initial SWD installation. Use
`zephyr.signed.bin` for subsequent BLE OTA updates.

## Test the SWD connection

First verify that OpenOCD can detect the target:

```sh
openocd \
    -f interface/ftdi/ft232h-module-swd.cfg \
    -f target/nrf52.cfg \
    -c 'adapter speed 250; init; reset halt; targets; exit'
```

Before erasing the factory installation, save the complete 1 MiB internal
flash:

```sh
openocd \
    -f interface/ftdi/ft232h-module-swd.cfg \
    -f target/nrf52.cfg \
    -c 'adapter speed 250; init; reset halt; dump_image nano33-full-backup.bin 0x00000000 0x00100000; exit'
```

Keep this backup together with an official Arduino bootloader image if Arduino
operation may need to be restored later.

## Program MCUboot and the initial application

Erase the device, then program MCUboot and the signed initial application in
one OpenOCD session:

```sh
openocd \
    -f interface/ftdi/ft232h-module-swd.cfg \
    -f target/nrf52.cfg \
    -c 'adapter speed 250; init; reset halt; nrf5 mass_erase; program build/nano33ble_smp_svr-qemuarm64/mcuboot/zephyr/zephyr.hex verify; program build/nano33ble_smp_svr-qemuarm64/nano33ble_smp_svr/zephyr/zephyr.signed.hex verify reset exit'
```

A mass erase removes the Arduino bootloader, application, settings, bonding
information, and relevant writable configuration. This is the one-time SWD
provisioning step; subsequent application updates can use BLE.

## Update over BLE

After the initial SWD installation:

1. Boot the Nano and open **nRF Connect Device Manager** on the phone.
2. Connect to the device advertising the MCUmgr SMP service.
3. Select the new build's `zephyr.signed.bin`.
4. Upload the image.
5. Mark it for test or confirmation if requested.
6. Reset the device to boot the new image.
7. Confirm the image after a successful boot.

The application must confirm a test image, or MCUboot may roll it back on a
later reset.

## Production requirements and limitations

- The current build warns that the Bluetooth MCUmgr transport is
  unauthenticated. Add application-level access control and an appropriate BLE
  security policy before exposing DFU on a production device.
- The copied sample enables filesystem management without file-access hooks.
  Disable that command group if it is unnecessary, or add authorization hooks
  before production use.
- Replace MCUboot's development signing key with a protected project-specific
  private key. Build MCUboot with the matching public key and sign every OTA
  image with the private key.
- Keep the private signing key outside the firmware source tree and build
  artifacts.
- Review downgrade prevention, image confirmation, rollback behavior, and
  recovery behavior before deployment.
- A/B MCUboot updates divide the nRF52840's 1 MiB internal flash between
  MCUboot, settings/storage, and two application slots. The maximum application
  is therefore substantially smaller than a conventional single-slot Arduino
  image.
- Inspect the generated `partitions.yml` and linker/devicetree output before
  committing to an application size or persistent-storage layout.
