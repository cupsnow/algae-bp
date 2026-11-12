Develop Note
====

[nRF52840 Product Specification](https://docs.nordicsemi.com/bundle/ps_nrf52840/page/keyfeatures_html5.html)

OpenOCD with FT232H
----

[Programming with Adafruit FT232H BreakOut](adafruit-ft232h.md)

- The Nano33 BLE is powered by usb, **Not from SDW 3.3v**

Nano33 BLE SDW pins

```
| SWD   | Adafruit FT232H | Nano33 BLE | memo     |
| ----- | --------------- | ---------- | -------- |
| V3.3  | V3.3            | Pin1       | not used |
| GND   | GND             | Pin5       |          |
| SWCLK | D0              | Pin3       |          |
| SWDIO | D1              | Pin2       |          |
| RESET | D3              | Pin6       | not used |
```

Nano33 BLE SDW pins location (back view, usb connector toward up)

```
5 3 1
6   2
```

Check SWD connection

```sh
LD_LIBRARY_PATH=../build/sysroot-ub20/lib timeout -skill 60 ../build/sysroot-ub20/bin/openocd \
    -s ../build/sysroot-ub20/share/openocd/scripts \
    -f interface/ftdi/ft232h-module-swd.cfg \
    -f target/nordic/nrf52.cfg \
    -c 'adapter speed 1000; init; reset halt' \
    -c 'targets; reset run; shutdown'
```

Backup existing firmware (Not wrok currently)

```sh
LD_LIBRARY_PATH=../build/sysroot-ub20/lib timeout -skill 60 ../build/sysroot-ub20/bin/openocd \
    -s ../build/sysroot-ub20/share/openocd/scripts \
    -f interface/ftdi/ft232h-module-swd.cfg \
    -f target/nordic/nrf52.cfg \
    -c 'adapter speed 1000; init; reset halt' \
    -c 'dump_image nano33-full-backup.bin 0 0x100000; reset run; shutdown'
```

Programming

```sh
LD_LIBRARY_PATH=../build/sysroot-ub20/lib timeout -skill 60 ../build/sysroot-ub20/bin/openocd \
    -s ../build/sysroot-ub20/share/openocd/scripts \
    -f interface/ftdi/ft232h-module-swd.cfg \
    -f target/nordic/nrf52.cfg \
    -c 'adapter speed 1000; init; reset halt' \
    -c 'nrf5 mass_erase' \
    -c 'program ../build/nano33ble_smp_svr-qemuarm64/mcuboot/zephyr/zephyr.hex verify' \
    -c 'program ../build/nano33ble_smp_svr-qemuarm64/nano33ble_smp_svr/zephyr/zephyr.signed.hex verify reset exit'
```

Upgrade from mobile

The Android application is **nRF Connect Device Manager** (Not "nRF Connect for Mobile")

### 2026-11-12: MCUboot placement and LED boot fix

The application header was present at `0xc000`, but address zero was erased
and the CPU faulted with PC `0xfffffffe`. The MCUboot overlay incorrectly
selected `slot0_partition`, so both HEX files targeted the application slot.
Select `boot_partition` in `package/nano33ble_smp_svr/sysbuild/mcuboot.overlay`;
the application overlay must continue to select `slot0_partition`.

After `make nano33smp`, the MCUboot HEX starts at address zero and fits in
the 48 KiB boot partition. Programming only the corrected MCUboot HEX with
`program ... verify reset exit` reported `Verified OK` and booted the existing
signed application. No mass erase was needed for this repair.

SWD verification showed VTOR `0xc200`, PC `0x3123c` in the application,
and GPIO0 OUT bit 24 changing from 1 to 0 across a 300 ms run interval.
This confirms that the red RGB LED output is toggling. Visual blinking
still needs observation on the board.

OpenOCD with Beagleplay GPIO
----

> [cat /sys/kernel/debug/gpio](docs/bp-gpio.txt)

| gpio_sysfs | MikroBus | net      | DAPLink |
| ---------- | -------- | -------- | ------- |
| 640        | INT      | GPIO1_9  | SWDIO   |
| 641        | AN       | GPIO1_10 | SWDCLK  |
| 642        | PWM      | GPIO1_11 |         |
| 643        | RST      | GPIO1_12 | RESET   |

```sh
./devsync.sh gpio_init 640 out
./devsync.sh gpio_init 641 out
./devsync.sh gpio_init 643 out
openocd -f bpgpioswd.cfg
openocd -f bpgpioswd.cfg -c "program arduino_nano_33_ble_bootloader-0.9.2_s140_6.1.1.hex verify reset exit"
openocd -f bpgpioswd.cfg -c "program arduino_nano_33_ble_bootloader-0.9.2-29-g6a9a6a3_s140_6.1.1.hex verify reset exit"
```

Build with nrf sdk
----

    make CROSS_COMPILE=/home/joelai/07_sw/pkg/toolchain-arm-none-eabi/bin/arm-none-eabi- BOARD=arduino_nano_33_ble all

bootloader check DFU and FRST
----

There are two pins, DFU and FRST that bootloader will check upon reset/power:

    Double Reset Reset twice within 500 ms will enter DFU with UF2 and CDC support (only works with nRF52840)
    DFU = LOW and FRST = HIGH: Enter bootloader with UF2 and CDC support
    DFU = LOW and FRST = LOW: Enter bootloader with OTA, to upgrade with a mobile application such as Nordic nrfConnect/Toolbox
    DFU = HIGH and FRST = LOW: Factory Reset mode: erase firmware application and its data
    DFU = HIGH and FRST = HIGH: Go to application code if it is present, otherwise enter DFU with UF2
    The GPREGRET register can also be set to force the bootloader can enter any of above modes (plus a CDC-only mode for Arduino). GPREGRET is set by the application before performing a soft reset.

```c
#define BUTTON_1              _PINNUM(1, 11)  // D2 switch
#define BUTTON_2              _PINNUM(1, 12)  // D3 switch

#ifndef BUTTON_DFU
#define BUTTON_DFU      BUTTON_1
#endif

#ifndef BUTTON_FRESET
#define BUTTON_FRESET   BUTTON_2
#endif
```

garage
====

