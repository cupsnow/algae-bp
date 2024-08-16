#------------------------------------
#
include builder/proj.mk
-include site.mk

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
export APP_ATTR?=$(APP_ATTR_$(APP_PLATFORM))

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

CPPFLAGS+=
CFLAGS+=
CXXFLAGS+=
LDFLAGS+=

GENDIR:=
GENPYVENV:=

# abstract from make $(1)=XXX -> XXX
CLIARGS_VAL=$(if $(filter x"command line",x"$(strip $(origin $(1)))"),$($(1)))

# abstract from make V=XXX -> XXX
CLIARGS_VERBOSE=$(call CLIARGS_VAL,V)

# abstract from make V=1 ... -> -v
# RSYNC_VERBOSE=$(if $(filter x"1", x"$(CLIARGS_VERBOSE)"),-v)

ifneq ($(strip $(filter x"1", x"$(CLIARGS_VERBOSE)")),)
RSYNC_VERBOSE=-v
CP_VERBOSE=-v
MV_VERBOSE=-v
endif


# abstract from make V=1 ... -> -v
CP_VERBOSE=$(if $(filter x"1", x"$(CLIARGS_VERBOSE)"),-v)

# abstract from make V=1 ... -> -v
MV_VERBOSE=$(if $(filter x"1", x"$(CLIARGS_VERBOSE)"),-v)

#------------------------------------
#
.DEFAULT_GOAL=help
help:
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

optee: | $(BUILDDIR)/pyvenv
	. $(BUILDDIR)/pyvenv/bin/activate \
	  && $(optee_MAKE) $(PARALLEL_BUILD)

optee_%:
	. $(BUILDDIR)/pyvenv/bin/activate \
	  && $(optee_MAKE) $(PARALLEL_BUILD) $(@:optee_%=%)

GENPYVENV+=pyelftools cryptography

#------------------------------------
# git clong -b ti-linux-firmware git://git.ti.com/processor-firmware/ti-linux-firmware.git
# 
ti-linux-fw_DIR?=$(PKGDIR2)/ti-linux-firmware

#------------------------------------
# apt install libssl-dev device-tree-compiler swig python3-distutils
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
    CROSS_COMPILE=$(ARM_CROSS_COMPILE)

uboot_defconfig-bp-r5=am62x_beagleplay_r5_defconfig

uboot_MAKEARGS-bp-a53+=BINMAN_INDIRS=$(ti-linux-fw_DIR) \
    BL31=$(firstword $(wildcard $(atf_BUILDDIR)/k3/lite/release/bl31.bin \
        $(atf_BUILDDIR)/k3/lite/debug/bl31.bin)) \
    TEE=$(optee_BUILDDIR)/core/tee-raw.bin CROSS_COMPILE=$(AARCH64_CROSS_COMPILE)

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
	$(MAKE) APP_PLATFORM=bp-a53 atf_BUILDDIR=$(atf_BUILDDIR) \
	    optee_BUILDDIR=$(optee_BUILDDIR) uboot

uboot_%:
	$(MAKE) APP_PLATFORM=bp-r5 uboot_$(@:uboot_%=%)
	$(MAKE) APP_PLATFORM=bp-a53 atf_BUILDDIR=$(atf_BUILDDIR) \
	    optee_BUILDDIR=$(optee_BUILDDIR) uboot_$(@:uboot_%=%)

else
# normal case

uboot_defconfig $(uboot_BUILDDIR)/.config: | $(uboot_BUILDDIR)
	if [ -f uboot-$(APP_PLATFORM).defconfig ]; then \
	  cp -v uboot-$(APP_PLATFORM).defconfig $(uboot_BUILDDIR)/.config && \
	  yes "" | $(uboot_MAKE) oldconfig; \
	else \
	  $(uboot_MAKE) $(uboot_defconfig-$(APP_PLATFORM)); \
	fi

$(addprefix uboot_,help):
	$(uboot_MAKE) $(PARALLEL_BUILD) $(@:uboot_%=%)

$(addprefix uboot_,htmldocs): | $(BUILDDIR)/pyvenv $(uboot_BUILDDIR)
	. $(BUILDDIR)/pyvenv/bin/activate \
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
ubootenv:
	$(call CMD_UENV)

uboot: | $(uboot_BUILDDIR)/.config $(BUILDDIR)/pyvenv
	. $(BUILDDIR)/pyvenv/bin/activate \
	  && $(uboot_MAKE) $(PARALLEL_BUILD)

