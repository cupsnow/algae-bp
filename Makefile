#------------------------------------
#
include builder/proj.mk
-include site.mk

export SHELL=/bin/bash

PARALLEL_BUILD?=$(or $(1),-j)10

PKGDIR=$(PROJDIR)/package
PKGDIR2=$(abspath $(PROJDIR)/..)

BUILDDIR2=$(abspath $(PROJDIR)/../build)

APP_ATTR_ub20?=ub20

# ti_linux
APP_ATTR_bp?=bp

APP_ATTR_qemuarm64?=qemuarm64

APP_PLATFORM?=bp

# locale_posix2c
export APP_ATTR?=$(APP_ATTR_$(APP_PLATFORM)) locale_posix2c

ifneq ($(strip $(filter bp qemuarm64,$(APP_PLATFORM))),)
APP_BUILD=aarch64
else
APP_BUILD=$(APP_PLATFORM)
endif

ARM_TOOLCHAIN_PATH?=$(PROJDIR)/tool/gcc-arm
ARM_CROSS_COMPILE?=$(shell $(ARM_TOOLCHAIN_PATH)/bin/*-gcc -dumpmachine)-
PATH_PUSH+=$(ARM_TOOLCHAIN_PATH)/bin

AARCH64_TOOLCHAIN_PATH?=$(PROJDIR)/tool/gcc-aarch64
AARCH64_CROSS_COMPILE?=$(shell $(AARCH64_TOOLCHAIN_PATH)/bin/*-gcc -dumpmachine)-
PATH_PUSH+=$(AARCH64_TOOLCHAIN_PATH)/bin

ifneq ($(strip $(filter bp qemuarm64,$(APP_PLATFORM))),)
TOOLCHAIN_PATH?=$(AARCH64_TOOLCHAIN_PATH)
CROSS_COMPILE?=$(AARCH64_CROSS_COMPILE)
TOOLCHAIN_SYSROOT?=$(abspath $(shell $(TOOLCHAIN_PATH)/bin/$(CROSS_COMPILE)gcc -print-sysroot))
else ifneq ($(strip $(filter bbb xm,$(APP_PLATFORM))),)
TOOLCHAIN_PATH?=$(ARM_TOOLCHAIN_PATH)
CROSS_COMPILE?=$(ARM_CROSS_COMPILE)
TOOLCHAIN_SYSROOT?=$(abspath $(shell $(TOOLCHAIN_PATH)/bin/$(CROSS_COMPILE)gcc -print-sysroot))
else
TOOLCHAIN_SYSROOT?=$(abspath $(shell $(CROSS_COMPILE)gcc -print-sysroot))
endif

BUILD_SYSROOT?=$(BUILDDIR2)/sysroot-$(APP_PLATFORM)
BUILD_PKGCFG_ENV+=PKG_CONFIG_LIBDIR="$(or $(1),$(BUILD_SYSROOT))/lib/pkgconfig" \
    PKG_CONFIG_SYSROOT_DIR="$(or $(1),$(BUILD_SYSROOT))"

export PATH:=$(call ENVPATH,$(PROJDIR)/tool/bin $(PATH_PUSH) $(PATH))

PYVENVDIR=$(PROJDIR)/.venv

CPPFLAGS+=
CFLAGS+=
CXXFLAGS+=
LDFLAGS+=

GENDIR:=
GENPYVENV:=

CLIARGS_VAL=$(if $(filter x"command line",x"$(strip $(origin $(1)))"),$($(1)))

CLIARGS_VERBOSE=$(call CLIARGS_VAL,V)

RSYNC_VERBOSE:=--debug=FILTER

ifneq ($(strip $(filter x"1", x"$(CLIARGS_VERBOSE)")),)
RSYNC_VERBOSE+=-v
CP_VERBOSE+=-v
MV_VERBOSE+=-v
ELFSTRIP_VERBOSE+=-v
endif

#------------------------------------
#
.DEFAULT_GOAL=help
help: help1

help1:
	@echo "APP_ATTR: $(APP_ATTR)"
	@echo "AARCH64 build target: $$($(AARCH64_CROSS_COMPILE)gcc -dumpmachine)"
	@echo "ARM build target: $$($(ARM_CROSS_COMPILE)gcc -dumpmachine)"
	@echo "TOOLCHAIN_SYSROOT: $(TOOLCHAIN_SYSROOT)"

#------------------------------------
#
atf_DIR?=$(PKGDIR2)/arm-trusted-firmware
atf_BUILDDIR=$(BUILDDIR2)/atf-$(APP_PLATFORM)
atf_MAKE=$(MAKE) BUILD_BASE=$(atf_BUILDDIR) $(atf_MAKEARGS-$(APP_PLATFORM)) \
    -C $(atf_DIR)

atf_MAKEARGS-bp+=ARCH=aarch64 PLAT=k3 TARGET_BOARD=lite SPD=opteed \
    K3_PM_SYSTEM_SUSPEND=1 DEBUG=1 CROSS_COMPILE=$(AARCH64_CROSS_COMPILE)

atf:
	$(atf_MAKE) $(PARALLEL_BUILD) all

atf_%:
	$(atf_MAKE) $(PARALLEL_BUILD) $(@:atf_%=%)

#------------------------------------
# for build doc: pip install pyelftools cryptography
#
optee_DIR?=$(PKGDIR2)/optee_os
optee_BUILDDIR=$(BUILDDIR2)/optee-$(APP_PLATFORM)
optee_MAKE=$(MAKE) O=$(optee_BUILDDIR) $(optee_MAKEARGS-$(APP_PLATFORM)) \
    -C $(optee_DIR)

optee_MAKEARGS-bp+=CFG_ARM64_core=y PLATFORM=k3-am62x CFG_TEE_CORE_LOG_LEVEL=2 \
    CFG_TEE_CORE_DEBUG=y CFG_WITH_SOFTWARE_PRNG=y \
    CROSS_COMPILE=$(ARM_CROSS_COMPILE) CROSS_COMPILE64=$(AARCH64_CROSS_COMPILE)

optee: | $(PYVENVDIR)
	. $(PYVENVDIR)/bin/activate \
	  && $(optee_MAKE) $(PARALLEL_BUILD)

optee_%:
	. $(PYVENVDIR)/bin/activate \
	  && $(optee_MAKE) $(PARALLEL_BUILD) $(@:optee_%=%)

GENPYVENV+=pyelftools cryptography

#------------------------------------
# git clong -b ti-linux-firmware git://git.ti.com/processor-firmware/ti-linux-firmware.git
# 
ti-linux-fw_DIR?=$(PKGDIR2)/ti-linux-firmware

#------------------------------------
# apt install libssl-dev device-tree-compiler swig python3-distutils libgnutls28-dev
# apt install python3-dev python3-setuptools
# for build doc: pip install yamllint jsonschema
#
# qemu-system-aarch64 -machine virt,virtualization=on,secure=off -cpu max \
#   -bios ../build/uboot-qemuarm64/u-boot.bin -nographic
#
uboot_DIR?=$(PKGDIR2)/u-boot
uboot_BUILDDIR=$(BUILDDIR2)/uboot-$(or $1,$(APP_PLATFORM))

uboot_MAKE=$(MAKE) O=$(uboot_BUILDDIR) $(uboot_MAKEARGS-$(APP_PLATFORM)) \
    -C $(uboot_DIR)

uboot_MAKEARGS-bp-r5+=BINMAN_INDIRS=$(ti-linux-fw_DIR) \
    ARCH=arm CROSS_COMPILE=$(ARM_CROSS_COMPILE)

uboot_defconfig-bp-r5=am62x_beagleplay_r5_defconfig

uboot_MAKEARGS-bp-a53+=BINMAN_INDIRS=$(ti-linux-fw_DIR) \
    BL31=$(firstword $(wildcard $(atf_BUILDDIR)/k3/lite/release/bl31.bin \
        $(atf_BUILDDIR)/k3/lite/debug/bl31.bin)) \
    TEE=$(optee_BUILDDIR)/core/tee-raw.bin \
	ARCH=arm CROSS_COMPILE=$(AARCH64_CROSS_COMPILE)

uboot_defconfig-bp-a53=am62x_beagleplay_a53_defconfig

uboot_MAKEARGS-qemuarm64+=CROSS_COMPILE=$(AARCH64_CROSS_COMPILE)

uboot_defconfig-qemuarm64=qemu_arm64_defconfig

UBOOT_TOOLS+=dumpimage fdtgrep gen_eth_addr gen_ethaddr_crc \
    mkenvimage mkimage proftool spl_size_limit

ifeq ("$(MAKELEVEL)","20")
$(error Maybe endless loop, MAKELEVEL: $(MAKELEVEL))
endif

ifneq ($(strip $(filter bp,$(APP_PLATFORM))),)
# bp runs uboot for 2 different core, pass APP_PLATFORM for specified core to else
#

$(addprefix uboot_,menuconfig htmldocs tools tools_install):
	$(MAKE) APP_PLATFORM=bp-a53 atf_BUILDDIR=$(atf_BUILDDIR) \
	    optee_BUILDDIR=$(optee_BUILDDIR) uboot_$(@:uboot_%=%)

ubootenv:
	$(MAKE) APP_PLATFORM=bp-a53 $@

uboot:
	$(MAKE) APP_PLATFORM=bp-r5 uboot
	$(MAKE) APP_PLATFORM=bp-a53 atf_BUILDDIR=$(atf_BUILDDIR) FORCE_ORIGIN=1 \
	    optee_BUILDDIR=$(optee_BUILDDIR) uboot

uboot_%:
	$(MAKE) APP_PLATFORM=bp-r5 uboot_$(@:uboot_%=%)
	$(MAKE) APP_PLATFORM=bp-a53 atf_BUILDDIR=$(atf_BUILDDIR) \
	    optee_BUILDDIR=$(optee_BUILDDIR) uboot_$(@:uboot_%=%)

else
# normal case

uboot_defconfig $(uboot_BUILDDIR)/.config: | $(uboot_BUILDDIR)
	if [ "$(FORCE_ORIGIN)" != "1" ] && [ -f uboot-$(APP_PLATFORM).defconfig ]; then \
	  cp -v uboot-$(APP_PLATFORM).defconfig $(uboot_BUILDDIR)/.config && \
	  yes "" | $(uboot_MAKE) oldconfig; \
	else \
	  $(uboot_MAKE) $(uboot_defconfig-$(APP_PLATFORM)); \
	fi

$(addprefix uboot_,help):
	$(uboot_MAKE) $(PARALLEL_BUILD) $(@:uboot_%=%)

$(addprefix uboot_,htmldocs): | $(PYVENVDIR) $(uboot_BUILDDIR)
	. $(PYVENVDIR)/bin/activate \
	  && $(uboot_MAKE) $(PARALLEL_BUILD) $(@:uboot_%=%)

uboot_tools_install: DESTDIR=$(PROJDIR)/tool
uboot_tools_install:
	[ -d $(DESTDIR)/bin ] || $(MKDIR) $(DESTDIR)/bin
	$(MAKE) uboot_tools
	for i in $(UBOOT_TOOLS); do \
	  cp -v $(uboot_BUILDDIR)/tools/$$i $(DESTDIR)/bin/; \
	done

$(addprefix uboot_,menuconfig savedefconfig oldconfig): | $(uboot_BUILDDIR)/.config
	$(uboot_MAKE) $(PARALLEL_BUILD) $(@:uboot_%=%)

ubootenv: DESTDIR=$(BUILDDIR)
ubootenv: $(PROJDIR)/tool/bin/mkenvimage
	$(call CMD_UENV)

uboot: | $(uboot_BUILDDIR)/.config $(PYVENVDIR)
	. $(PYVENVDIR)/bin/activate \
	  && $(uboot_MAKE) $(PARALLEL_BUILD)

uboot_%: | $(uboot_BUILDDIR)/.config $(PYVENVDIR)
	. $(PYVENVDIR)/bin/activate \
	  && $(uboot_MAKE) $(PARALLEL_BUILD) $(@:uboot_%=%)

GENPYVENV+=setuptools pyyaml yamllint jsonschema swig

# for tools_install
GENDIR+=$(PROJDIR)/tool/bin

GENDIR+=$(uboot_BUILDDIR)

# for htmldocs
GENPYVENV+=sphinx sphinx_rtd_theme six sphinx-prompt

# end of uboot APP_PLATFORM
endif

CMD_UENV=$(PROJDIR)/tool/bin/mkenvimage \
    $$([ x"$$($(call CMD_SED_KEYVAL1,CONFIG_SYS_REDUNDAND_ENVIRONMENT) $(uboot_BUILDDIR)/.config)"=x"y" ] && echo -r) \
    -s $$($(call CMD_SED_KEYVAL1,CONFIG_ENV_SIZE) $(uboot_BUILDDIR)/.config) \
    -o $(or $(2),$(DESTDIR)/uboot.env) \
	$(or $(1),ubootenv-$(APP_PLATFORM).txt) \
  && chmod a+r $(or $(2),$(DESTDIR)/uboot.env)

$(addprefix $(PROJDIR)/tool/bin/,$(UBOOT_TOOLS)):
	$(MAKE) DESTDIR=$(PROJDIR)/tool uboot_tools_install

#------------------------------------
#
wlregdb_DIR?=$(PKGDIR2)/wireless-regdb

#------------------------------------
# for install: make with variable INSTALL_HDR_PATH, INSTALL_MOD_PATH 
#

ifeq ("$(strip $(filter bp,$(APP_ATTR)))_$(strip $(filter ti_linux,$(APP_ATTR_bp)))","bp_ti_linux")
linux_DIR?=$(PKGDIR2)/ti-processor-sdk/board-support/ti-linux-kernel-6.1.83+gitAUTOINC+c1c2f1971f-ti
linux_BUILDDIR?=$(BUILDDIR2)/ti-linux-$(APP_PLATFORM)
else
linux_DIR?=$(PKGDIR2)/linux
linux_BUILDDIR?=$(BUILDDIR2)/linux-$(APP_PLATFORM)
endif
linux_MAKE=$(MAKE) O=$(linux_BUILDDIR) $(linux_MAKEARGS-$(APP_PLATFORM)) \
    -C $(linux_DIR)

linux_MAKEARGS-bp+=ARCH=arm64 CROSS_COMPILE=$(AARCH64_CROSS_COMPILE)

linux_defconfig-bp=defconfig

linux_MAKEARGS-qemuarm64+=ARCH=arm64 CROSS_COMPILE=$(AARCH64_CROSS_COMPILE)

linux_defconfig-qemuarm64=defconfig

ifeq ("$(strip $(filter bp,$(APP_ATTR)))_$(strip $(filter ti_linux,$(APP_ATTR_bp)))","bp_ti_linux")
linux_defconfig $(linux_BUILDDIR)/.config: | $(linux_BUILDDIR)
	$(linux_MAKE) defconfig ti_arm64_prune.config
else
linux_defconfig $(linux_BUILDDIR)/.config: | $(linux_BUILDDIR)
	if [ -f "$(PROJDIR)/linux-$(APP_PLATFORM).config" ]; then \
	  cp -v $(PROJDIR)/linux-$(APP_PLATFORM).config $(linux_BUILDDIR)/.config \
	    && yes "" | $(linux_MAKE) oldconfig; \
	else \
	  $(linux_MAKE) $(linux_defconfig-$(APP_PLATFORM)); \
	fi
endif

$(addprefix linux_,help):
	$(linux_MAKE) $(PARALLEL_BUILD) $(@:linux_%=%)

# dep: apt install dvipng imagemagick
#      pip install sphinx_rtd_theme six
$(addprefix linux_,htmldocs): | $(PYVENVDIR) $(linux_BUILDDIR)
	. $(PYVENVDIR)/bin/activate \
	  && $(linux_MAKE) $(PARALLEL_BUILD) $(@:linux_%=%)

$(addprefix linux_,menuconfig savedefconfig oldconfig): | $(linux_BUILDDIR)/.config
	$(linux_MAKE) $(PARALLEL_BUILD) $(@:linux_%=%)

linux: | $(linux_BUILDDIR)/.config
	$(linux_MAKE) $(PARALLEL_BUILD)

linux_%: | $(linux_BUILDDIR)/.config
	$(linux_MAKE) $(PARALLEL_BUILD) $(@:linux_%=%)

kernelrelease=$(BUILDDIR)/kernelrelease-$(APP_PLATFORM)
kernelrelease $(kernelrelease): | $(dir $(kernelrelease))
	$(linux_MAKE) -s --no-print-directory kernelrelease > $(kernelrelease)
	@cat "$(kernelrelease)"

GENDIR+=$(dir $(kernelrelease))

GENPYVENV+=sphinx_rtd_theme six

GENDIR+=$(linux_BUILDDIR)

#------------------------------------
# for install: make with variable CONFIG_PREFIX
#
busybox_DIR?=$(PKGDIR2)/busybox
busybox_BUILDDIR?=$(BUILDDIR2)/busybox-$(APP_BUILD)
busybox_MAKE=$(MAKE) ARCH=arm64 CROSS_COMPILE=$(CROSS_COMPILE) \
    O=$(busybox_BUILDDIR) -C $(busybox_DIR)

busybox_defconfig $(busybox_BUILDDIR)/.config: | $(busybox_BUILDDIR)
	if [ -f "$(PROJDIR)/busybox.config" ]; then \
	  cp -v $(PROJDIR)/busybox.config $(busybox_BUILDDIR)/.config && \
	  yes "" | $(busybox_MAKE) oldconfig; \
	else \
	  $(busybox_MAKE) defconfig; \
	fi

$(addprefix busybox_,mrproper):
	$(filter-out O=%,$(busybox_MAKE)) $(@:busybox_%=%)

$(addprefix busybox_,help doc html): | $(PYVENVDIR)
	. $(PYVENVDIR)/bin/activate && \
	  $(busybox_MAKE) $(@:busybox_%=%)

busybox_destpkg $(busybox_BUILDDIR)-destpkg.tar.xz:
	$(RMTREE) $(busybox_BUILDDIR)-destpkg
	$(MAKE) DESTDIR=$(busybox_BUILDDIR)-destpkg busybox_install
	tar -Jcvf $(busybox_BUILDDIR)-destpkg.tar.xz \
	    -C $(dir $(busybox_BUILDDIR)-destpkg) \
		$(notdir $(busybox_BUILDDIR)-destpkg)
	$(RMTREE) $(busybox_BUILDDIR)-destpkg

busybox_destpkg_install: DESTDIR=$(BUILD_SYSROOT)
busybox_destpkg_install: | $(busybox_BUILDDIR)-destpkg.tar.xz
	[ -d "$(DESTDIR)" ] || $(MKDIR) $(DESTDIR)
	tar -Jxvf $(busybox_BUILDDIR)-destpkg.tar.xz --strip-components=1 \
	    -C $(DESTDIR)

busybox_destdep_install: $(foreach iter,$(busybox_DEP),$(iter)_destdep_install)
	$(MAKE) busybox_destpkg_install

busybox_distclean:
	$(RMTREE) $(busybox_BUILDDIR) 

busybox: | $(busybox_BUILDDIR)/.config
	$(busybox_MAKE) $(PARALLEL_BUILD)

busybox_install: DESTDIR=$(BUILD_SYSROOT)
busybox_install: $(busybox_BUILDDIR)/.config
	$(busybox_MAKE) CONFIG_PREFIX=$(DESTDIR) $(PARALLEL_BUILD) $(@:busybox_%=%)

busybox_%: $(busybox_BUILDDIR)/.config
	$(busybox_MAKE) $(PARALLEL_BUILD) $(@:busybox_%=%)

GENDIR+=$(busybox_BUILDDIR)

#------------------------------------
#
mmcutils_DIR=$(PKGDIR2)/mmc-utils
mmcutils_BUILDDIR=$(BUILDDIR2)/mmcutils-$(APP_PLATFORM)
mmcutils_MAKE=$(MAKE) CC=$(CC) C= -C $(mmcutils_BUILDDIR)

mmcutils_defconfig $(mmcutils_BUILDDIR)/Makefile: | $(mmcutils_BUILDDIR)
	rsync -a $(RSYNC_VERBOSE) $(mmcutils_DIR)/* $(mmcutils_BUILDDIR)/

mmcutils_install: $(DESTDIR)=$(BUILD_SYSROOT)
mmcutils_install: | $(mmcutils_BUILDDIR)/Makefile
	$(mmcutils_MAKE) $(PARALLEL_BUILD) DESTDIR=$(DESTDIR) prefix= $(@:mmcutils_%=%)

mmcutils_destpkg $(mmcutils_BUILDDIR)-destpkg.tar.xz:
	$(RMTREE) $(mmcutils_BUILDDIR)-destpkg
	$(MAKE) DESTDIR=$(mmcutils_BUILDDIR)-destpkg mmcutils_install
	tar -Jcvf $(mmcutils_BUILDDIR)-destpkg.tar.xz \
	    -C $(dir $(mmcutils_BUILDDIR)-destpkg) \
		$(notdir $(mmcutils_BUILDDIR)-destpkg)
	$(RMTREE) $(mmcutils_BUILDDIR)-destpkg

mmcutils_destpkg_install: DESTDIR=$(BUILD_SYSROOT)
mmcutils_destpkg_install: | $(mmcutils_BUILDDIR)-destpkg.tar.xz
	[ -d "$(DESTDIR)" ] || $(MKDIR) $(DESTDIR)
	tar -Jxvf $(mmcutils_BUILDDIR)-destpkg.tar.xz --strip-components=1 \
	    -C $(DESTDIR)

mmcutils_destdep_install: $(foreach iter,$(mmcutils_DEP),$(iter)_destdep_install)
	$(MAKE) mmcutils_destpkg_install

mmcutils_distclean:
	$(RMDIR) $(mmcutils_BUILDDIR)

mmcutils: | $(mmcutils_BUILDDIR)/Makefile
	$(mmcutils_MAKE) $(PARALLEL_BUILD)

mmcutils_%: | $(mmcutils_BUILDDIR)/Makefile
	$(mmcutils_MAKE) $(PARALLEL_BUILD) $(@:mmcutils_%=%)

GENDIR+=$(mmcutils_BUILDDIR)

#------------------------------------
#
zlib_DIR=$(PKGDIR2)/zlib
zlib_BUILDDIR?=$(BUILDDIR2)/zlib-$(APP_BUILD)

zlib_MAKE=$(MAKE) DESTDIR=$(DESTDIR) -C $(zlib_BUILDDIR)

zlib_defconfig $(zlib_BUILDDIR)/configure.log: | $(zlib_BUILDDIR)
	cd $(zlib_BUILDDIR) \
	  && prefix= CROSS_PREFIX=$(CROSS_COMPILE) \
	      CFLAGS="$(zlib_CFLAGS_$(APP_PLATFORM))" \
	      $(zlib_DIR)/configure $(zlib_ACARGS_$(APP_PLATFORM))

GENDIR+=$(zlib_BUILDDIR)

zlib_distclean:
	$(RM) $(zlib_BUILDDIR)

zlib_install: DESTDIR=$(BUILD_SYSROOT)
zlib_install: | $(zlib_BUILDDIR)/configure.log
	$(zlib_MAKE) $(PARALLEL_BUILD) DESTDIR=$(DESTDIR) $(@:zlib_%=%)
	for i in zlib; do \
	  if [ -f "$(DESTDIR)/lib/$${i}.la" ]; then \
	    rm -f $(DESTDIR)/lib/$${i}.la; \
	  fi && \
	  if [ -f "$(DESTDIR)/lib/pkgconfig/$${i}.pc" ]; then \
	    rm -f $(DESTDIR)/lib/pkgconfig/$${i}.pc; \
	  fi; \
	done
	rmdir $(DESTDIR)/lib/pkgconfig

zlib_destpkg $(zlib_BUILDDIR)-destpkg.tar.xz:
	$(RMTREE) $(zlib_BUILDDIR)-destpkg
	$(MAKE) DESTDIR=$(zlib_BUILDDIR)-destpkg zlib_install
	tar -Jcvf $(zlib_BUILDDIR)-destpkg.tar.xz \
	    -C $(dir $(zlib_BUILDDIR)-destpkg) \
	    $(notdir $(zlib_BUILDDIR)-destpkg)
	$(RMTREE) $(zlib_BUILDDIR)-destpkg

zlib_destpkg_install: DESTDIR=$(BUILD_SYSROOT)
zlib_destpkg_install: | $(zlib_BUILDDIR)-destpkg.tar.xz
	[ -d "$(DESTDIR)" ] || $(MKDIR) $(DESTDIR)
	tar -Jxvf $(zlib_BUILDDIR)-destpkg.tar.xz --strip-components=1 \
	    -C $(DESTDIR)

zlib_destdep_install: $(foreach iter,$(zlib_DEP),$(iter)_destdep_install)
	$(MAKE) zlib_destpkg_install

zlib: | $(zlib_BUILDDIR)/configure.log
	$(zlib_MAKE) $(PARALLEL_BUILD)

zlib_%: | $(zlib_BUILDDIR)/configure.log
	$(zlib_MAKE) $(PARALLEL_BUILD) $(patsubst _%,%,$(@:zlib%=%))

#------------------------------------
#
ncursesw_DIR?=$(PKGDIR2)/ncurses
ncursesw_BUILDDIR?=$(BUILDDIR2)/ncursesw-$(APP_BUILD)
ncursesw_TINFODIR=/usr/share/terminfo

# ncursesw_ACARGS_$(APP_PLATFORM)+=--without-debug
ncursesw_ACARGS_ub20+=--with-pkg-config=/lib
ncursesw_ACARGS_bp+=--disable-db-install --without-tests --without-manpages

ncursesw_MAKE=$(MAKE) -C $(ncursesw_BUILDDIR)

# no strip to prevent not recoginize crosscompiled executable
ncursesw_defconfig $(ncursesw_BUILDDIR)/Makefile: | $(ncursesw_BUILDDIR)
	cd $(ncursesw_BUILDDIR) \
	  && $(BUILD_PKGCFG_ENV) $(ncursesw_DIR)/configure \
	      --host=`$(CC) -dumpmachine` --prefix= --with-termlib --with-ticlib \
	      --enable-widec --disable-stripping \
	      --with-default-terminfo-dir=$(ncursesw_TINFODIR) \
	      CFLAGS="-fPIC $(ncursesw_CFLAGS_$(APP_PLATFORM))" \
	      $(ncursesw_ACARGS_$(APP_PLATFORM))

# remove wrong pc file for the crosscompiled lib
ncursesw_install: DESTDIR=$(BUILD_SYSROOT)
ncursesw_install: | $(ncursesw_BUILDDIR)/Makefile
	$(ncursesw_MAKE) $(PARALLEL_BUILD)
	$(ncursesw_MAKE) $(PARALLEL_BUILD) DESTDIR=$(DESTDIR) install
	[ -d "$(DESTDIR)" ] || $(MKDIR) $(DESTDIR)
	echo "INPUT(-lncursesw)" > $(DESTDIR)/lib/libcurses.so;
	for i in ncurses form panel menu tinfo; do \
	  if [ -e $(DESTDIR)/lib/lib$${i}w.so ]; then \
	    echo "INPUT(-l$${i}w)" > $(DESTDIR)/lib/lib$${i}.so; \
	  fi; \
	  if [ -e $(DESTDIR)/lib/lib$${i}w.a ]; then \
	    ln -sfn lib$${i}w.a $(DESTDIR)/lib/lib$${i}.a; \
	  fi; \
	done

ncursesw_destpkg $(ncursesw_BUILDDIR)-destpkg.tar.xz:
	$(RMTREE) $(ncursesw_BUILDDIR)-destpkg
	$(MAKE) DESTDIR=$(ncursesw_BUILDDIR)-destpkg ncursesw_install
	tar -Jcvf $(ncursesw_BUILDDIR)-destpkg.tar.xz \
	    -C $(dir $(ncursesw_BUILDDIR)-destpkg) \
	    $(notdir $(ncursesw_BUILDDIR)-destpkg)
	$(RMTREE) $(ncursesw_BUILDDIR)-destpkg

ncursesw_destpkg_install: DESTDIR=$(BUILD_SYSROOT)
ncursesw_destpkg_install: | $(ncursesw_BUILDDIR)-destpkg.tar.xz
	[ -d "$(DESTDIR)" ] || $(MKDIR) $(DESTDIR)
	tar -Jxvf $(ncursesw_BUILDDIR)-destpkg.tar.xz --strip-components=1 \
	    -C $(DESTDIR)

ncursesw_destdep_install: $(foreach iter,$(ncursesw_DEP),$(iter)_destdep_install)
	$(MAKE) ncursesw_destpkg_install

ncursesw: | $(ncursesw_BUILDDIR)/Makefile
	$(ncursesw_MAKE) $(PARALLEL_BUILD)

ncursesw_%: | $(ncursesw_BUILDDIR)/Makefile
	$(ncursesw_MAKE) $(PARALLEL_BUILD) $(@:ncursesw_%=%)

GENDIR+=$(ncursesw_BUILDDIR)

terminfo: DESTDIR=$(BUILD_SYSROOT)
terminfo: | $(PROJDIR)/tool/bin/tic
	$(call CMD_TERMINFO)

terminfo_BUILDDIR=$(BUILDDIR2)/terminfo
terminfo_destpkg $(terminfo_BUILDDIR)-destpkg.tar.xz:
	$(RMTREE) $(terminfo_BUILDDIR)-destpkg
	$(MAKE) DESTDIR=$(terminfo_BUILDDIR)-destpkg terminfo
	tar -Jcvf $(terminfo_BUILDDIR)-destpkg.tar.xz \
	    -C $(dir $(terminfo_BUILDDIR)-destpkg) \
	    $(notdir $(terminfo_BUILDDIR)-destpkg)
	$(RMTREE) $(terminfo_BUILDDIR)-destpkg

terminfo_destpkg_install: DESTDIR=$(BUILD_SYSROOT)
terminfo_destpkg_install: | $(terminfo_BUILDDIR)-destpkg.tar.xz
	[ -d "$(DESTDIR)" ] || $(MKDIR) $(DESTDIR)
	tar -Jxvf $(terminfo_BUILDDIR)-destpkg.tar.xz --strip-components=1 \
	    -C $(DESTDIR)

terminfo_destdep_install: $(foreach iter,$(terminfo_DEP),$(iter)_destdep_install)
	$(MAKE) terminfo_destpkg_install

# Create small terminfo refer to https://invisible-island.net/ncurses/ncurses.faq.html#big_terminfo
# refine to comma saperated list when use in tic
TERMINFO_NAMES=$(subst $(SPACE),$(COMMA),$(sort $(subst $(COMMA),$(SPACE), \
    ansi ansi-m color_xterm,linux,pcansi-m,rxvt-basic,vt52,vt100 \
    vt102,vt220,xterm,tmux-256color,screen-256color,xterm-256color screen)))
TERMINFO_TIC=LD_LIBRARY_PATH=$(PROJDIR)/tool/lib \
    TERMINFO=$(PROJDIR)/tool/$(ncursesw_TINFODIR) \
	$(PROJDIR)/tool/bin/tic
CMD_TERMINFO= \
  { [ -d "$(or $(1),$(DESTDIR))/$(ncursesw_TINFODIR)" ] || \
    $(MKDIR) $(or $(1),$(DESTDIR))/$(ncursesw_TINFODIR); } \
  && $(TERMINFO_TIC) -s -1 -I -x -e"$(TERMINFO_NAMES)" \
      $(ncursesw_DIR)/misc/terminfo.src > $(BUILDDIR)/terminfo.src \
  && $(TERMINFO_TIC) -s -o $(or $(1),$(DESTDIR))/$(ncursesw_TINFODIR) \
      $(BUILDDIR)/terminfo.src

$(addprefix $(PROJDIR)/tool/bin/,tic):
	$(MAKE) DESTDIR=$(PROJDIR)/tool APP_PLATFORM=ub20 ncursesw_destdep_install

#------------------------------------
#
TOOLCHAIN_I18NPATH=$(TOOLCHAIN_SYSROOT)/usr/share/i18n
CMD_LOCALE_BASE=I18NPATH=$(TOOLCHAIN_I18NPATH) localedef
CMD_LOCALE_COMPILE=$(if $(2),,$(error "CMD_LOCALE_COMPILE invalid argument")) \
    $(CMD_LOCALE_BASE) -i $1 -f $2 $(or $(3),$(1).$(2))
CMD_LOCALE_AR=$(if $(2),,$(error "CMD_LOCALE_AR invalid argument")) \
    $(CMD_LOCALE_BASE) --add-to-archive --replace --prefix=$(1) $(2)
CMD_LOCALE_LIST=$(if $(1),,$(error "CMD_LOCALE_LIST invalid argument")) \
    $(CMD_LOCALE_BASE) --list-archive --prefix=$(1)
CMD_CHARMAP_INST=rsync -a $(RSYNC_VERBOSE) --ignore-missing-args \
    $(patsubst %,$(TOOLCHAIN_I18NPATH)/charmaps/%.gz,$(2)) \
    $(1)/usr/share/i18n/charmaps/

locale_BUILDDIR=$(BUILDDIR)/locale-$(APP_BUILD)

locale_install: | $(locale_BUILDDIR)
locale_install: DESTDIR=$(BUILDDIR)/locale-destdir
locale_install:
	[ -d "$(DESTDIR)/usr/lib/locale" ] || $(MKDIR) $(DESTDIR)/usr/lib/locale
	[ -d "$(DESTDIR)/usr/share/i18n/charmaps" ] || $(MKDIR) $(DESTDIR)/usr/share/i18n/charmaps
ifneq ($(strip $(filter locale_posix2c,$(APP_ATTR))),)
	$(call CMD_LOCALE_COMPILE,POSIX,UTF-8,$(locale_BUILDDIR)/C.UTF-8) || [ $$? -eq 1 ]
	$(call CMD_LOCALE_AR,$(DESTDIR),$(locale_BUILDDIR)/C.UTF-8)
else
	$(call CMD_LOCALE_COMPILE,C,UTF-8,$(locale_BUILDDIR)/C.UTF-8) || [ $$? -eq 1 ]
	$(call CMD_LOCALE_AR,$(DESTDIR),$(locale_BUILDDIR)/C.UTF-8)
	$(call CMD_LOCALE_COMPILE,POSIX,UTF-8,$(locale_BUILDDIR)/POSIX.UTF-8) || [ $$? -eq 1 ]
	$(call CMD_LOCALE_AR,$(DESTDIR),$(locale_BUILDDIR)/POSIX.UTF-8)
endif
	# $(call CMD_LOCALE_COMPILE,en_US,UTF-8,$(locale_BUILDDIR)/en_US.UTF-8) || [ $$? -eq 1 ]
	# $(call CMD_LOCALE_AR,$(DESTDIR),$(locale_BUILDDIR)/en_US.UTF-8)
	# $(call CMD_CHARMAP_INST,$(DESTDIR),UTF-8)
	# $(call CMD_LOCALE_COMPILE,zh_TW,BIG5,$(locale_BUILDDIR)/zh_TW.BIG5) || [ $$? -eq 1 ]
	# $(call CMD_LOCALE_AR,$(DESTDIR),$(locale_BUILDDIR)/zh_TW.BIG5)
	# $(call CMD_CHARMAP_INST,$(DESTDIR),BIG5)
	# @echo "Locale archived: $$($(call CMD_LOCALE_LIST,$(DESTDIR)) | xargs)"

locale_destpkg $(locale_BUILDDIR)-destpkg.tar.xz: | $(locale_BUILDDIR)
	$(RMTREE) $(locale_BUILDDIR)-destpkg
	$(MAKE) DESTDIR=$(locale_BUILDDIR)-destpkg locale_install
	tar -Jcvf $(locale_BUILDDIR)-destpkg.tar.xz \
	    -C $(dir $(locale_BUILDDIR)-destpkg) \
	    $(notdir $(locale_BUILDDIR)-destpkg)
	$(RMTREE) $(locale_BUILDDIR)-destpkg

locale_destpkg_install: DESTDIR=$(BUILD_SYSROOT)
locale_destpkg_install: | $(locale_BUILDDIR)-destpkg.tar.xz
	[ -d "$(DESTDIR)" ] || $(MKDIR) $(DESTDIR)
	tar -Jxvf $(locale_BUILDDIR)-destpkg.tar.xz --strip-components=1 \
	    -C $(DESTDIR)
	# @echo "Locale:"
	# @$(call CMD_LOCALE_LIST,$(DESTDIR))

locale_destdep_install: $(foreach iter,$(locale_DEP),$(iter)_destdep_install)
	$(MAKE) locale_destpkg_install

GENDIR+=$(locale_BUILDDIR)

#------------------------------------
#
libevent_DIR?=$(PKGDIR2)/libevent
libevent_BUILDDIR?=$(BUILDDIR2)/libevent-$(APP_BUILD)

libevent_MAKE=$(MAKE) -C $(libevent_BUILDDIR)

$(libevent_DIR)/configure: $(libevent_DIR)/autogen.sh
	cd $(libevent_DIR) \
	  && ./autogen.sh

libevent_defconfig $(libevent_BUILDDIR)/Makefile: | $(libevent_DIR)/configure $(libevent_BUILDDIR)
	cd $(libevent_BUILDDIR) \
	  && $(BUILD_PKGCFG_ENV) $(libevent_DIR)/configure \
	      --host=`$(CC) -dumpmachine` --prefix= --disable-openssl \
		  --disable-mbedtls --with-pic \
	      $(libevent_ACARGS_$(APP_PLATFORM))

libevent_install: DESTDIR=$(BUILD_SYSROOT)
libevent_install: | $(libevent_BUILDDIR)/Makefile
	$(libevent_MAKE) $(PARALLEL_BUILD) DESTDIR=$(DESTDIR) $(@:libevent_%=%)
	for i in libevent_core libevent_extra libevent libevent_pthreads; do \
	  if [ -f "$(DESTDIR)/lib/$${i}.la" ]; then \
	    rm -f $(DESTDIR)/lib/$${i}.la; \
	  fi && \
	  if [ -f "$(DESTDIR)/lib/pkgconfig/$${i}.pc" ]; then \
	    rm -f $(DESTDIR)/lib/pkgconfig/$${i}.pc; \
	  fi; \
	done
	rmdir $(DESTDIR)/lib/pkgconfig

libevent_destpkg $(libevent_BUILDDIR)-destpkg.tar.xz:
	$(RMTREE) $(libevent_BUILDDIR)-destpkg
	$(MAKE) DESTDIR=$(libevent_BUILDDIR)-destpkg libevent_install
	tar -Jcvf $(libevent_BUILDDIR)-destpkg.tar.xz \
	    -C $(dir $(libevent_BUILDDIR)-destpkg) \
	    $(notdir $(libevent_BUILDDIR)-destpkg)
	$(RMTREE) $(libevent_BUILDDIR)-destpkg

libevent_destpkg_install: DESTDIR=$(BUILD_SYSROOT)
libevent_destpkg_install: | $(libevent_BUILDDIR)-destpkg.tar.xz
	[ -d "$(DESTDIR)" ] || $(MKDIR) $(DESTDIR)
	tar -Jxvf $(libevent_BUILDDIR)-destpkg.tar.xz --strip-components=1 \
	    -C $(DESTDIR)

libevent_destdep_install: $(foreach iter,$(libevent_DEP),$(iter)_destdep_install)
	$(MAKE) libevent_destpkg_install

libevent: | $(libevent_BUILDDIR)/Makefile
	$(libevent_MAKE) $(PARALLEL_BUILD)

libevent_%: | $(libevent_BUILDDIR)/Makefile
	$(libevent_MAKE) $(PARALLEL_BUILD) $(@:libevent_%=%)

GENDIR+=$(libevent_BUILDDIR)

#------------------------------------
# dep ncursesw libevent locale terminfo
#
tmux_DEP=ncursesw libevent locale terminfo
tmux_DIR=$(PKGDIR2)/tmux
tmux_BUILDDIR?=$(BUILDDIR2)/tmux-$(APP_BUILD)

# tmux_MAKE=$(MAKE) DESTDIR=$(DESTDIR) -C $(tmux_BUILDDIR)
tmux_MAKE=$(MAKE) -C $(tmux_BUILDDIR)

tmux_INCDIR=$(BUILD_SYSROOT)/include $(BUILD_SYSROOT)/include/ncursesw
tmux_LIBDIR=$(BUILD_SYSROOT)/lib $(BUILD_SYSROOT)/lib64

# tmux_CFLAGS+=$(BUILD_CFLAGS2_$(APP_PLATFORM)) -fPIC
# ifneq ($(strip $(filter release1,$(APP_ATTR))),)
# tmux_CFLAGS+=-O3
# else ifneq ($(strip $(filter debug1,$(APP_ATTR))),)
# tmux_CFLAGS+=-g
# endif
# tmux_CFGPARAM_$(APP_PLATFORM)+=CFLAGS="$(tmux_CFLAGS)"

$(tmux_DIR)/autogen.sh:

$(tmux_DIR)/configure: | $(tmux_DIR)/autogen.sh
	cd $(tmux_DIR) \
	  && ./autogen.sh

tmux_defconfig $(tmux_BUILDDIR)/Makefile: | $(tmux_DIR)/configure $(tmux_BUILDDIR)
	cd $(tmux_BUILDDIR) \
	  && $(BUILD_ENV) $(tmux_DIR)/configure \
	      --host=`$(CC) -dumpmachine` --prefix= \
	      ac_cv_func_strtonum_working=no \
	      CPPFLAGS="$(addprefix -I,$(tmux_INCDIR))" \
	      LDFLAGS="$(addprefix -L,$(tmux_LIBDIR))" \
	      $(tmux_ACARGS_$(APP_PLATFORM))

tmux_install: DESTDIR=$(BUILD_SYSROOT)

tmux_destpkg $(tmux_BUILDDIR)-destpkg.tar.xz:
	$(RMTREE) $(tmux_BUILDDIR)-destpkg
	$(MAKE) DESTDIR=$(tmux_BUILDDIR)-destpkg tmux_install
	tar -Jcvf $(tmux_BUILDDIR)-destpkg.tar.xz \
	    -C $(dir $(tmux_BUILDDIR)-destpkg) \
	    $(notdir $(tmux_BUILDDIR)-destpkg)
	$(RMTREE) $(tmux_BUILDDIR)-destpkg

tmux_destpkg_install: DESTDIR=$(BUILD_SYSROOT)
tmux_destpkg_install: | $(tmux_BUILDDIR)-destpkg.tar.xz
	[ -d "$(DESTDIR)" ] || $(MKDIR) $(DESTDIR)
	tar -Jxvf $(tmux_BUILDDIR)-destpkg.tar.xz --strip-components=1 \
	    -C $(DESTDIR)

tmux_destdep: $(foreach iter,$(tmux_DEP),$(iter)_destdep_install)
	$(MAKE) tmux

tmux_destdep_install: tmux_destdep
	$(MAKE) tmux_destpkg_install

# tmux_dist_install: DESTDIR=$(BUILD_SYSROOT)
# tmux_dist_install:
# 	$(RM) $(tmux_BUILDDIR)_footprint
# 	$(call RUN_DIST_INSTALL1,tmux,$(tmux_BUILDDIR)/Makefile)

tmux: | $(tmux_BUILDDIR)/Makefile
	$(tmux_MAKE) $(PARALLEL_BUILD)

tmux_%: | $(tmux_BUILDDIR)/Makefile
	$(tmux_MAKE) $(PARALLEL_BUILD) $(@:tmux_%=%)

GENDIR+=$(tmux_BUILDDIR)

#------------------------------------
#
openssl_DEP=zlib
openssl_DIR=$(PKGDIR2)/openssl
openssl_BUILDDIR?=$(BUILDDIR2)/openssl-$(APP_BUILD)
openssl_MAKE=$(MAKE) DESTDIR=$(DESTDIR) -C $(openssl_BUILDDIR)

openssl_ACARGS_ub20+=linux-x86_64
openssl_ACARGS_bp+=linux-aarch64

# enable-engine enable-afalgeng
openssl_defconfig $(openssl_BUILDDIR)/configdata.pm: | $(openssl_BUILDDIR)
	cd $(openssl_BUILDDIR) \
	  && $(openssl_DIR)/Configure --cross-compile-prefix=$(CROSS_COMPILE) \
	      --prefix=/ --openssldir=/lib/ssl no-tests no-hw-padlock \
	      $(openssl_ACARGS_$(APP_PLATFORM)) \
	      -L$(BUILD_SYSROOT)/lib -I$(BUILD_SYSROOT)/include

GENDIR+=$(openssl_BUILDDIR)

openssl_install: DESTDIR=$(BUILD_SYSROOT)
openssl_install: $(openssl_BUILDDIR)/configdata.pm
	$(openssl_MAKE) install_sw install_ssldirs
	for i in libcrypto libssl openssl; do \
	  if [ -f "$(DESTDIR)/lib/$${i}.la" ]; then \
	    rm -f $(DESTDIR)/lib/$${i}.la; \
	  fi && \
	  if [ -f "$(DESTDIR)/lib/pkgconfig/$${i}.pc" ]; then \
	    rm -f $(DESTDIR)/lib/pkgconfig/$${i}.pc; \
	  fi; \
	done
	rmdir $(DESTDIR)/lib/pkgconfig

openssl_destpkg $(openssl_BUILDDIR)-destpkg.tar.xz:
	$(RMTREE) $(openssl_BUILDDIR)-destpkg
	$(MAKE) DESTDIR=$(openssl_BUILDDIR)-destpkg openssl_install
	tar -Jcvf $(openssl_BUILDDIR)-destpkg.tar.xz \
	    -C $(dir $(openssl_BUILDDIR)-destpkg) \
	    $(notdir $(openssl_BUILDDIR)-destpkg)
	$(RMTREE) $(openssl_BUILDDIR)-destpkg

openssl_destpkg_install: DESTDIR=$(BUILD_SYSROOT)
openssl_destpkg_install: | $(openssl_BUILDDIR)-destpkg.tar.xz
	[ -d "$(DESTDIR)" ] || $(MKDIR) $(DESTDIR)
	tar -Jxvf $(openssl_BUILDDIR)-destpkg.tar.xz --strip-components=1 \
	    -C $(DESTDIR)

openssl_destdep_install: $(foreach iter,$(openssl_DEP),$(iter)_destdep_install)
	$(MAKE) openssl_destpkg_install

openssl: $(openssl_BUILDDIR)/configdata.pm
	$(openssl_MAKE) $(PARALLEL_BUILD)

openssl_%: $(openssl_BUILDDIR)/configdata.pm
	$(openssl_MAKE) $(PARALLEL_BUILD) $(@:openssl_%=%)

#------------------------------------
#
libnl_DIR?=$(PKGDIR2)/libnl
libnl_BUILDDIR?=$(BUILDDIR2)/libnl-$(APP_BUILD)

libnl_MAKE=$(MAKE) -C $(libnl_BUILDDIR)

$(libnl_DIR)/configure: $(libnl_DIR)/autogen.sh
	cd $(libnl_DIR) \
	  && ./autogen.sh

libnl_defconfig $(libnl_BUILDDIR)/Makefile: | $(libnl_DIR)/configure $(libnl_BUILDDIR)
	cd $(libnl_BUILDDIR) \
	  && $(BUILD_PKGCFG_ENV) $(libnl_DIR)/configure \
	      --host=`$(CC) -dumpmachine` --prefix= --disable-openssl \
		  --disable-mbedtls --with-pic \
	      $(libnl_ACARGS_$(APP_PLATFORM))

CMD_RM_PAT=$(if $(3),,$(error "CMD_RM_PAT invalid argument")) \
  for i in $(3); do \
    for j in $$(find $(2) -maxdepth 1 -iname $${i}$(1)); do \
      rm -f $${j}; \
    done; \
  done

libnl_install: DESTDIR=$(BUILD_SYSROOT)
libnl_install: | $(libnl_BUILDDIR)/Makefile
	$(libnl_MAKE) $(PARALLEL_BUILD) DESTDIR=$(DESTDIR) $(@:libnl_%=%)
	$(call CMD_RM_PAT,.la,$(DESTDIR)/lib, \
	  libnl-3* libnl-cli-3* libnl-genl-3* libnl-nf-3* libnl-route-3*)
	$(call CMD_RM_PAT,.pc,$(DESTDIR)/lib/pkgconfig, \
	  libnl-3* libnl-cli-3* libnl-genl-3* libnl-nf-3* libnl-route-3*)
	$(call CMD_RM_PAT,.la,$(DESTDIR)/lib/libnl/cli/cls, \
	  basic cgroup)
	$(call CMD_RM_PAT,.la,$(DESTDIR)/lib/libnl/cli/qdisc, \
	  bfifo blackhole fq_codel htb ingress pfifo plug )
	rmdir $(DESTDIR)/lib/pkgconfig

libnl_destpkg $(libnl_BUILDDIR)-destpkg.tar.xz:
	$(RMTREE) $(libnl_BUILDDIR)-destpkg
	$(MAKE) DESTDIR=$(libnl_BUILDDIR)-destpkg libnl_install
	tar -Jcvf $(libnl_BUILDDIR)-destpkg.tar.xz \
	    -C $(dir $(libnl_BUILDDIR)-destpkg) \
	    $(notdir $(libnl_BUILDDIR)-destpkg)
	$(RMTREE) $(libnl_BUILDDIR)-destpkg

libnl_destpkg_install: DESTDIR=$(BUILD_SYSROOT)
libnl_destpkg_install: | $(libnl_BUILDDIR)-destpkg.tar.xz
	[ -d "$(DESTDIR)" ] || $(MKDIR) $(DESTDIR)
	tar -Jxvf $(libnl_BUILDDIR)-destpkg.tar.xz --strip-components=1 \
	    -C $(DESTDIR)

libnl_destdep_install: $(foreach iter,$(libnl_DEP),$(iter)_destdep_install)
	$(MAKE) libnl_destpkg_install

libnl: | $(libnl_BUILDDIR)/Makefile
	$(libnl_MAKE) $(PARALLEL_BUILD)

libnl_%: | $(libnl_BUILDDIR)/Makefile
	$(libnl_MAKE) $(PARALLEL_BUILD) $(@:libnl_%=%)

GENDIR+=$(libnl_BUILDDIR)

#------------------------------------
# dep: libnl
#
iw_DIR=$(PKGDIR2)/iw
iw_BUILDDIR?=$(BUILDDIR2)/iw-$(APP_BUILD)
# iw_INCDIR=$(BUILD_SYSROOT)/include $(BUILD_SYSROOT)/include/libnl3
iw_LIBDIR=$(BUILD_SYSROOT)/lib

iw_MAKE=PKG_CONFIG_LIBDIR="$(BUILD_SYSROOT)/lib/pkgconfig" \
    PKG_CONFIG_SYSROOT_DIR="$(BUILD_SYSROOT)" \
    PREFIX=/ DESTDIR=$(DESTDIR) CC=$(CC) LDFLAGS="$(addprefix -L,$(iw_LIBDIR)) -lm" \
    $(MAKE) -C $(iw_BUILDDIR)

iw_defconfig $(iw_BUILDDIR)/Makefile:
	[ -d $(iw_BUILDDIR) ] || $(MKDIR) $(iw_BUILDDIR)
	$(CP) $(iw_DIR)/* $(iw_BUILDDIR)/

iw_install: DESTDIR=$(BUILD_SYSROOT)

iw_dist_install: DESTDIR=$(BUILD_SYSROOT)
iw_dist_install:
	$(RM) $(iw_BUILDDIR)_footprint
	$(call RUN_DIST_INSTALL1,iw,$(iw_BUILDDIR)/Makefile)

iw: | $(iw_BUILDDIR)/Makefile
	$(iw_MAKE) $(BUILDPARALLEL:%=-j%)

iw_%: | $(iw_BUILDDIR)/Makefile
	$(iw_MAKE) $(BUILDPARALLEL:%=-j%) $(@:iw_%=%)


#------------------------------------
#
dummy_DIR=$(PROJDIR)/package/dummy1

dummy1:
	$(MAKE) PROJDIR=$(PROJDIR) CROSS_COMPILE=$(CROSS_COMPILE) -C $(dummy_DIR)

#------------------------------------
#
dist_DIR=$(PROJDIR)/destdir

SD_BOOT=$(firstword $(wildcard /media/$(USER)/BOOT /media/$(USER)/boot))
SD_ROOTFS=$(firstword $(wildcard /media/$(USER)/rootfs))

CMD_RSYNC_TOOLCHAIN_SYSROOT=$(if $(1),,$(error "CMD_RSYNC_TOOLCHAIN_SYSROOT invalid argument")) \
  cd $(TOOLCHAIN_SYSROOT) \
    && rsync -aR --ignore-missing-args $(RSYNC_VERBOSE) \
        $(foreach i,audit/ gconv/ locale/ libasan.* libgfortran.* libubsan.* \
	        *.a *.o *.la,--exclude="${i}") \
        lib lib64 usr/lib usr/lib64 \
        $(1) \
    && rsync -aR --ignore-missing-args $(RSYNC_VERBOSE) \
        $(foreach i,sbin/sln usr/bin/gdbserver,--exclude="${i}") \
        sbin usr/bin usr/sbin \
        $(1)

CMD_RSYNC_PREBUILT=$(if $(2),,$(error "CMD_RSYNC_PREBUILT invalid argument")) \
    $(if $(strip $(wildcard $(2))), \
      rsync -a $(RSYNC_VERBOSE) -I $(wildcard $(2)) $(1))

CMD_GENROOT_EXT4= \
  $(RMTREE) $(2) \
    && truncate -s 255M $(2) \
    && fakeroot mkfs.ext4 -d $(1) $(2)

dist_rootfs_phase1:
# build package
	$(MAKE) $(addsuffix _destdep_install, \
	    busybox tmux mmcutils)

dist_rootfs_phase2: DESTDIR=$(dist_DIR)/rootfs
dist_rootfs_phase2:
# install prebuilt
	for i in dev lib/firmware media proc root sys tmp var/run; do \
	  [ -d "$(DESTDIR)/$${i}" ] || $(MKDIR) "$(DESTDIR)/$${i}"; \
	done
	$(call CMD_RSYNC_TOOLCHAIN_SYSROOT,$(DESTDIR)/)
	$(call CMD_RSYNC_PREBUILT,$(DESTDIR)/,$(PROJDIR)/prebuilt/common/*)
	$(call CMD_RSYNC_PREBUILT,$(DESTDIR)/,$(PROJDIR)/prebuilt/$(APP_PLATFORM)/common/*)
	rsync -L $(RSYNC_VERBOSE) \
	    $$(find $(DESTDIR)/etc/skel/ -maxdepth 1 -type f) \
		$(DESTDIR)/root/
	[ -f $(DESTDIR)/root/.exrc ] \
	  && chmod 0700 $(DESTDIR)/root/.exrc
	rsync -a $(RSYNC_VERBOSE) $(wlregdb_DIR)/regulatory.db \
	    $(wlregdb_DIR)/regulatory.db.p7s \
	    $(DESTDIR)/lib/firmware/
	ln -sfn /var/run/udhcpc/resolv.conf $(DESTDIR)/etc/resolv.conf
	ln -sfn /var/run/ld.so.cache $(DESTDIR)/etc/ld.so.cache
	rsync -L $(RSYNC_VERBOSE) $(PROJDIR)/builder/devsync.sh $(DESTDIR)/root/
ifeq (1,1)
	$(MAKE) dummy1
	rsync -L $(RSYNC_VERBOSE) $(BUILDDIR)/dummy1/tester_syslog $(DESTDIR)/root/
endif

dist-qemuarm64_phase1:
	$(MAKE) uboot linux $(kernelrelease)
	$(MAKE) linux_modules linux_dtbs
	$(MAKE) INSTALL_HDR_PATH=$(BUILD_SYSROOT) linux_headers_install
	$(RMTREE) $(BUILD_SYSROOT)/lib/modules
	$(MAKE) INSTALL_MOD_PATH=$(BUILD_SYSROOT) linux_modules_install
	$(MAKE) dist_rootfs_phase1

dist-qemuarm64_phase2: | $(dist_DIR)/$(APP_PLATFORM)/boot
dist-qemuarm64_phase2: | $(dist_DIR)/$(APP_PLATFORM)/rootfs/lib
	$(MAKE) DESTDIR=$(dist_DIR)/$(APP_PLATFORM)/boot ubootenv
	rsync -L $(RSYNC_VERBOSE) $(uboot_BUILDDIR)/u-boot.bin \
	    $(linux_BUILDDIR)/arch/arm64/boot/Image.gz \
	    $(linux_BUILDDIR)/arch/arm64/boot/Image \
	    $(linux_BUILDDIR)/vmlinux \
	    $(dist_DIR)/$(APP_PLATFORM)/boot/
	rsync -a $(RSYNC_VERBOSE) $(BUILD_SYSROOT)/* \
	    $(dist_DIR)/$(APP_PLATFORM)/rootfs/
	$(RMTREE) $(dist_DIR)/$(APP_PLATFORM)/rootfs/include \
	    $(dist_DIR)/$(APP_PLATFORM)/rootfs/lib/*.a \
	    $(dist_DIR)/$(APP_PLATFORM)/rootfs/lib64/*.a
	$(busybox_DIR)/examples/depmod.pl \
	    -b "$(dist_DIR)/$(APP_PLATFORM)/rootfs/lib/modules/$$(cat $(kernelrelease))" \
	    -F $(linux_BUILDDIR)/System.map
	. $(PYVENVDIR)/bin/activate && \
	  python3 builder/elfstrip.py $(ELFSTRIP_VERBOSE) \
	      -l $(BUILDDIR)/elfstrip.log \
	      --strip=$(TOOLCHAIN_PATH)/bin/$(STRIP) \
		  --bound=$(dist_DIR)/$(APP_PLATFORM)/rootfs \
	      $(dist_DIR)/$(APP_PLATFORM)/rootfs
	$(MAKE) DESTDIR=$(dist_DIR)/$(APP_PLATFORM)/rootfs dist_rootfs_phase2

GENDIR+=$(dist_DIR)/$(APP_PLATFORM)/boot
GENDIR+=$(dist_DIR)/$(APP_PLATFORM)/rootfs/lib

dist-qemuarm64_phase3: | $(dist_DIR)/$(APP_PLATFORM)/boot
dist-qemuarm64_phase3: | $(dist_DIR)/$(APP_PLATFORM)/rootfs/lib
	$(call CMD_GENROOT_EXT4,$(dist_DIR)/$(APP_PLATFORM)/rootfs, \
	    $(dist_DIR)/$(APP_PLATFORM)/rootfs.bin)

dist-qemuarm64_locale:
	$(RMTREE) $(locale_BUILDDIR)*
	$(MAKE) locale_destdep_install
	$(MAKE) dist-qemuarm64_phase2

dist-qemuarm64:
	$(MAKE) dist-qemuarm64_phase1
	$(MAKE) dist-qemuarm64_phase2
	$(MAKE) dist-qemuarm64_phase3

dist_DTINCDIR+=$(linux_DIR)/scripts/dtc/include-prefixes
ifneq ($(strip $(filter bp,$(APP_PLATFORM))),)
dist_DTINCDIR+=$(linux_DIR)/arch/arm64/boot/dts/ti
endif

dist-bp_dtb: DESTDIR=$(dist_DIR)/$(APP_PLATFORM)/boot/dtb
dist-bp_dtb: DTBFILE=k3-am625-beagleplay.dtb
dist-bp_dtb:
	if [ -f "linux-$(APP_PLATFORM).dts" ]; then \
	  $(call CMD_CPPDTS) $(addprefix -I,$(dist_DTINCDIR)) \
	      -o $(BUILDDIR)/linux-$(APP_PLATFORM).dts linux-$(APP_PLATFORM).dts \
	  && $(call CMD_DTC2) $(addprefix -i,$(dist_DTINCDIR)) \
	      -o $(DESTDIR)/$(DTBFILE) $(BUILDDIR)/linux-$(APP_PLATFORM).dts; \
	else \
	  $(MAKE) linux_dtbs \
	    && rsync -L $(linux_BUILDDIR)/arch/arm64/boot/dts/ti/k3-am625-beagleplay.dtb \
		    $(DESTDIR)/$(DTBFILE); \
	fi
	dtc -I dtb -O dts $(DTC_LINUX_WNO) $(DESTDIR)/$(DTBFILE) \
	    > $(BUILDDIR)/$(DTBFILE:%.dtb=%).dts

dist-bp_phase1:
	$(MAKE) atf optee linux uboot $(kernelrelease)
	$(MAKE) linux_modules linux_dtbs
	$(MAKE) INSTALL_HDR_PATH=$(BUILD_SYSROOT) linux_headers_install
	$(RMTREE) $(BUILD_SYSROOT)/lib/modules
	$(MAKE) INSTALL_MOD_PATH=$(BUILD_SYSROOT) linux_modules_install
	$(MAKE) dist_rootfs_phase1

dist-bp_phase2: | $(dist_DIR)/$(APP_PLATFORM)/boot/boot/dtb
dist-bp_phase2: | $(dist_DIR)/$(APP_PLATFORM)/rootfs/lib
	$(MAKE) DESTDIR=$(dist_DIR)/$(APP_PLATFORM)/boot ubootenv
	rsync -L $(RSYNC_VERBOSE) $(call uboot_BUILDDIR,bp-r5)/tiboot3-am62x-gp-evm.bin \
	    $(dist_DIR)/$(APP_PLATFORM)/boot/tiboot3.bin
	rsync -L $(RSYNC_VERBOSE) $(call uboot_BUILDDIR,bp-a53)/tispl.bin_unsigned \
	    $(dist_DIR)/$(APP_PLATFORM)/boot/tispl.bin
	rsync -L $(RSYNC_VERBOSE) $(call uboot_BUILDDIR,bp-a53)/u-boot.img_unsigned \
	    $(dist_DIR)/$(APP_PLATFORM)/boot/u-boot.img
	rsync -L $(RSYNC_VERBOSE) $(linux_BUILDDIR)/arch/arm64/boot/Image \
	    $(linux_BUILDDIR)/arch/arm64/boot/Image.gz \
	    $(dist_DIR)/$(APP_PLATFORM)/boot/boot/
	# rsync -L $(RSYNC_VERBOSE) $(linux_BUILDDIR)/arch/arm64/boot/dts/ti/k3-am625-beagleplay.dtb \
	#     $(dist_DIR)/$(APP_PLATFORM)/boot/boot/dtb/
	$(MAKE) DESTDIR=$(dist_DIR)/$(APP_PLATFORM)/boot/boot/dtb dist-bp_dtb
	rsync -a $(RSYNC_VERBOSE) $(BUILD_SYSROOT)/* \
	    $(dist_DIR)/$(APP_PLATFORM)/rootfs/
	$(RMTREE) $(dist_DIR)/$(APP_PLATFORM)/rootfs/include \
	    $(dist_DIR)/$(APP_PLATFORM)/rootfs/lib/*.a \
	    $(dist_DIR)/$(APP_PLATFORM)/rootfs/lib64/*.a
	$(busybox_DIR)/examples/depmod.pl \
	    -b "$(dist_DIR)/$(APP_PLATFORM)/rootfs/lib/modules/$$(cat $(kernelrelease))" \
	    -F $(linux_BUILDDIR)/System.map
	. $(PYVENVDIR)/bin/activate && \
	  python3 builder/elfstrip.py $(ELFSTRIP_VERBOSE) \
	      -l $(BUILDDIR)/elfstrip.log \
	      --strip=$(TOOLCHAIN_PATH)/bin/$(STRIP) \
		  --bound=$(dist_DIR)/$(APP_PLATFORM)/rootfs \
	      $(dist_DIR)/$(APP_PLATFORM)/rootfs
	$(MAKE) DESTDIR=$(dist_DIR)/$(APP_PLATFORM)/rootfs dist_rootfs_phase2

dist-bp_depmod:
	$(busybox_DIR)/examples/depmod.pl \
	    -b "$(dist_DIR)/$(APP_PLATFORM)/rootfs/lib/modules/$$(cat $(kernelrelease))" \
	    -F $(linux_BUILDDIR)/System.map

GENDIR+=$(dist_DIR)/$(APP_PLATFORM)/boot/boot/dtb
GENDIR+=$(dist_DIR)/$(APP_PLATFORM)/rootfs/lib

dist-bp_phase3: | $(dist_DIR)/$(APP_PLATFORM)/boot/boot/dtb
dist-bp_phase3: | $(dist_DIR)/$(APP_PLATFORM)/rootfs/lib

GENDIR+=$(dist_DIR)/$(APP_PLATFORM)/rootfs

dist-bp:
	$(MAKE) dist-bp_phase1
	$(MAKE) dist-bp_phase2
	$(MAKE) dist-bp_phase3

dist-bp_sd_phase1: | $(SD_BOOT)/dtb
	rsync -a $(RSYNC_VERBOSE) $(dist_DIR)/$(APP_PLATFORM)/boot/* $(SD_BOOT)/

GENDIR+=$(SD_BOOT)/dtb

dist-bp_sd_phase2: | $(SD_ROOTFS)
	rsync -a $(dist_DIR)/$(APP_PLATFORM)/rootfs/* $(SD_ROOTFS)/

dist-bp_sd:
	$(MAKE) dist-bp_sd_phase1
	$(MAKE) dist-bp_sd_phase2

#------------------------------------
#
dist:
	$(MAKE) dist-$(APP_PLATFORM)

dist_%:
	$(MAKE) dist-$(APP_PLATFORM)_$(@:dist_%=%)

memo_git:
	@for i in $(linux_DIR) $(ti-linux-fw_DIR) $(uboot_DIR) $(optee_DIR) \
	    $(atfa_DIR) $(busybox_DIR) \
	    ; do \
	  if [ -d "$${i}/.git" ]; then \
	    echo -n "$$(basename $$i): " && \
	    cd $$i && git rev-parse HEAD; \
	  else \
	    echo -n "$$(basename $$i): unknown upstream"; \
	  fi; \
	done

#------------------------------------
#
distclean:
	$(RMTREE) $(BUILDDIR) $(BUILDDIR2)

#------------------------------------
#
pyvenv $(PYVENVDIR):
	python3 -m venv $@
	. $(PYVENVDIR)/bin/activate \
	  && pip3 install $(sort $(GENPYVENV))

#------------------------------------
#
$(sort $(GENDIR)):
	$(MKDIR) $@

.PHONY: always
always:; # always build

#------------------------------------
#------------------------------------
#------------------------------------
#

