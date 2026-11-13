# APP_PLATFORM?=air192
APP_PLATFORM?=qemuarm64

APP_ATTR_air192?=air192
ifneq ($(strip $(filter air192,$(APP_PLATFORM))),)
air192_DIR=/home/joelai/02_dev/eveplay-ws/air192
ARM_TOOLCHAIN_PATH?=${air192_DIR}/tool/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-linux-gnueabihf
TOOLCHAIN_PATH?=$(ARM_TOOLCHAIN_PATH)
CROSS_COMPILE?=$(ARM_CROSS_COMPILE)
TOOLCHAIN_SYSROOT?=$(abspath $(shell $(ARM_TOOLCHAIN_BIN_PATH)/$(CROSS_COMPILE)gcc -print-sysroot))
BUILD_STAGINGROOT+=$(air192_DIR)/../build/sysroot-sa7715

openssl_ACARGS_air192+=linux-armv4
libgpiod_CROSSFILE_air192=$(BUILDDIR)/meson-arm-$(APP_PLATFORM).ini
glib_CROSSFILE_air192=$(BUILDDIR)/meson-arm-$(APP_PLATFORM).ini
glib_DEP=utilinux libffi iconvgettext # use air192's pcre2
libical_CROSSFILE_air192=$(BUILDDIR)/cross-arm.cmake
systemd_CROSSFILE_air192=$(BUILDDIR)/meson-arm-$(APP_PLATFORM).ini

# GLib uses PCRE2 from the staging root. Supply its flags explicitly because
# pkg-config's single sysroot cannot describe this split dependency layout.
# This bypasses pkg-config's GLib version/dependency checks.
bluez_GLIB_CFLAGS+=-I$(BUILD_SYSROOT)/include/glib-2.0 -I$(BUILD_SYSROOT)/lib/glib-2.0/include
bluez_GLIB_LIBS+=-L$(BUILD_SYSROOT)/lib -lglib-2.0 $(BUILD_STAGINGROOT:%=-L%/lib) -lpcre2-8
bluez_ACARGS_air192+=GLIB_CFLAGS="$(bluez_GLIB_CFLAGS)" GLIB_LIBS="$(bluez_GLIB_LIBS)"

cpython_ACENV_air192+=ac_cv_file__dev_ptmx=yes ac_cv_file__dev_ptc=no
cpython_ACENV_air192+=ac_cv_buggy_getaddrinfo=no
# cpython_ACENV_air192+='ZLIB_CFLAGS="-I$(air192_DIR)/../build/sysroot-sa7715/include" ZLIB_LIBS="-L$(air192_DIR)/../build/sysroot-sa7715/lib -lz"'

cpython_ACARGS_air192+=--host=`$(CC) -dumpmachine` --build=`$(cpython_DIR)/config.guess`
cpython_ACARGS_air192+=--with-build-python=$(cpython_host_python)
cpython_ACARGS_air192+=--enable-shared

# cpython find openssl through pkg-config then /usr/include
cpython_ACARGS_air192+=--with-openssl=$(abspath $(air192_DIR)/../build/sysroot-sa7715)

beszel_GOENV_air192+=GOARCH=arm

# end of air192
endif

ifneq ($(strip $(filter ub20,$(APP_PLATFORM))),)
live555_BUILDDIR?=$(BUILDDIR)/live555-$(APP_BUILD)

# end of ub20
endif