uboot_%: | $(uboot_BUILDDIR)/.config $(BUILDDIR)/pyvenv
	. $(BUILDDIR)/pyvenv/bin/activate \
	  && $(uboot_MAKE) $(PARALLEL_BUILD) $(@:uboot_%=%)

GENPYVENV+=yamllint jsonschema

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
$(addprefix linux_,htmldocs): | $(BUILDDIR)/pyvenv $(linux_BUILDDIR)
	. $(BUILDDIR)/pyvenv/bin/activate \
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

$(addprefix busybox_,help doc html): | $(BUILDDIR)/pyvenv
	. $(BUILDDIR)/pyvenv/bin/activate && \
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
	    ln -sf lib$${i}w.a $(DESTDIR)/lib/lib$${i}.a; \
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

GENDIR += $(ncursesw_BUILDDIR)

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

GENDIR+=$(locale_BUILDDIR)

locale_destpkg $(locale_BUILDDIR)-destpkg.tar.xz:
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

dist_rootfs_phase3: DESTDIR=$(dist_DIR)/rootfs
dist_rootfs_phase3:
	for i in dev media proc root sys tmp var/run; do \
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
	ln -sf /var/run/udhcpc/resolv.conf $(DESTDIR)/etc/resolv.conf
	ln -sf /var/run/ld.so.cache $(DESTDIR)/etc/ld.so.cache
	rsync -L $(PROJDIR)/builder/devsync.sh $(DESTDIR)/root/
ifeq (1,1)
	$(MAKE) dummy1
	rsync -L $(BUILDDIR)/dummy1/tester_syslog $(DESTDIR)/root/
endif

dist_lfs_phase1: DESTDIR=$(dist_DIR)/lfs
dist_lfs_phase1:
	$(MAKE) DESTDIR=$(DESTDIR) busybox_destdep_install \
	    tmux_destdep_install

dist_lfs_phase2: DESTDIR=$(dist_DIR)/lfs
dist_lfs_phase2:
	$(MAKE) DESTDIR=$(DESTDIR) dist_rootfs_phase3
	$(RMTREE) $(dist_DIR)/lfs.bin
	truncate -s 512M $(dist_DIR)/lfs.bin
	mkfs.ext4 -d $(DESTDIR) $(dist_DIR)/lfs.bin

dist_lfs: DESTDIR=$(dist_DIR)/lfs
dist_lfs:
	$(MAKE) DESTDIR=$(DESTDIR) dist_lfs_phase1
	$(MAKE) DESTDIR=$(DESTDIR) dist_lfs_phase2

dist-qemuarm64_phase1:
	$(MAKE) uboot linux $(addsuffix _destdep_install, \
	    busybox tmux mmcutils)

