# CC1352P7 UART flasher in C

Standalone Linux userspace flasher for BeaglePlay's onboard CC1352P7.
No Python or libgpiod dependency. Uses termios and Linux GPIO character-device
ABI v1 (`CONFIG_GPIO_CDEV` and `CONFIG_GPIO_CDEV_V1` must be enabled).
This is a focused replacement for the BeaglePlay raw-binary programming path,
not every feature/device supported by cc1352-flasher.

## Build

From algae-bp:

```sh
# Host build, for protocol tests only
make -C package/cc1352-flash
python3 package/cc1352-flash/test_protocol.py

# AArch64 Linux binary for BeaglePlay (use your normal target compiler if different)
make -B -C package/cc1352-flash \
  CC="$PWD/tool/gcc-aarch64/bin/aarch64-none-linux-gnu-gcc"
```

Copy `package/cc1352-flash/cc1352-flash` to BeaglePlay. It is dynamically
linked; the target rootfs must provide compatible libc/loader libraries.

## Program on BeaglePlay

Disable the `mcu` child under Linux `main_uart6` in the boot DT, keep UART6
enabled, deploy the DTB and reboot. Unbinding just gb_beagleplay does not
necessarily expose a TTY. Stop other serial readers/getty before flashing.

```sh
# As root on BeaglePlay; ERASES customer flash, including existing CCFG.
./cc1352-flash -p /dev/ttyS1 zephyr.bin
```

Confirm UART6's actual TTY on the running image; `/dev/ttyS1` follows this
project's serial1 alias but must not be assumed for other images. Without `-p`,
the tool uses `/dev/play/cc1352/uart`.

GPIO lines are discovered across `/dev/gpiochip*` using DT line names
`CC1352P7_BOOT` and `CC1352P7_RSTN`. No global GPIO numbering is assumed.
The BeaglePlay sequence drives BOOT low and RESET low, waits 200 ms, releases
RESET high, waits 200 ms, drives BOOT high, then waits 200 ms before releasing
GPIO handles. The configured backdoor must be DIO15, active low.

```sh
# Target manually placed in ROM bootloader; bypass GPIO requests
./cc1352-flash --manual -p /dev/ttyS1 zephyr.bin

# Enter bootloader and compare flash with the supplied image; no erase/write
./cc1352-flash --verify-only -p /dev/ttyS1 zephyr.bin
```

Input is a raw binary loaded at address zero, 1..720896 bytes, padded with
0xff to the complete 704 KiB flash. Intel HEX and ELF are not supported.
Use a complete CC1352P7 image including CCFG, such as the board-targeted Zephyr
binary. This tool does not validate CCFG, target identity, or firmware validity.
A wrong image can disable future UART bootloader access; recovery may require
JTAG. This is not a backup or partial-flash updater.

Programming sequence: 115200 8N1, auto-baud `55 55`, status, BANK_ERASE,
DOWNLOAD at zero, SEND_DATA in at most 252-byte chunks with status checks,
full-flash CRC32-IEEE verification, RESET. Commands use big-endian parameters,
8-bit additive packet checksums, ACK/NACK handling and bounded I/O deadlines.
Failures stop immediately without retrying ambiguous flash writes; CRC failure
leaves the target in the bootloader. Re-enter bootloader and rerun after fixing
the cause. Verification-only mode also resets after a successful match.

## Existing kernel firmware-upload route

The inspected `algae-ws/linux/drivers/greybus/gb-beagleplay.c` registers
`firmware_upload_register(..., "cc1352p7", ...)`. With that driver bound and
firmware-upload support enabled, inspect:

```sh
ls /sys/class/firmware/cc1352p7/
```

The upload API uses `loading`, `data`, `status`, `error` and `remaining_size`.
The driver requires exactly 704 KiB, unlike this tool which pads shorter inputs.
A typical upload (root, with a correctly padded complete image) is:

```sh
echo 1 > /sys/class/firmware/cc1352p7/loading
cat firmware-704k.bin > /sys/class/firmware/cc1352p7/data
echo 0 > /sys/class/firmware/cc1352p7/loading
cat /sys/class/firmware/cc1352p7/status
cat /sys/class/firmware/cc1352p7/error
```

The operation is asynchronous: poll status until `idle`, then inspect error and
remaining_size. If writing data fails, cancel with `echo -1 > loading` rather
than committing an incomplete upload. This kernel route owns the UART/GPIOs;
do not run the standalone UART flasher concurrently. Kernel driver availability
and behavior depend on the booted kernel, not just the source in the workspace.

## TI references and validation

- [SWRA466E: CC2538, CC13xx, and CC26xx Serial Bootloader Interface](https://www.ti.com/lit/pdf/swra466),
  especially sections 2.1, 2.2, 2.3 and 4.3. It explicitly includes CC13x2x7.
- [SWCU192: CC13x2x7, CC26x2x7 Technical Reference Manual](https://www.ti.com/lit/pdf/swcu192),
  device-specific bootloader command reference identified by SWRA466E.
- [TI SBL example source download](https://www.ti.com/lit/zip/swra466).
  TI's accompanying example/library uses Windows Visual C++; its transport is
  not directly a Linux C implementation.
- Local Linux `drivers/greybus/gb-beagleplay.c`, used to cross-check BeaglePlay
  GPIO timing, command fields and CRC behavior. The standalone implementation
  is written independently against the documented protocol.

Validation: warning-clean native and AArch64 compilation; PTY emulator checks
full 704 KiB programming, 0xff padding, command checksums/parameters, fragmented
responses with idle zeros, verification-only, NACK, status failure, response
checksum failure and CRC mismatch. No physical BeaglePlay flashing or GPIO
validation has been performed. Tests emulate the ROM; they do not establish
hardware compatibility by themselves.
