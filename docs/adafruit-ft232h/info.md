Adafruit FT232H Breakout
----

- [Adafruit FT232H Breakout](https://learn.adafruit.com/adafruit-ft232h-breakout)


OpenOCD
----

```
| SWD   | Adafruit FT232H |
| ----- | --------------- |
| V3.3  | V3.3            |
| GND   | GND             |
| SWCLK | D0              |
| SWDIO | D1              |
| RESET | D3              |
```

```text
FT232H D0 ---------------- Nano SWCLK
FT232H D1 ---------------- +
                           +-- Nano SWDIO
FT232H D2 -- 220-470 ohm --+
FT232H D4 ---------------- Nano RESET       optional
FT232H GND --------------- Nano GND
```

    openocd -f interface/ftdi/ft232h-module-swd.cfg -f target/nrf52.cfg \
    -c "adapter speed 1000" \
    -c "program /home/joelai/02_dev/nano33_blinky/build/merged.hex verify reset exit"

    openocd -f interface/ftdi/ft232h-module-swd.cfg -f target/nrf52.cfg \
    -c "adapter speed 1000" \
    -c "program /home/joelai/02_dev/nano33_blinky/build/nano33_blinky/zephyr/zephyr.hex verify reset exit"

