Develop Note
====

[nRF52840 Product Specification](https://docs.nordicsemi.com/bundle/ps_nrf52840/page/keyfeatures_html5.html)

OpenOCD with FT232H
----

[Programming with Adafruit FT232H BreakOut](adafruit-ft232h.md)

check swd connection

```sh
LD_LIBRARY_PATH=../build/sysroot-ub20/lib ../build/sysroot-ub20/bin/openocd \
    -s ../build/sysroot-ub20/share/openocd/scripts \
    -f interface/ftdi/ft232h-module-swd.cfg \
    -f target/nordic/nrf52.cfg \
    -c 'adapter speed 250; init; reset halt; targets; reset run; shutdown'
```

backup existing firmware

```sh
LD_LIBRARY_PATH=../build/sysroot-ub20/lib ../build/sysroot-ub20/bin/openocd \
    -s ../build/sysroot-ub20/share/openocd/scripts \
    -f interface/ftdi/ft232h-module-swd.cfg \
    -f target/nordic/nrf52.cfg \
    -c 'init; reset halt; dump_image nano33-full-backup.bin 0 0x100000; reset run; shutdown'
```

programming

```sh
LD_LIBRARY_PATH=../build/sysroot-ub20/lib ../build/sysroot-ub20/bin/openocd \
    -s ../build/sysroot-ub20/share/openocd/scripts \
    -f interface/ftdi/ft232h-module-swd.cfg \
    -f target/nordic/nrf52.cfg \
    -c 'init; reset halt; nrf5 mass_erase' \
    -c 'program ../build/nano33ble_smp_svr-qemuarm64/mcuboot/zephyr/zephyr.hex verify' \
    -c 'program ../build/nano33ble_smp_svr-qemuarm64/nano33ble_smp_svr/zephyr/zephyr.signed.hex verify reset exit'
```

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
