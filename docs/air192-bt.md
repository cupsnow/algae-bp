Bluetooth on Air192
====

fact
----

- rtl8821cs bt via uart to host

build
----

kernel config

```sh
CONFIG_BT=m
CONFIG_BT_RFCOMM=m
CONFIG_BT_RFCOMM_TTY=y
CONFIG_BT_BNEP=m
CONFIG_BT_HIDP=m
CONFIG_BT_HCIUART=m
CONFIG_BT_HCIUART_RTL=y
```

firmware (fix for wiring)

```sh
zstd -d -f /lib/firmware/rtl_bt/rtl8821cs_fw.bin.zst \
    -o $(dist_DIR)/rootfs/lib/firmware/rtl_bt/rtl8821cs_fw.bin
zstd -d -f /lib/firmware/rtl_bt/rtl8821cs_config.bin.zst \
    -o $(dist_DIR)/rootfs/lib/firmware/rtl_bt/rtl8821cs_config.bin
# 5f                   ; bit2 = 1, hardware flow control enable
printf '\x5b' | dd of=$(dist_DIR)/rootfs/lib/firmware/rtl_bt/rtl8821cs_config.bin bs=1 seek=21 conv=notrunc
# 02 80 92 04          ; 0x04928002 = 1500000
printf '\x14\xc0\x52\x02' | dd of=$(dist_DIR)/rootfs/lib/firmware/rtl_bt/rtl8821cs_config.bin bs=1 seek=9 conv=notrunc
```

devicetree (add node bluetooth, and pinmux)

```dts
uart3: serial@fc008000 {
  pinctrl-names = "default";
  pinctrl-0 = <&pinctrl_uart3_default>;
  atmel,use-dma-rx;
  atmel,use-dma-tx;
  status = "okay";

  bluetooth {
    compatible = "realtek,rtl8821cs-bt", "realtek,rtl8723bs-bt";
    enable-gpios = <&pioA PIN_PA11 GPIO_ACTIVE_HIGH>;
  };

};
```
