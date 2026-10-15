Dev Info
====

Wired
----

- [Adafruit FT232H Breakout](https://learn.adafruit.com/adafruit-ft232h-breakout) I2C Mode switch turn **on**
- Nano33 BLE powered with USB
- More

```
| SWD   | Breakout | Nano33 BLE | memo     |
| ----- | -------- | ---------- | -------- |
| V3.3  | V3.3     | Pin1       | not used |
| GND   | GND      | Pin5       |          |
| SWCLK | D0       | Pin3       |          |
| SWDIO | D1       | Pin2       |          |
| RESET | D3       | Pin6       | not used |
```

Download Nano33 BLE With Openocd
----

    openocd -f interface/ftdi/ft232h-module-swd.cfg -f target/nrf52.cfg \
    -c "adapter speed 1000" \
    -c "program /home/joelai/02_dev/nano33_blinky/build/merged.hex verify reset exit"

    openocd -f interface/ftdi/ft232h-module-swd.cfg -f target/nrf52.cfg \
    -c "adapter speed 1000" \
    -c "program /home/joelai/02_dev/nano33_blinky/build/nano33_blinky/zephyr/zephyr.hex verify reset exit"
