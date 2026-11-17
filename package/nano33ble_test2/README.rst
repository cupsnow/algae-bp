:orphan:

Nano33 BLE test2 UART console
****************************

From the repository root, configure and build both MCUboot and the application::

    make nano33test2_defconfig
    make nano33test2

The wrapper includes ``bt.conf;uart.conf``. MCUboot separately loads
``sysbuild/mcuboot.conf``. Both images use physical UART0 at 115200 baud,
8 data bits, no parity, 1 stop bit, no flow control. Connect a 3.3 V USB-UART
adapter RX to Nano D1/TX (P1.03), adapter TX to D0/RX (P1.10), and common GND.
The Nano USB connector is not this UART; ``cdc.conf`` is a separate optional
MCUmgr transport and is not used by this build.

Initial provisioning requires both images. ``make nano33test2_flash`` uses
the repository FT232H/OpenOCD setup and **mass-erases the device**, including
existing firmware and stored data. See ``docs/nano33ble/mcuboot.md`` for SWD
wiring. Build outputs are under ``../build/nano33test2/``:

* ``mcuboot/zephyr/zephyr.hex``: bootloader.
* ``nano33ble_test2/zephyr/zephyr.signed.hex``: initial application.
* ``nano33ble_test2/zephyr/zephyr.signed.bin``: firmware update payload,
  also copied to ``destdir/nano33ble/nano33test2-signed.bin`` by the build target.

Open a serial terminal, for example ``picocom -b 115200 /dev/ttyUSB0``, then
reset the board to see MCUboot messages followed by application logs and the
``nano33>`` shell prompt. Example commands::

    help
    nano info
    nano echo hello world
    nano echo "hello UART"

``nano info`` reports the build timestamp and uptime in milliseconds.
``nano echo`` demonstrates argument handling and shell output. See the
``SHELL_CMD_REGISTER`` and ``SHELL_CMD_ARG`` examples in ``src/main.c``.

Firmware upgrade over the same UART
===================================

The application accepts MCUmgr SMP over the shell UART alongside interactive
commands. The shell owns UART input and routes framed SMP packets to MCUmgr;
do not enable a second UART MCUmgr input driver on the same UART. Application
logs go through the shell backend so they share output with the prompt.
BLE management remains enabled.

Close the terminal before using the Go ``mcumgr`` CLI on that serial port::

    mcumgr --conntype serial --connstring 'dev=/dev/ttyUSB0,baud=115200' image list
    mcumgr --conntype serial --connstring 'dev=/dev/ttyUSB0,baud=115200' image upload destdir/nano33ble/nano33test2-signed.bin
    mcumgr --conntype serial --connstring 'dev=/dev/ttyUSB0,baud=115200' image list
    mcumgr --conntype serial --connstring 'dev=/dev/ttyUSB0,baud=115200' image test <uploaded-image-hash>
    mcumgr --conntype serial --connstring 'dev=/dev/ttyUSB0,baud=115200' reset
    mcumgr --conntype serial --connstring 'dev=/dev/ttyUSB0,baud=115200' image confirm

Replace ``<uploaded-image-hash>`` with the secondary-slot hash from ``image list``.
Upload a different signed build, wait for it to boot successfully, then confirm
it; an unconfirmed test image reverts on a later reset. The application receives
the upload into slot 1 and MCUboot performs the swap on reset. MCUboot provides
boot logging here, not an interactive shell or serial recovery upload mode;
UART updates require a working application. UART commands and DFU still need
hardware validation. This example retains the default MCUboot development key
and unauthenticated management services.

Upstream sample reference
*************************

.. zephyr:code-sample:: smp-svr
   :name: SMP server

   Implement a Simple Management Protocol (SMP) server.

Overview
********

This sample application implements a Simple Management Protocol (SMP) server.
SMP is a basic transfer encoding for use with the MCUmgr management protocol.
For more information about MCUmgr and SMP, please see :ref:`device_mgmt`.

This sample application supports the following MCUmgr transports by default:

    * Shell
    * Bluetooth
    * UDP

``smp_svr`` enables support for the following command groups:

    * ``fs_mgmt``
    * ``img_mgmt``
    * ``os_mgmt``
    * ``stat_mgmt``
    * ``shell_mgmt``

Caveats
*******

* The MCUboot bootloader is required for ``img_mgmt`` to function
  properly. More information about the Device Firmware Upgrade subsystem and
  MCUboot can be found in :ref:`mcuboot`.

Prerequisites
*************

Use of a tool
=============

To interact remotely with the management subsystem on a device, a tool is required to interact with
it. There are various tools available which are listed on the :ref:`mcumgr_tools_libraries` page
of the Management subsystem documentation which can be used as a client for a Zephyr MCUmgr SMP
server.

Building a BLE Controller
=========================