dist-qemuarm64_phase2: | $(dist_DIR)/$(APP_PLATFORM)/boot
dist-qemuarm64_phase2: | $(dist_DIR)/$(APP_PLATFORM)/rootfs
	$(MAKE) DESTDIR=$(dist_DIR)/$(APP_PLATFORM)/boot ubootenv
	rsync -L $(RSYNC_VERBOSE) $(uboot_BUILDDIR)/u-boot.bin \
	    $(linux_BUILDDIR)/arch/arm64/boot/Image.gz \
	    $(linux_BUILDDIR)/arch/arm64/boot/Image \
	    $(linux_BUILDDIR)/vmlinux \
	    $(dist_DIR)/$(APP_PLATFORM)/boot/
	rsync -a $(RSYNC_VERBOSE) $(BUILD_SYSROOT)/* \
	    $(dist_DIR)/$(APP_PLATFORM)/rootfs/
	$(MAKE) DESTDIR=$(dist_DIR)/$(APP_PLATFORM)/rootfs dist_rootfs_phase3
	$(RMTREE) $(dist_DIR)/$(APP_PLATFORM)/rootfs.bin
	truncate -s 512M $(dist_DIR)/$(APP_PLATFORM)/rootfs.bin
	mkfs.ext4 -d $(dist_DIR)/$(APP_PLATFORM)/rootfs \
	    $(dist_DIR)/$(APP_PLATFORM)/rootfs.bin

GENDIR+=$(dist_DIR)/$(APP_PLATFORM)/boot
GENDIR+=$(dist_DIR)/$(APP_PLATFORM)/rootfs

dist-qemuarm64_locale:
	$(RMTREE) $(locale_BUILDDIR)*
	$(MAKE) locale_destdep_install
	$(MAKE) dist_phase2

dist-qemuarm64:
	$(MAKE) dist-qemuarm64_phase1
	$(MAKE) dist-qemuarm64_phase2

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
	$(MAKE) atf optee linux
	$(MAKE) uboot linux_modules linux_dtbs
	$(MAKE) INSTALL_HDR_PATH=$(BUILD_SYSROOT) linux_headers_install
	$(MAKE) busybox_destdep_install tmux_destdep_install

dist-bp_phase2: | $(dist_DIR)/$(APP_PLATFORM)/boot/boot/dtb
	rsync -L $(RSYNC_VERBOSE) $(call uboot_BUILDDIR,bp-r5)/tiboot3-am62x-gp-evm.bin \
	    $(dist_DIR)/$(APP_PLATFORM)/boot/tiboot3.bin
	rsync -L $(RSYNC_VERBOSE) $(call uboot_BUILDDIR,bp-a53)/tispl.bin_unsigned \
	    $(dist_DIR)/$(APP_PLATFORM)/boot/tispl.bin
	rsync -L $(RSYNC_VERBOSE) $(call uboot_BUILDDIR,bp-a53)/u-boot.img_unsigned \
	    $(dist_DIR)/$(APP_PLATFORM)/boot/u-boot.img
	$(MAKE) DESTDIR=$(dist_DIR)/$(APP_PLATFORM)/boot ubootenv
	rsync -L $(RSYNC_VERBOSE) $(linux_BUILDDIR)/arch/arm64/boot/Image \
	    $(linux_BUILDDIR)/arch/arm64/boot/Image.gz \
	    $(dist_DIR)/$(APP_PLATFORM)/boot/boot/
	# rsync -L $(RSYNC_VERBOSE) $(linux_BUILDDIR)/arch/arm64/boot/dts/ti/k3-am625-beagleplay.dtb \
	#     $(dist_DIR)/$(APP_PLATFORM)/boot/boot/dtb/
	$(MAKE) DESTDIR=$(dist_DIR)/$(APP_PLATFORM)/boot/boot/dtb dist-bp_dtb

GENDIR+=$(dist_DIR)/$(APP_PLATFORM)/boot/boot/dtb

dist-bp_phase3: | $(dist_DIR)/$(APP_PLATFORM)/rootfs
	$(RMTREE) $(BUILD_SYSROOT)/lib/modules
	echo ignored *** $(MAKE) INSTALL_MOD_PATH=$(BUILD_SYSROOT) linux_modules_install
	rsync -a $(RSYNC_VERBOSE) $(BUILD_SYSROOT)/* \
	    $(dist_DIR)/$(APP_PLATFORM)/rootfs/
	$(MAKE) DESTDIR=$(dist_DIR)/$(APP_PLATFORM)/rootfs dist_rootfs_phase3

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
dist_strip_known_sh_pattern=\.sh \.pl \.py c_rehash ncursesw6-config alsaconf \
    $(addprefix usr/bin/,xtrace tzselect ldd sotruss catchsegv mtrace) \.la
dist_strip_known_sh_pattern2=$(subst $(SPACE),|,$(sort $(subst $(COMMA),$(SPACE), \
    $(dist_strip_known_sh_pattern))))
dist_strip:
	@echo -e "$(ANSI_GREEN)Strip executable$(if $($(@)_log),$(COMMA) log to $($(@)_log))$(ANSI_NORMAL)"
	@$(if $($(@)_log),echo "" >> $($(@)_log); date >> $($(@)_log))
	@$(if $($(@)_log),echo "Start strip; path: $($(@)_DIR) $($(@)_EXTRA)" >> $($(@)_log))
	@for i in $(addprefix $($(@)_DIR), \
	  usr/lib/libgcc_s.so.1 usr/lib64/libgcc_s.so.1 \
	  bin sbin lib lib64 usr/bin usr/sbin usr/lib usr/lib64) $($(@)_EXTRA); do \
	  if [ ! -e "$$i" ]; then \
	    $(if $($(@)_log),echo "Strip skipping missing explicite $$i" >> $($(@)_log);) \
	    continue; \
	  fi; \
	  [ -f "$$i" ] && { \
	    $(if $($(@)_log),echo "Strip explicite $$i" >> $($(@)_log);) \
	    $(STRIP) -g $$i; \
	    continue; \
	  }; \
	  [ -d "$$i" ] && { \
	    $(if $($(@)_log),echo "Strip recurse dir $$i" >> $($(@)_log);) \
	    for j in `find $$i`; do \
	      [[ "$$j" =~ .+($(dist_strip_known_sh_pattern2)) ]] && { \
	        $(if $($(@)_log),echo "Skip known script/file $$j" >> $($(@)_log);) \
	        continue; \
		  }; \
	      [[ "$$j" =~ .*/lib/modules/.+\.ko ]] && { \
	        $(if $($(@)_log),echo "Strip implicite kernel module $$j" >> $($(@)_log);) \
	        $(STRIP) -g $$j; \
	        continue; \
	      }; \
	      [ ! -x "$$j" ] && { \
	        $(if $($(@)_log),echo "Strip skipping non-executable $$j" >> $($(@)_log);) \
	        continue; \
	      }; \
	      [ -L "$$j" ] && { \
	        $(if $($(@)_log),echo "Strip skipping symbolic $$j -> `readlink $$j`" >> $($(@)_log);) \
	        continue; \
	      }; \
	      [ -d "$$j" ] && { \
	        $(if $($(@)_log),echo "Strip skipping dirname $$j" >> $($(@)_log);) \
	        continue; \
	      }; \
	      $(if $($(@)_log),echo "Strip implicite file $$j" >> $($(@)_log);) \
	      $(STRIP) -g $$j; \
	    done; \
	  }; \
	done

