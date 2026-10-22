# BlueZ peripheral example

Requires a running system D-Bus and bluetoothd with GATT/LE advertising support,
a BLE-capable adapter, and permission to access BlueZ and become the default
pairing agent. Stop the raw `blegatt` server before using this module.
No BlueZ private headers or libbluetooth are required; the dependency is GIO.

In tester_ev3:

```
bluez1 start
# Alternatively: bluez1 start /org/bluez/hci1
bluez1 send hello
bluez1 notify hello
bluez1 stop
```

`start` powers on the adapter, registers the agent and GATT application, enables
pairing, and advertises as `tester-bluez`. It does not erase existing bonds.
The agent becomes the system default and accepts Just Works pairing without a
prompt while running. This demo policy provides encryption without MITM
protection; choose an agent with real user confirmation for authenticated pairing.
The agent rejects PIN/passkey interaction and unrelated service authorization.
Stopping restores the previous Pairable value and releases registrations by
closing the private D-Bus connection. The adapter remains powered on. Restart
this module after restarting bluetoothd or removing/reconnecting the adapter;
registrations are not automatically recreated. If another application previously
owned the default agent, that application may need to request default status again.

On a phone, use a BLE GATT client such as nRF Connect to connect and pair:

| Attribute | UUID suffix | Operations |
|---|---|---|
| Service | `...def0` | Custom service |
| RX | `...def1` | Write / write without response; encryption required |
| TX | `...def2` | Read with encryption; notifications |

All UUIDs use prefix `12345678-1234-5678-1234-56789abc`.
RX writes print a hex dump and call the optional receive callback.
`send` updates TX for subsequent reads. `notify` updates TX and emits a value
change for subscribed clients; it fails if nobody has enabled notifications.
Enable TX notifications in the phone before running `notify`.
An ATT notification is not an application-level delivery acknowledgement.
Messages are binary, at most 20 bytes per operation, and empty messages are valid.
The CLI joins text arguments with spaces; use the C API for arbitrary binary data.
Long/prepared RX writes and nonzero write offsets are rejected. TX reads support
valid offsets. Add application framing/chunking for larger messages.

The phone can initiate pairing. To initiate it from Linux for an already
discovered BlueZ Device1 object:

```
bluez1 pair /org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF
```

The example does not scan for remote devices. Pairing is asynchronous on D-Bus
but the CLI waits for the result (30-second call timeout). Existing paired peers
do not need pairing again. Bond persistence is managed by bluetoothd.

The implementation uses C-style structs, functions, and GLib threading in
`bluez1.cpp`, compiled with the C++ compiler. Explicit pointer and enum casts
keep it compatible with C++ type checking.
The public C-compatible API is in `bluez1.h`: init, start, send, pair, stop,
and destroy. Calls must be serialized by the caller. Receive callbacks execute
on the module's D-Bus thread; copy data before returning and queue work back to
your application. Do not call module functions from that callback.
No Bluetooth work starts until `bluez1_start` is called.

Build flags use target GLib in BUILD_SYSROOT, or host pkg-config without a sysroot.
Override `bluez1_CPPFLAGS` and `bluez1_LDFLAGS` for other staging layouts.

Run the host mock integration test from this directory:

```sh
c++ -std=c++11 -Wall -Wextra -Werror -pthread \
  $(pkg-config --cflags gio-2.0) -c bluez1.cpp -o /tmp/bluez1.o
c++ -std=c++11 -Wall -Wextra -Werror -pthread \
  $(pkg-config --cflags gio-2.0) tests/bluez1_mock.cpp /tmp/bluez1.o \
  $(pkg-config --libs gio-2.0) -o /tmp/bluez1_mock
dbus-run-session -- sh -c \
  'export DBUS_SYSTEM_BUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS"; G_DEBUG=fatal-warnings /tmp/bluez1_mock'
```

The test uses a private bus and mock org.bluez, with no radio access. It verifies
ObjectManager discovery, read/write callbacks, subscription handling, size limits,
and repeatable lifecycle. It does not verify radio traffic or real pairing.