.. note::
   This section is only relevant for Linux users

If you want to try out Device Firmware Upgrade (DFU) over the air using
Bluetooth Low Energy (BLE) and do not have a built-in or pluggable BLE radio,
you can build one and use it following the instructions in
:ref:`bluetooth-hci-uart-bluez`.

.. _smp_svr_sample_build:

Building the sample application
*******************************

The below steps describe how to build and run the ``smp_svr`` sample in Zephyr with ``MCUboot``
included. The ``smp_svr`` sample comes in different flavours.

.. tabs::

   .. group-tab:: Bluetooth

      To build the bluetooth sample:

      .. zephyr-app-commands::
         :tool: west
         :zephyr-app: samples/subsys/mgmt/mcumgr/smp_svr
         :board: nrf52dk/nrf52832
         :goals: build
         :west-args: --sysbuild
         :gen-args: -DEXTRA_CONF_FILE="bt.conf"
         :compact:

   .. group-tab:: Serial

      To build the serial sample with file-system and shell management support:

      .. zephyr-app-commands::
         :tool: west
         :zephyr-app: samples/subsys/mgmt/mcumgr/smp_svr
         :board: frdm_k64f
         :goals: build
         :west-args: --sysbuild
         :gen-args: -DEXTRA_CONF_FILE="serial.conf;fs.conf;shell-mgmt.conf"
         :compact:

   .. group-tab:: Raw UART (serial)

      .. note::
         Raw UART support sends raw MCUmgr packets over a UART device without using the SMP over console protocol, this is binary data and cannot be used if e.g. the shell is enabled on the same UART.

      To build the raw UART (serial) sample with file-system management support:

      .. zephyr-app-commands::
         :tool: west
         :zephyr-app: samples/subsys/mgmt/mcumgr/smp_svr
         :board: nrf52840dk/nrf52840
         :goals: build
         :west-args: --sysbuild
         :gen-args: -DEXTRA_CONF_FILE="raw-serial.conf;fs.conf"
         :compact:

   .. group-tab:: USB CDC_ACM

      To build the serial sample with USB CDC_ACM backend:

      .. zephyr-app-commands::
         :tool: west
         :zephyr-app: samples/subsys/mgmt/mcumgr/smp_svr
         :board: nrf52840dk/nrf52840
         :goals: build
         :west-args: --sysbuild
         :gen-args: -DEXTRA_CONF_FILE="cdc.conf" -DEXTRA_DTC_OVERLAY_FILE="usb.overlay"
         :compact:

      .. note::
         If you are building the sample with the CDC overlay files
         and plan to test it, make sure to connect to the USB port
         on your board that is connected directly to the MCU.

   .. group-tab:: Shell

      To build the shell sample:

      .. zephyr-app-commands::
         :tool: west
         :zephyr-app: samples/subsys/mgmt/mcumgr/smp_svr
         :board: frdm_k64f
         :goals: build
         :west-args: --sysbuild
         :gen-args: -DEXTRA_CONF_FILE="shell.conf"
         :compact:

   .. group-tab:: UDP

      The UDP transport for SMP supports both IPv4 and IPv6.
      In the sample, both IPv4 and IPv6 are enabled, but they can be
      enabled and disabled separately.

      To build the UDP sample:

      .. zephyr-app-commands::
         :tool: west
         :zephyr-app: samples/subsys/mgmt/mcumgr/smp_svr
         :board: frdm_k64f
         :goals: build
         :west-args: --sysbuild
         :gen-args: -DEXTRA_CONF_FILE="udp.conf"
         :compact:

      To build UDP sample with 802154 Subg networking:

      .. zephyr-app-commands::
         :tool: west
         :zephyr-app: samples/subsys/mgmt/mcumgr/smp_svr
         :board: beagleconnect_freedom
         :goals: build
         :west-args: --sysbuild
         :gen-args: -DEXTRA_CONF_FILE="overlay-udp.conf;802154-subg.conf"
         :compact:

      To build the UDP with DTLS sample:

      .. zephyr-app-commands::
         :tool: west
         :zephyr-app: samples/subsys/mgmt/mcumgr/smp_svr
         :board: bl5340_dvk/nrf5340/cpuapp
         :goals: build
         :west-args: --sysbuild
         :gen-args: -DEXTRA_CONF_FILE="udp-dtls.conf"
         :compact:

Flashing the sample image
*************************

The original application will be built for slot-0, see :ref:`flash_map_api` for details on flash
partitioning. Flash both MCUboot and the sample application:

.. code-block:: console

    west flash

Sample image: hello world!
==========================

The ``smp_svr`` app is ready to run.  Just reset your board and test the app
with your choice of tool's ``echo`` functionality, which will
send a string to the remote target device and have it echo it back.

J-Link Virtual MSD Interaction Note
***********************************