# ~/07_sw/arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-readelf -d destdir/sdcard/rootfs/lib/libavcodec.so.58.134.100 | sed -nE "s/.*\(NEEDED\)\s+Shared library:\s*\[(.*)\]/\1/p"
# ~/07_sw/arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-objdump -p destdir/rootfs_least/bin/busybox | sed -nE "s/^\s*NEEDED\s+(.*)/\1/p"
dist_elfdep: elfdep_log=$(BUILDDIR)/elfdep_log-$(APP_PLATFORM).txt
dist_elfdep:
	@echo -e "$(ANSI_GREEN)ELF dep dump$(ANSI_NORMAL)"
	@echo "# `date`" $(if $(elfdep_log),&>> $(elfdep_log))
	for i in $(addprefix $(dist_DIR)/rootfs/, \
	    usr/lib/libgcc_s.so.1 usr/lib64/libgcc_s.so.1 \
	    bin sbin lib lib64 usr/bin usr/sbin usr/lib usr/lib64); do \
	  if [ ! -e "$$i" ]; then \
	    echo "# Skipping missing explicite $$i" $(if $(elfdep_log),&>> $(elfdep_log)); \
	    continue; \
	  fi; \
	  [ -f "$$i" ] && { \
	    echo "# ELF explicite $$i" $(if $(elfdep_log),&>> $(elfdep_log)); \
		$(call ELFDEP,"$$i") $(if $(elfdep_log),&>> $(elfdep_log)); \
	    continue; \
	  }; \
	  [ -d "$$i" ] && { \
	    echo "# Recurse dir $$i" $(if $(elfdep_log),&>> $(elfdep_log)); \
	    for j in `find $$i`; do \
	      [[ "$$j" =~ .+(\.sh|\.pl|\.py|c_rehash|ncursesw6-config|alsaconf) ]] && { \
	        echo "# Skip known script/file $$j" $(if $(elfdep_log),&>> $(elfdep_log)); \
	        continue; \
	      }; \
	      [[ "$$j" =~ .*/lib/modules/.+\.ko ]] && { \
	        echo "# ELF implicite kernel module $$j" $(if $(elfdep_log),&>> $(elfdep_log)); \
	        $(call ELFDEP,"$$j") $(if $(elfdep_log),&>> $(elfdep_log)); \
	        continue; \
	      }; \
	      [ ! -x "$$j" ] && { \
	        echo "# Skipping non-executable $$j" $(if $(elfdep_log),&>> $(elfdep_log)); \
	        continue; \
	      }; \
	      [ -L "$$j" ] && { \
	        echo "# Skipping symbolic $$j" $(if $(elfdep_log),&>> $(elfdep_log)); \
	        continue; \
	      }; \
	      [ -d "$$j" ] && { \
	        echo "# Skipping dirname $$j" $(if $(elfdep_log),&>> $(elfdep_log)); \
	        continue; \
	      }; \
	      echo "# ELF implicite file $$j" $(if $(elfdep_log),&>> $(elfdep_log)); \
	      $(call ELFDEP,"$$j") $(if $(elfdep_log),&>> $(elfdep_log)); \
	    done; \
	  }; \
	done
	echo "" $(if $(elfdep_log),&>> $(elfdep_log))
	echo "# Sorted result" $(if $(elfdep_log),&>> $(elfdep_log))
	$(if $(elfdep_log), cat "$(elfdep_log)" | "grep" -v -e "^\s*#" -e "^\s*$$" \
	  | sort | uniq &>> $(elfdep_log))

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
$(BUILDDIR)/pyvenv:
	python3 -m venv $@
	. $(BUILDDIR)/pyvenv/bin/activate \
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