On boards where a J-Link OB is present which has both CDC and MSC (virtual Mass
Storage Device, also known as drag-and-drop) support, the MSD functionality can
prevent mcumgr commands over the CDC UART port from working due to how USB
endpoints are configured in the J-Link firmware (for example on the Nordic
``nrf52840dk``) because of limiting the maximum packet size (most likely to occur
when using image management commands for updating firmware). This issue can be
resolved by disabling MSD functionality on the J-Link device, follow the
instructions on :ref:`nordic_segger_msd` to disable MSD support.

Device Firmware Upgrade (DFU)
*****************************

Now that the SMP server is running on your board and you are able to communicate with it using a
client, you might want to test what is commonly called "OTA DFU", or Over-The-Air Device Firmware
Upgrade. This works for both BT and UDP.

The general sequence of a DFU process is as follows:

* Build an MCUboot enabled application using sysbuild, see :ref:`smp_svr_sample_build`
* Upload the signed image using an MCUmgr client
* Listing the images on the device using an MCUmgr client
* Mark the uploaded image for testing using an MCUmgr client
* Reset the device remotely using an MCUmgr client
* Confirm the uploaded image using an MCUmgr client (optional)

Direct image upload and Image mapping to MCUboot slot
=====================================================

Currently MCUmgr supports, for direct upload, 4 target images, of which first two are mapped
into MCUboot primary (slot-0) and secondary (slot-1) respectively.

For clarity, here is DTS label to slot to ``<image>`` translation table:

    +-----------+--------+------------+
    | DTS label | Slot   | -n <image> |
    +===========+========+============+
    | "image-0" | slot-0 |     1      |
    +-----------+--------+------------+
    | "image-1" | slot-1 |     0, 1   |
    +-----------+--------+------------+
    | "image-2" |        |     2      |
    +-----------+--------+------------+
    | "image-3" |        |     3      |
    +-----------+--------+------------+

.. note::

   There is a slot info command that can be used to see information on all slots and get the
   upload ``image`` ID to use to update that slot, see :ref:`mcumgr_smp_group_1_slot_info` for
   details.

Upload the signed image
=======================

To upload the signed image, refer to the documentation for your chosen tool, select the new
firmware file to upload and begin the upload.

.. note::

   At the beginning of the upload process, the target might start erasing the image slot, taking
   several dozen seconds for some targets.

List the images
===============

A list of images (slot-0 and slot-1) that are present can now be obtained on the remote target
device using the tool of your choice, which should print the status and hash values of each of
the images present.

Test the image
==============

In order to instruct MCUboot to swap the images, the image needs to be tested first, making sure it
boots, see the instructions in the tool of your choice. Upon reboot, MCUBoot will swap to the new
image.

.. note::

   Some tools may allow for listing the hash of an image without needing to upload them.
   ``imgtool`` can also be used to list the image hash, albeit in a C hex-array format, by using
   the ``dumpinfo`` command on the signed update file, e.g.

   .. code-block:: console

       imgtool dumpinfo smp_svr/zephyr/zephyr.signed.bin

       Printing content of signed image: zephyr.signed.bin

       #### Image header (offset: 0x0) ############################
       magic:              0x96f3b83d
       ...
       #### TLV area (offset: 0xbfa0) #############################
       magic:     0x6907
       area size: 0x150
               ---------------------------------------------
               type: SHA256 (0x10)
               len:  0x20
               data: 0x9b 0xa9 0x84 0x48 0xe5 0x4d 0xac 0x40
                     0x62 0x29 0xe2 0x11 0x17 0x96 0x66 0xd9
                     0xae 0x83 0x9a 0x37 0x71 0x00 0xfc 0xe2
                     0xc0 0x30 0x30 0x4f 0xfc 0x40 0x58 0xaa
               ---------------------------------------------
       ...

   The full SHA256 hash for the above output would be:
   9ba98448e54dac406229e211179666d9ae839a377100fce2c030304ffc4058aa

Reset remotely
==============

The device can be reset remotely to observe (use the console output) how MCUboot swaps the images,
check the documentation in the tool of your choice. Upon reset MCUboot will swap slot-0 and
slot-1.

Confirm new image
=================

The new image is now loaded into slot-0, but it will be swapped back into slot-1 on the next
reset unless the image is confirmed. Confirm the image using the tool of your choice.

.. note::

   If you try to send the very same image that is already flashed in slot-0 then the procedure
   will not complete successfully since the hash values for both slots will be identical.

Download files from/upload files to file system
***********************************************

SMP server supports downloading files from/uploading files to the on-device
:ref:`file_system_api`, this is useful with e.g. FS log backend, when files are stored in
non-volatile memory. Build and flash ``smp_svr`` using sysbuild and then use the tool of your
choice to download files from the file system. The full path of the file on the device must be
known and used.
