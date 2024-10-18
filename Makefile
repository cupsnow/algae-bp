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

# 0 remove .pc and .la after build
# 1 remove .la after build
BUILD_PKGCFG_USAGE=2
BUILD_PKGCFG_ENV+=PKG_CONFIG_LIBDIR="$(or $(1),$(BUILD_SYSROOT))/lib/pkgconfig:$(or $(1),$(BUILD_SYSROOT))/share/pkgconfig" \
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

RSYNC_VERBOSE+=--debug=FILTER

ifneq ($(strip $(filter x"1", x"$(CLIARGS_VERBOSE)")),)
RSYNC_VERBOSE+=-v
CP_VERBOSE+=-v
MV_VERBOSE+=-v
ELFSTRIP_VERBOSE+=-v
endif

#------------------------------------
#
define DEF_DESTDEP
$(1)_destpkg $$($(1)_BUILDDIR)-destpkg.tar.xz:
	$$(RMTREE) $$($(1)_BUILDDIR)-destpkg
	$$(MAKE) DESTDIR=$$($(1)_BUILDDIR)-destpkg $(1)_install
	tar -Jcvf $$($(1)_BUILDDIR)-destpkg.tar.xz \
	    -C $$(dir $$($(1)_BUILDDIR)-destpkg) \
	    $$(notdir $$($(1)_BUILDDIR)-destpkg)
	$$(RMTREE) $$($(1)_BUILDDIR)-destpkg

$(1)_destpkg_install: DESTDIR=$$(BUILD_SYSROOT)
$(1)_destpkg_install: | $$($(1)_BUILDDIR)-destpkg.tar.xz
	[ -d "$$(DESTDIR)" ] || $$(MKDIR) $$(DESTDIR)
	tar -Jxvf $$($(1)_BUILDDIR)-destpkg.tar.xz --strip-components=1 \
	    -C $$(DESTDIR)

$(1)_destdep_install: $$(foreach iter,$$($(1)_DEP),$$(iter)_destdep_install)
	$$(MAKE) $(1)_destpkg_install
# end of DEF_DEPINSTALL for $(1)
endef

#------------------------------------
#
.DEFAULT_GOAL=help
help: help1

help1:
	@echo "APP_ATTR: $(APP_ATTR)"
	@echo "AARCH64 build target: $$($(AARCH64_CROSS_COMPILE)gcc -dumpmachine)"
	@echo "ARM build target: $$($(ARM_CROSS_COMPILE)gcc -dumpmachine)"
	@echo "TOOLCHAIN_SYSROOT: $(TOOLCHAIN_SYSROOT)"
	$(call CMD_RM_EMPTYDIR,abc def)

meson_aarch64 $(BUILDDIR)/meson-aarch64.ini: | $(PROJDIR)/builder/meson-aarch64.ini
	rsync -a $(RSYNC_VERBOSE) $(PROJDIR)/builder/meson-aarch64.ini \
	    $(BUILDDIR)/meson-aarch64.ini
	sed -i "s|\$${BUILD_SYSROOT}|$(BUILD_SYSROOT)|" $(BUILDDIR)/meson-aarch64.ini
	sed -i "s|\$${AARCH64_CROSS_COMPILE}|$(AARCH64_CROSS_COMPILE)|" $(BUILDDIR)/meson-aarch64.ini

CMD_DEPSHOW_RULE=echo "$(1): $(2)";
CMD_DEPSHOW_DOT=$(foreach iter,$(2),echo "  $(iter) -> $(1)";)
CMD_DEPSHOW=$(if $($(1)_DEP), \
  $(foreach iter,$($(1)_DEP),$(call CMD_DEPSHOW,$(iter),$(2))) \
  $(call $(or $(2),CMD_DEPSHOW_RULE),$(1),$($(1)_DEP)), \
  $(call $(or $(2),CMD_DEPSHOW_RULE),$(1)))

depshow:
	@echo "USAGE: make [-s] [$(@)_<PKG> | depdot_<PKG>]"

depshow_%:
	@$(call CMD_DEPSHOW,$(@:depshow_%=%))

# DEPDOT_NAME=$(subst $(SPACE),,$(firstword $(1)) $(words $(1)))
DEPDOT_NAME=$(firstword $(1))$(words $(1))

# depdot: DEPDOT_PKGS=glib mtdutils
depdot: depdot_name=$(call DEPDOT_NAME,$(DEPDOT_PKGS))
depdot:
	@if [ -z "$(DEPDOT_PKGS)" ]; then \
	  echo "USAGE: make -s \"DEPDOT_PKGS=<PKGS>\" $@"; \
	  echo ""; \
	  false; \
	fi
	@echo "digraph $(depdot_name) {"
	@$(foreach iter,$(DEPDOT_PKGS),$(call CMD_DEPSHOW,$(iter),CMD_DEPSHOW_DOT))
	@echo "}"

depdotshow: depdot_name=$(call DEPDOT_NAME,$(DEPDOT_PKGS))
depdotshow:
	@if [ -z "$(DEPDOT_PKGS)" ]; then \
	  echo "USAGE: make -s \"DEPDOT_PKGS=<PKGS>\" $@"; \
	  echo ""; \
	  false; \
	fi
	$(MAKE) -s DEPDOT_PKGS="$(DEPDOT_PKGS)" depdot >$(BUILDDIR)/dep-$(depdot_name).dot
	dot -Tsvg $(BUILDDIR)/dep-$(depdot_name).dot >$(BUILDDIR)/dep-$(depdot_name).svg
	xdg-open $(BUILDDIR)/dep-$(depdot_name).svg

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

uboot_MAKEARGS-bp-a53-emmc=$(uboot_MAKEARGS-bp-a53)

uboot_defconfig-bp-a53=am62x_beagleplay_a53_defconfig

uboot_defconfig-bp-a53-emmc=$(uboot_defconfig-bp-a53)

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
	$(MAKE) APP_PLATFORM=bp-a53-emmc atf_BUILDDIR=$(atf_BUILDDIR) \
	    optee_BUILDDIR=$(optee_BUILDDIR) REPO_DEFCONFIG=1 \
	    REPO_DEFCONFIG_PATCH="$(wildcard $(PROJDIR)/uboot-bp-a53-defconfig*.patch)" \
	    uboot
	$(MAKE) APP_PLATFORM=bp-a53 atf_BUILDDIR=$(atf_BUILDDIR) \
	    optee_BUILDDIR=$(optee_BUILDDIR) REPO_DEFCONFIG=1 \
	    REPO_DEFCONFIG_PATCH="$(wildcard $(PROJDIR)/uboot-bp-a53-defconfig*.patch)" \
	    uboot

uboot_%:
	$(MAKE) APP_PLATFORM=bp-r5 uboot_$(@:uboot_%=%)
	$(MAKE) APP_PLATFORM=bp-a53-emmc atf_BUILDDIR=$(atf_BUILDDIR) \
	    optee_BUILDDIR=$(optee_BUILDDIR) REPO_DEFCONFIG=1 \
	    REPO_DEFCONFIG_PATCH="$(wildcard $(PROJDIR)/uboot-bp-a53-emmc-defconfig*.patch)" \
	    uboot_$(@:uboot_%=%)
	$(MAKE) APP_PLATFORM=bp-a53 atf_BUILDDIR=$(atf_BUILDDIR) \
	    optee_BUILDDIR=$(optee_BUILDDIR) REPO_DEFCONFIG=1 \
	    REPO_DEFCONFIG_PATCH="$(wildcard $(PROJDIR)/uboot-bp-a53-defconfig*.patch)" \
	    uboot_$(@:uboot_%=%)

else
# normal case

uboot_defconfig $(uboot_BUILDDIR)/.config: | $(uboot_BUILDDIR)
	if [ "$(REPO_DEFCONFIG)" != "1" ] && [ -f uboot-$(APP_PLATFORM).defconfig ]; then \
	  cp -v uboot-$(APP_PLATFORM).defconfig $(uboot_BUILDDIR)/.config \
	    && ( yes "" | $(uboot_MAKE) oldconfig ); \
	else \
	  $(uboot_MAKE) $(uboot_defconfig-$(APP_PLATFORM)); \
	  cd $(uboot_BUILDDIR) \
	    && for i in $$($(call CMD_SORT_WS_SEP,$(REPO_DEFCONFIG_PATCH))); do \
	      patch -p1 --verbose <$${i}; \
	    done; \
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

GENDIR+=$(busybox_BUILDDIR)

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

busybox_install: DESTDIR=$(BUILD_SYSROOT)
busybox_install: | $(busybox_BUILDDIR)/.config
	$(busybox_MAKE) CONFIG_PREFIX=$(DESTDIR) install

$(eval $(call DEF_DESTDEP,busybox))

busybox: | $(busybox_BUILDDIR)/.config
	$(busybox_MAKE) $(PARALLEL_BUILD)

busybox_%: $(busybox_BUILDDIR)/.config
	$(busybox_MAKE) $(PARALLEL_BUILD) $(@:busybox_%=%)

#------------------------------------
#
attr_DIR=$(PKGDIR2)/attr
attr_BUILDDIR=$(BUILDDIR2)/attr-$(APP_BUILD)
attr_MAKE=$(MAKE) -C $(attr_BUILDDIR)

$(attr_DIR)/configure: | $(attr_DIR)/autogen.sh
	cd $(attr_DIR) \
	  && ./autogen.sh

GENDIR+=$(attr_BUILDDIR)

attr_defconfig $(attr_BUILDDIR)/Makefile: | $(attr_DIR)/configure $(attr_BUILDDIR)
	cd $(attr_BUILDDIR) \
	  && $(attr_DIR)/configure \
	  --host=`$(CC) -dumpmachine` --prefix=

attr_install: DESTDIR=$(BUILD_SYSROOT)
attr_install: | $(attr_BUILDDIR)/Makefile
	$(attr_MAKE) DESTDIR=$(DESTDIR) install

ifneq ($(strip $(filter 0 1,$(BUILD_PKGCFG_USAGE))),)
	$(call CMD_RM_FIND,.la,$(DESTDIR)/lib,libattr)
endif
ifneq ($(strip $(filter 0,$(BUILD_PKGCFG_USAGE))),)
	$(call CMD_RM_FIND,.pc,$(DESTDIR)/lib/pkgconfig,libattr)
endif
	$(call CMD_RM_EMPTYDIR,--ignore-fail-on-non-empty $(DESTDIR)/lib/pkgconfig)

$(eval $(call DEF_DESTDEP,attr))

attr: | $(attr_BUILDDIR)/Makefile
	$(attr_MAKE) $(PARALLEL_BUILD)

attr_%: | $(attr_BUILDDIR)/Makefile
	$(attr_MAKE) $(PARALLEL_BUILD) $(@:attr_%=%)

#------------------------------------
#
acl_DEP=attr
acl_DIR=$(PKGDIR2)/acl
acl_BUILDDIR=$(BUILDDIR2)/acl-$(APP_BUILD)
acl_MAKE=$(MAKE) -C $(acl_BUILDDIR)

acl_INCDIR=$(BUILD_SYSROOT)/include
acl_LIBDIR=$(BUILD_SYSROOT)/lib $(BUILD_SYSROOT)/lib64

$(acl_DIR)/configure: | $(acl_DIR)/autogen.sh
	cd $(acl_DIR) \
	  && ./autogen.sh

GENDIR+=$(acl_BUILDDIR)

acl_defconfig $(acl_BUILDDIR)/Makefile: | $(acl_DIR)/configure $(acl_BUILDDIR)
	cd $(acl_BUILDDIR) \
	  && $(acl_DIR)/configure \
	      --host=`$(CC) -dumpmachine` --prefix= \
	      CPPFLAGS="$(addprefix -I,$(acl_INCDIR))" \
	      LDFLAGS="$(addprefix -L,$(acl_LIBDIR))" \
	      $(acl_ACARGS_$(APP_PLATFORM))

acl_install: DESTDIR=$(BUILD_SYSROOT)
acl_install: | $(acl_BUILDDIR)/Makefile
	$(acl_MAKE) DESTDIR=$(DESTDIR) install
ifneq ($(strip $(filter 0 1,$(BUILD_PKGCFG_USAGE))),)
	$(call CMD_RM_FIND,.la,$(DESTDIR)/lib,libacl)
endif
ifneq ($(strip $(filter 0,$(BUILD_PKGCFG_USAGE))),)
	$(call CMD_RM_FIND,.pc,$(DESTDIR)/lib/pkgconfig,libacl)
endif
	$(call CMD_RM_EMPTYDIR,$(DESTDIR)/lib/pkgconfig)

$(eval $(call DEF_DESTDEP,acl))

acl: | $(acl_BUILDDIR)/Makefile
	$(acl_MAKE) $(PARALLEL_BUILD)

acl_%: | $(acl_BUILDDIR)/Makefile
	$(acl_MAKE) $(PARALLEL_BUILD) $(@:acl_%=%)

#------------------------------------
# apply utilinux libuuid, libblkid
#
e2fsprogs_DEP=utilinux
e2fsprogs_DIR=$(PKGDIR2)/e2fsprogs
e2fsprogs_BUILDDIR=$(BUILDDIR2)/e2fsprogs-$(APP_BUILD)
e2fsprogs_MAKE=$(MAKE) -C $(e2fsprogs_BUILDDIR)

e2fsprogs_INCDIR+=$(BUILD_SYSROOT)/include
e2fsprogs_LIBDIR+=$(BUILD_SYSROOT)/lib $(BUILD_SYSROOT)/lib64

ifneq ($(strip $(filter utilinux,$(e2fsprogs_DEP))),)
e2fsprogs_LIBS+=blkid
e2fsprogs_ACARGS_$(APP_PLATFORM)+=--disable-libuuid --disable-libblkid
endif

GENDIR+=$(e2fsprogs_BUILDDIR)

e2fsprogs_defconfig $(e2fsprogs_BUILDDIR)/Makefile: | $(e2fsprogs_BUILDDIR)
	cd $(e2fsprogs_BUILDDIR) \
	  && $(BUILD_PKGCFG_ENV) $(e2fsprogs_DIR)/configure \
	      --host=`$(CC) -dumpmachine` --prefix= --enable-elf-shlibs \
	      CPPFLAGS="$(addprefix -I,$(e2fsprogs_INCDIR))" \
	      LDFLAGS="$(addprefix -L,$(e2fsprogs_LIBDIR)) $(addprefix -l,$(e2fsprogs_LIBS))" \
	      $(e2fsprogs_ACARGS_$(APP_PLATFORM))


e2fsprogs_install: DESTDIR=$(BUILD_SYSROOT)
e2fsprogs_install: | $(e2fsprogs_BUILDDIR)/scrub/e2scrub.conf
	$(e2fsprogs_MAKE) DESTDIR=$(DESTDIR) install
ifneq ($(strip $(filter 0,$(BUILD_PKGCFG_USAGE))),)
	$(call CMD_RM_FIND,.pc,$(DESTDIR)/lib/pkgconfig, \
	    blkid com_err e2p ext2fs ss uuid)
endif
	$(call CMD_RM_EMPTYDIR,$(DESTDIR)/lib/pkgconfig)

$(eval $(call DEF_DESTDEP,e2fsprogs))

$(e2fsprogs_BUILDDIR)/scrub/e2scrub.conf: e2fsprogs

e2fsprogs: | $(e2fsprogs_BUILDDIR)/Makefile
	$(e2fsprogs_MAKE) $(PARALLEL_BUILD)

e2fsprogs_%: | $(e2fsprogs_BUILDDIR)/Makefile
	$(e2fsprogs_MAKE) $(PARALLEL_BUILD) $(@:e2fsprogs_%=%)

#------------------------------------
#
mmcutils_DIR=$(PKGDIR2)/mmc-utils
mmcutils_BUILDDIR=$(BUILDDIR2)/mmcutils-$(APP_BUILD)
mmcutils_MAKE=$(MAKE) CC=$(CC) C= -C $(mmcutils_BUILDDIR)

GENDIR+=$(mmcutils_BUILDDIR)

mmcutils_defconfig $(mmcutils_BUILDDIR)/Makefile: | $(mmcutils_BUILDDIR)
	rsync -a $(RSYNC_VERBOSE) $(mmcutils_DIR)/* $(mmcutils_BUILDDIR)/

mmcutils_install: DESTDIR=$(BUILD_SYSROOT)
mmcutils_install: | $(mmcutils_BUILDDIR)/Makefile
	$(mmcutils_MAKE) DESTDIR=$(DESTDIR) prefix= install

$(eval $(call DEF_DESTDEP,mmcutils))

mmcutils: | $(mmcutils_BUILDDIR)/Makefile
	$(mmcutils_MAKE) $(PARALLEL_BUILD)

mmcutils_%: | $(mmcutils_BUILDDIR)/Makefile
	$(mmcutils_MAKE) $(PARALLEL_BUILD) $(@:mmcutils_%=%)

#------------------------------------
#
zlib_DIR=$(PKGDIR2)/zlib
zlib_BUILDDIR?=$(BUILDDIR2)/zlib-$(APP_BUILD)
zlib_MAKE=$(MAKE) DESTDIR=$(DESTDIR) -C $(zlib_BUILDDIR)

GENDIR+=$(zlib_BUILDDIR)

zlib_defconfig $(zlib_BUILDDIR)/configure.log: | $(zlib_BUILDDIR)
	cd $(zlib_BUILDDIR) \
	  && prefix= CROSS_PREFIX=$(CROSS_COMPILE) \
	      CFLAGS="$(zlib_CFLAGS_$(APP_PLATFORM))" \
	      $(zlib_DIR)/configure $(zlib_ACARGS_$(APP_PLATFORM))

zlib_install: DESTDIR=$(BUILD_SYSROOT)
zlib_install: | $(zlib_BUILDDIR)/configure.log
	$(zlib_MAKE) DESTDIR=$(DESTDIR) install
ifneq ($(strip $(filter 0,$(BUILD_PKGCFG_USAGE))),)
	$(call CMD_RM_FIND,.pc,$(DESTDIR)/lib/pkgconfig,zlib)
endif
	$(call CMD_RM_EMPTYDIR,$(DESTDIR)/lib/pkgconfig)

$(eval $(call DEF_DESTDEP,zlib))

zlib_distclean:
	$(RM) $(zlib_BUILDDIR)

zlib: | $(zlib_BUILDDIR)/configure.log
	$(zlib_MAKE) $(PARALLEL_BUILD)

zlib_%: | $(zlib_BUILDDIR)/configure.log
	$(zlib_MAKE) $(PARALLEL_BUILD) $(patsubst _%,%,$(@:zlib%=%))

#------------------------------------
#
lzo_DIR=$(PKGDIR2)/lzo
lzo_BUILDDIR=$(BUILDDIR2)/lzo-$(APP_BUILD)
lzo_MAKE=$(MAKE) -C $(lzo_BUILDDIR)

GENDIR+=$(lzo_BUILDDIR)

lzo_defconfig $(lzo_BUILDDIR)/Makefile: | $(lzo_BUILDDIR)
	cd $(lzo_BUILDDIR) \
	  && $(BUILD_PKGCFG_ENV) $(lzo_DIR)/configure \
	      --host=`$(CC) -dumpmachine` --prefix= \
	      --enable-shared

lzo_install: DESTDIR=$(BUILD_SYSROOT)
lzo_install: | $(lzo_BUILDDIR)/Makefile
	$(lzo_MAKE) DESTDIR=$(DESTDIR) install
ifneq ($(strip $(filter 0 1,$(BUILD_PKGCFG_USAGE))),)
	$(call CMD_RM_FIND,.la,$(DESTDIR)/lib,liblzo2)
endif
ifneq ($(strip $(filter 0,$(BUILD_PKGCFG_USAGE))),)
	$(call CMD_RM_FIND,.pc,$(DESTDIR)/lib/pkgconfig,lzo2)
endif
	$(call CMD_RM_EMPTYDIR,$(DESTDIR)/lib/pkgconfig)

$(eval $(call DEF_DESTDEP,lzo))

lzo: | $(lzo_BUILDDIR)/Makefile
	$(lzo_MAKE) $(PARALLEL_BUILD)

lzo_%: | $(lzo_BUILDDIR)/Makefile
	$(lzo_MAKE) $(PARALLEL_BUILD) $(@:lzo_%=%)

#------------------------------------
# ubifs dep: e2fsprogs,lzo
# jfss2 dep: acl
# dep: zlib openssl
#
mtdutils_DEP=zlib acl lzo e2fsprogs openssl
mtdutils_DIR=$(PKGDIR2)/mtd-utils
mtdutils_BUILDDIR=$(BUILDDIR2)/mtdutils-$(APP_BUILD)
mtdutils_MAKE=$(MAKE) -C $(mtdutils_BUILDDIR)

mtdutils_INCDIR=$(BUILD_SYSROOT)/include
mtdutils_LIBDIR=$(BUILD_SYSROOT)/lib $(BUILD_SYSROOT)/lib64

$(mtdutils_DIR)/configure: | $(mtdutils_DIR)/autogen.sh
	cd $(mtdutils_DIR) \
	  && ./autogen.sh

GENDIR+=$(mtdutils_BUILDDIR)

mtdutils_defconfig $(mtdutils_BUILDDIR)/Makefile: | $(mtdutils_DIR)/configure $(mtdutils_BUILDDIR)
	cd $(mtdutils_BUILDDIR) \
	  && $(BUILD_PKGCFG_ENV) $(mtdutils_DIR)/configure \
	      --host=`$(CC) -dumpmachine` --prefix= \
		  --without-zstd --without-selinux \
	      CFLAGS="$(addprefix -I,$(mtdutils_INCDIR))" \
	      LDFLAGS="$(addprefix -L,$(mtdutils_LIBDIR))" \

mtdutils_install: DESTDIR=$(BUILD_SYSROOT)
mtdutils_install: | $(mtdutils_BUILDDIR)/Makefile
	$(mtdutils_MAKE) DESTDIR=$(DESTDIR) install

$(eval $(call DEF_DESTDEP,mtdutils))

mtdutils: | $(mtdutils_BUILDDIR)/Makefile
	$(mtdutils_MAKE) $(PARALLEL_BUILD)

mtdutils_%: | $(mtdutils_BUILDDIR)/Makefile
	$(mtdutils_MAKE) $(PARALLEL_BUILD) $(@:mtdutils_%=%)

#------------------------------------
#
ncursesw_DIR?=$(PKGDIR2)/ncurses
ncursesw_BUILDDIR?=$(BUILDDIR2)/ncursesw-$(APP_BUILD)
ncursesw_TINFODIR=/usr/share/terminfo
ncursesw_MAKE=$(MAKE) -C $(ncursesw_BUILDDIR)

# ncursesw_ACARGS_$(APP_PLATFORM)+=--without-debug
ncursesw_ACARGS_ub20+=--with-pkg-config=/lib
ncursesw_ACARGS_bp+=--disable-db-install --without-tests --without-manpages

GENDIR+=$(ncursesw_BUILDDIR)

# no strip to prevent not recoginize crosscompiled executable
ncursesw_defconfig $(ncursesw_BUILDDIR)/Makefile: | $(ncursesw_BUILDDIR)
	cd $(ncursesw_BUILDDIR) \
	  && $(BUILD_PKGCFG_ENV) $(ncursesw_DIR)/configure \
	      --host=`$(CC) -dumpmachine` --prefix= --with-termlib --with-ticlib \
	      --with-shared --enable-widec --disable-stripping \
	      --with-default-terminfo-dir=$(ncursesw_TINFODIR) \
	      CFLAGS="-fPIC $(ncursesw_CFLAGS_$(APP_PLATFORM))" \
	      $(ncursesw_ACARGS_$(APP_PLATFORM))

# remove wrong pc file for the crosscompiled lib
ncursesw_install: DESTDIR=$(BUILD_SYSROOT)
ncursesw_install: | $(ncursesw_BUILDDIR)/Makefile
	$(ncursesw_MAKE) $(PARALLEL_BUILD)
	$(ncursesw_MAKE) DESTDIR=$(DESTDIR) install
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

$(eval $(call DEF_DESTDEP,ncursesw))

ncursesw: | $(ncursesw_BUILDDIR)/Makefile
	$(ncursesw_MAKE) $(PARALLEL_BUILD)

ncursesw_%: | $(ncursesw_BUILDDIR)/Makefile
	$(ncursesw_MAKE) $(PARALLEL_BUILD) $(@:ncursesw_%=%)

# Create small terminfo refer to https://invisible-island.net/ncurses/ncurses.faq.html#big_terminfo
# refine to comma saperated list when use in tic

terminfo_BUILDDIR=$(BUILDDIR2)/terminfo-$(APP_BUILD)

TERMINFO_NAMES=$(subst $(SPACE),$(COMMA),$(sort $(subst $(COMMA),$(SPACE), \
    ansi ansi-m color_xterm,linux,pcansi-m,rxvt-basic,vt52,vt100 \
    vt102,vt220,xterm,tmux-256color,screen-256color,xterm-256color screen)))
TERMINFO_TIC=LD_LIBRARY_PATH=$(PROJDIR)/tool/lib \
    TERMINFO=$(PROJDIR)/tool/$(ncursesw_TINFODIR) \
	$(PROJDIR)/tool/bin/tic
CMD_TERMINFO= \
  { [ -d "$(or $(1),$(DESTDIR))/$(ncursesw_TINFODIR)" ] || \
    $(MKDIR) $(or $(1),$(DESTDIR))/$(ncursesw_TINFODIR); } \
  && $(TERMINFO_TIC) -s -r -I -x -e"$(TERMINFO_NAMES)" \
      $(ncursesw_DIR)/misc/terminfo.src > $(BUILDDIR)/terminfo.src \
  && $(TERMINFO_TIC) -s -o $(or $(1),$(DESTDIR))/$(ncursesw_TINFODIR) \
      $(BUILDDIR)/terminfo.src

terminfo_install: DESTDIR=$(BUILD_SYSROOT)
terminfo_install: | $(PROJDIR)/tool/bin/tic
	$(call CMD_TERMINFO)

$(eval $(call DEF_DESTDEP,terminfo))

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

GENDIR+=$(locale_BUILDDIR)

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
endif
	$(call CMD_LOCALE_COMPILE,POSIX,UTF-8,$(locale_BUILDDIR)/POSIX.UTF-8) || [ $$? -eq 1 ]
	$(call CMD_LOCALE_AR,$(DESTDIR),$(locale_BUILDDIR)/POSIX.UTF-8)
	$(call CMD_LOCALE_COMPILE,en_US,UTF-8,$(locale_BUILDDIR)/en_US.UTF-8) || [ $$? -eq 1 ]
	$(call CMD_LOCALE_AR,$(DESTDIR),$(locale_BUILDDIR)/en_US.UTF-8)
	$(call CMD_LOCALE_COMPILE,zh_TW,UTF-8,$(locale_BUILDDIR)/zh_TW.UTF-8) || [ $$? -eq 1 ]
	$(call CMD_LOCALE_AR,$(DESTDIR),$(locale_BUILDDIR)/zh_TW.UTF-8)
	$(call CMD_LOCALE_COMPILE,zh_TW,BIG5,$(locale_BUILDDIR)/zh_TW.BIG5) || [ $$? -eq 1 ]
	$(call CMD_LOCALE_AR,$(DESTDIR),$(locale_BUILDDIR)/zh_TW.BIG5)
	$(call CMD_CHARMAP_INST,$(DESTDIR),UTF-8)
	$(call CMD_CHARMAP_INST,$(DESTDIR),BIG5)
	$(call CMD_RM_EMPTYDIR,$(DESTDIR)/usr/share/i18n/charmaps)
	# @echo "Locale archived: $$($(call CMD_LOCALE_LIST,$(DESTDIR)) | xargs)"

$(eval $(call DEF_DESTDEP,locale))
# 	# @echo "Locale:"
# 	# @$(call CMD_LOCALE_LIST,$(DESTDIR))

#------------------------------------
# https://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.17.tar.gz
#
libiconv_DIR?=$(PKGDIR2)/libiconv
libiconv_BUILDDIR?=$(BUILDDIR2)/libiconv-$(APP_BUILD)
libiconv_MAKE=$(MAKE) -C $(libiconv_BUILDDIR)

# $(libiconv_DIR)/configure: | $(libiconv_DIR)/autogen.sh
# 	cd $(libiconv_DIR) \
# 	  && ./autogen.sh --skip-gnulib

GENDIR+=$(libiconv_BUILDDIR)

libiconv_defconfig $(libiconv_BUILDDIR)/Makefile: | $(libiconv_DIR)/configure $(libiconv_BUILDDIR)
	cd $(libiconv_BUILDDIR) \
	  && $(BUILD_PKGCFG_ENV) $(libiconv_DIR)/configure \
	      --host=`$(CC) -dumpmachine` --prefix= \
		  --enable-year2038 \
	      $(libiconv_ACARGS_$(APP_PLATFORM))

libiconv_install: DESTDIR=$(BUILD_SYSROOT)
libiconv_install: | $(libiconv_BUILDDIR)/Makefile
	$(libiconv_MAKE) DESTDIR=$(DESTDIR) install
ifneq ($(strip $(filter 0 1,$(BUILD_PKGCFG_USAGE))),)
	$(call CMD_RM_FIND,.la,$(DESTDIR)/lib, \
	    libasprintf liblibiconvlib liblibiconvpo liblibiconvsrc libtextstyle)
endif

$(eval $(call DEF_DESTDEP,libiconv))

libiconv: | $(libiconv_BUILDDIR)/Makefile
	$(libiconv_MAKE) $(PARALLEL_BUILD)

libiconv_%: | $(libiconv_BUILDDIR)/Makefile
	$(libiconv_MAKE) $(PARALLEL_BUILD) $(@:libiconv_%=%)

#------------------------------------
# https://ftp.gnu.org/pub/gnu/gettext/gettext-0.22.5.tar.gz
#
gettext_DIR?=$(PKGDIR2)/gettext
gettext_BUILDDIR?=$(BUILDDIR2)/gettext-$(APP_BUILD)
gettext_MAKE=$(MAKE) -C $(gettext_BUILDDIR)
gettext_ACARGS_$(APP_PLATFORM)=$(libiconv_DESTDIR:%=--with-libiconv-prefix=%)

# $(gettext_DIR)/configure: | $(gettext_DIR)/autogen.sh
# 	cd $(gettext_DIR) \
# 	  && ./autogen.sh --skip-gnulib

GENDIR+=$(gettext_BUILDDIR)

gettext_defconfig $(gettext_BUILDDIR)/Makefile: | $(gettext_DIR)/configure $(gettext_BUILDDIR)
	cd $(gettext_BUILDDIR) \
	  && $(BUILD_PKGCFG_ENV) $(gettext_DIR)/configure \
	      --host=`$(CC) -dumpmachine` --prefix= \
		  --enable-year2038 \
	      $(gettext_ACARGS_$(APP_PLATFORM))

gettext_install: DESTDIR=$(BUILD_SYSROOT)
gettext_install: | $(gettext_BUILDDIR)/gettext-tools/src/.libs/xgettext
	$(gettext_MAKE) DESTDIR=$(DESTDIR) install
ifneq ($(strip $(filter 0 1,$(BUILD_PKGCFG_USAGE))),)
	$(call CMD_RM_FIND,.la,$(DESTDIR)/lib, \
	    libasprintf libgettextlib libgettextpo libgettextsrc libtextstyle)
endif

$(eval $(call DEF_DESTDEP,gettext))

$(gettext_BUILDDIR)/gettext-tools/src/.libs/xgettext: gettext

gettext: | $(gettext_BUILDDIR)/Makefile
# DESTDIR is used while building gettext
	DESTDIR= $(gettext_MAKE) DESTDIR= $(PARALLEL_BUILD)

gettext_%: | $(gettext_BUILDDIR)/Makefile
	$(gettext_MAKE) $(PARALLEL_BUILD) $(@:gettext_%=%)

#------------------------------------
#
iconvgettext_BUILDDIR=$(BUILDDIR2)/iconvgettext-$(APP_BUILD)

iconvgettext_install: DESTDIR=$(BUILD_SYSROOT)
iconvgettext_install:
	# $(RMTREE) $(gettext_BUILDDIR) $(libiconv_BUILDDIR)
	$(MAKE) DESTDIR=$(DESTDIR) libiconv_install
	$(MAKE) libiconv_DESTDIR=$(DESTDIR) DESTDIR=$(DESTDIR) gettext_install
	# $(RMTREE) $(libiconv_BUILDDIR)
	# $(MAKE) DESTDIR=$(DESTDIR) libiconv_install

$(eval $(call DEF_DESTDEP,iconvgettext))

#------------------------------------
#
pcre2_DIR?=$(PKGDIR2)/pcre2
pcre2_BUILDDIR?=$(BUILDDIR2)/pcre2-$(APP_BUILD)
pcre2_MAKE=$(MAKE) -C $(pcre2_BUILDDIR)

$(pcre2_DIR)/configure: | $(pcre2_DIR)/autogen.sh
	cd $(pcre2_DIR) \
	  && ./autogen.sh

GENDIR+=$(pcre2_BUILDDIR)

pcre2_defconfig $(pcre2_BUILDDIR)/Makefile: | $(pcre2_DIR)/configure $(pcre2_BUILDDIR)
	cd $(pcre2_BUILDDIR) \
	  && $(BUILD_PKGCFG_ENV) $(pcre2_DIR)/configure \
	      --host=`$(CC) -dumpmachine` --prefix= \
	      $(pcre2_ACARGS_$(APP_PLATFORM))

pcre2_install: DESTDIR=$(BUILD_SYSROOT)
pcre2_install: | $(pcre2_BUILDDIR)/Makefile
	$(pcre2_MAKE) DESTDIR=$(DESTDIR) install
ifneq ($(strip $(filter 0 1,$(BUILD_PKGCFG_USAGE))),)
	$(call CMD_RM_FIND,.la,$(DESTDIR)/lib,libpcre2-8 libpcre2-posix)
endif
ifneq ($(strip $(filter 0,$(BUILD_PKGCFG_USAGE))),)
	$(call CMD_RM_FIND,.pc,$(DESTDIR)/lib/pkgconfig,libpcre2-8 libpcre2-posix)
endif
	$(call CMD_RM_EMPTYDIR,$(DESTDIR)/lib/pkgconfig)

$(eval $(call DEF_DESTDEP,pcre2))

pcre2: | $(pcre2_BUILDDIR)/Makefile
	$(pcre2_MAKE) $(PARALLEL_BUILD)

pcre2_%: | $(pcre2_BUILDDIR)/Makefile
	$(pcre2_MAKE) $(PARALLEL_BUILD) $(@:pcre2_%=%)

#------------------------------------
#
libffi_DIR?=$(PKGDIR2)/libffi
libffi_BUILDDIR?=$(BUILDDIR2)/libffi-$(APP_BUILD)
libffi_MAKE=$(MAKE) -C $(libffi_BUILDDIR)

$(libffi_DIR)/configure: | $(libffi_DIR)/autogen.sh
	cd $(libffi_DIR) \
	  && ./autogen.sh

GENDIR+=$(libffi_BUILDDIR)

libffi_defconfig $(libffi_BUILDDIR)/Makefile: | $(libffi_DIR)/configure $(libffi_BUILDDIR)
	cd $(libffi_BUILDDIR) \
	  && $(BUILD_PKGCFG_ENV) $(libffi_DIR)/configure \
	      --host=`$(CC) -dumpmachine` --prefix= \
	      $(libffi_ACARGS_$(APP_PLATFORM))

libffi_install: DESTDIR=$(BUILD_SYSROOT)
libffi_install: | $(libffi_BUILDDIR)/Makefile
	$(libffi_MAKE) DESTDIR=$(DESTDIR) install
ifneq ($(strip $(filter 0 1,$(BUILD_PKGCFG_USAGE))),)
	$(call CMD_RM_FIND,.la,$(DESTDIR)/lib64,libffi)
endif
ifneq ($(strip $(filter 0,$(BUILD_PKGCFG_USAGE))),)
	$(call CMD_RM_FIND,.pc,$(DESTDIR)/lib/pkgconfig,libffi)
endif
	$(call CMD_RM_EMPTYDIR,$(DESTDIR)/lib/pkgconfig)

$(eval $(call DEF_DESTDEP,libffi))

libffi: | $(libffi_BUILDDIR)/Makefile
	$(libffi_MAKE) $(PARALLEL_BUILD)

libffi_%: | $(libffi_BUILDDIR)/Makefile
	$(libffi_MAKE) $(PARALLEL_BUILD) $(@:libffi_%=%)

#------------------------------------
#
libevent_DIR?=$(PKGDIR2)/libevent
libevent_BUILDDIR?=$(BUILDDIR2)/libevent-$(APP_BUILD)
libevent_MAKE=$(MAKE) -C $(libevent_BUILDDIR)

$(libevent_DIR)/configure: | $(libevent_DIR)/autogen.sh
	cd $(libevent_DIR) \
	  && ./autogen.sh

GENDIR+=$(libevent_BUILDDIR)

libevent_defconfig $(libevent_BUILDDIR)/Makefile: | $(libevent_DIR)/configure $(libevent_BUILDDIR)
	cd $(libevent_BUILDDIR) \
	  && $(BUILD_PKGCFG_ENV) $(libevent_DIR)/configure \
	      --host=`$(CC) -dumpmachine` --prefix= --disable-openssl \
		  --disable-mbedtls --with-pic \
	      $(libevent_ACARGS_$(APP_PLATFORM))

libevent_install: DESTDIR=$(BUILD_SYSROOT)
libevent_install: | $(libevent_BUILDDIR)/Makefile
	$(libevent_MAKE) DESTDIR=$(DESTDIR) install
ifneq ($(strip $(filter 0 1,$(BUILD_PKGCFG_USAGE))),)
	$(call CMD_RM_FIND,.la,$(DESTDIR)/lib, \
	    libevent_core libevent_extra libevent libevent_pthreads)
endif
ifneq ($(strip $(filter 0,$(BUILD_PKGCFG_USAGE))),)
	$(call CMD_RM_FIND,.pc,$(DESTDIR)/lib/pkgconfig, \
	    libevent_core libevent_extra libevent libevent_pthreads)
endif
	$(call CMD_RM_EMPTYDIR,$(DESTDIR)/lib/pkgconfig)

$(eval $(call DEF_DESTDEP,libevent))

libevent: | $(libevent_BUILDDIR)/Makefile
	$(libevent_MAKE) $(PARALLEL_BUILD)

libevent_%: | $(libevent_BUILDDIR)/Makefile
	$(libevent_MAKE) $(PARALLEL_BUILD) $(@:libevent_%=%)

#------------------------------------
# dep ncursesw libevent locale terminfo
#
tmux_DEP=ncursesw libevent locale terminfo
tmux_DIR=$(PKGDIR2)/tmux
tmux_BUILDDIR?=$(BUILDDIR2)/tmux-$(APP_BUILD)
tmux_MAKE=$(MAKE) -C $(tmux_BUILDDIR)

tmux_INCDIR=$(BUILD_SYSROOT)/include $(BUILD_SYSROOT)/include/ncursesw
tmux_LIBDIR=$(BUILD_SYSROOT)/lib $(BUILD_SYSROOT)/lib64

$(tmux_DIR)/configure:
	cd $(tmux_DIR) \
	  && ./autogen.sh

GENDIR+=$(tmux_BUILDDIR)

tmux_defconfig $(tmux_BUILDDIR)/Makefile: | $(tmux_DIR)/configure $(tmux_BUILDDIR)
	cd $(tmux_BUILDDIR) \
	  && $(BUILD_PKGCFG_ENV) $(tmux_DIR)/configure \
	      --host=`$(CC) -dumpmachine` --prefix= \
	      ac_cv_func_strtonum_working=no \
	      CPPFLAGS="$(addprefix -I,$(tmux_INCDIR))" \
	      LDFLAGS="$(addprefix -L,$(tmux_LIBDIR))" \
	      $(tmux_ACARGS_$(APP_PLATFORM))

tmux_install: DESTDIR=$(BUILD_SYSROOT)
tmux_install: | $(tmux_BUILDDIR)/Makefile
	$(tmux_MAKE) DESTDIR=$(DESTDIR) install

$(eval $(call DEF_DESTDEP,tmux))

tmux_distclean:
	$(RM) $(tmux_BUILDDIR)

tmux: | $(tmux_BUILDDIR)/Makefile
	$(tmux_MAKE) $(PARALLEL_BUILD)

tmux_%: | $(tmux_BUILDDIR)/Makefile
	$(tmux_MAKE) $(PARALLEL_BUILD) $(@:tmux_%=%)

#------------------------------------
# https://github.com/scop/bash-completion.git
#
bashcomp_DIR?=$(PKGDIR2)/bash-completion
bashcomp_BUILDDIR?=$(BUILDDIR2)/bashcomp-$(APP_BUILD)
bashcomp_MAKE=$(MAKE) -C $(bashcomp_BUILDDIR)

$(bashcomp_DIR)/configure: | $(bashcomp_DIR)/configure.ac
	cd $(bashcomp_DIR) \
	  && autoreconf -fiv

GENDIR+=$(bashcomp_BUILDDIR)

bashcomp_defconfig $(bashcomp_BUILDDIR)/Makefile: | $(bashcomp_DIR)/configure $(bashcomp_BUILDDIR)
	cd $(bashcomp_BUILDDIR) \
	  && $(BUILD_PKGCFG_ENV) $(bashcomp_DIR)/configure \
	      --host=`$(CC) -dumpmachine` --prefix= \
	      $(bashcomp_ACARGS_$(APP_PLATFORM))

bashcomp_install: DESTDIR=$(BUILD_SYSROOT)
bashcomp_install: | $(bashcomp_BUILDDIR)/Makefile
	$(bashcomp_MAKE) DESTDIR=$(DESTDIR) install

$(eval $(call DEF_DESTDEP,bashcomp))

bashcomp: | $(bashcomp_BUILDDIR)/Makefile
	$(bashcomp_MAKE) $(PARALLEL_BUILD)

bashcomp_%: | $(bashcomp_BUILDDIR)/Makefile
	$(bashcomp_MAKE) $(PARALLEL_BUILD) $(@:bashcomp_%=%)


#------------------------------------
#
bash_DIR?=$(PKGDIR2)/bash
bash_BUILDDIR?=$(BUILDDIR2)/bash-$(APP_BUILD)
bash_MAKE=$(MAKE) -C $(bash_BUILDDIR)

# $(bash_DIR)/configure: | $(bash_DIR)/autogen.sh
# 	cd $(bash_DIR) \
# 	  && ./autogen.sh

GENDIR+=$(bash_BUILDDIR)

bash_defconfig $(bash_BUILDDIR)/Makefile: | $(bash_DIR)/configure $(bash_BUILDDIR)
	cd $(bash_BUILDDIR) \
	  && $(BUILD_PKGCFG_ENV) $(bash_DIR)/configure \
	      --host=`$(CC) -dumpmachine` --prefix= \
	      $(bash_ACARGS_$(APP_PLATFORM))

bash_install: DESTDIR=$(BUILD_SYSROOT)
bash_install: | $(bash_BUILDDIR)/Makefile
	$(bash_MAKE) DESTDIR=$(DESTDIR) install

$(eval $(call DEF_DESTDEP,bash))

bash: | $(bash_BUILDDIR)/Makefile
	$(bash_MAKE) $(PARALLEL_BUILD)

bash_%: | $(bash_BUILDDIR)/Makefile
	$(bash_MAKE) $(PARALLEL_BUILD) $(@:bash_%=%)

#------------------------------------
# openssl-3.3
#
openssl_DEP=zlib
openssl_DIR=$(PKGDIR2)/openssl
openssl_BUILDDIR?=$(BUILDDIR2)/openssl-$(APP_BUILD)
openssl_MAKE=$(MAKE) DESTDIR=$(DESTDIR) -C $(openssl_BUILDDIR)

openssl_ACARGS_ub20+=linux-x86_64
openssl_ACARGS_bp+=linux-aarch64
openssl_ACARGS_qemuarm64+=linux-aarch64

GENDIR+=$(openssl_BUILDDIR)

# enable-engine enable-afalgeng
openssl_defconfig $(openssl_BUILDDIR)/configdata.pm: | $(openssl_BUILDDIR)
	cd $(openssl_BUILDDIR) \
	  && $(BUILD_PKGCFG_ENV) $(openssl_DIR)/Configure --cross-compile-prefix=$(CROSS_COMPILE) \
	      --prefix=/ --openssldir=/lib/ssl no-tests \
	      $(openssl_ACARGS_$(APP_PLATFORM)) \
	      -L$(BUILD_SYSROOT)/lib -I$(BUILD_SYSROOT)/include

openssl_install: DESTDIR=$(BUILD_SYSROOT)
openssl_install: | $(openssl_BUILDDIR)/configdata.pm
	$(openssl_MAKE) install_sw install_ssldirs
ifneq ($(strip $(filter 0 1,$(BUILD_PKGCFG_USAGE))),)
	$(call CMD_RM_FIND,.la,$(DESTDIR)/lib,libcrypto libssl openssl)
endif
ifneq ($(strip $(filter 0,$(BUILD_PKGCFG_USAGE))),)
	$(call CMD_RM_FIND,.pc,$(DESTDIR)/lib/pkgconfig,libcrypto libssl openssl)
endif
	$(call CMD_RM_EMPTYDIR,$(DESTDIR)/lib/pkgconfig)

$(eval $(call DEF_DESTDEP,openssl))

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

GENDIR+=$(libnl_BUILDDIR)

libnl_defconfig $(libnl_BUILDDIR)/Makefile: | $(libnl_DIR)/configure $(libnl_BUILDDIR)
	cd $(libnl_BUILDDIR) \
	  && $(BUILD_PKGCFG_ENV) $(libnl_DIR)/configure \
	      --host=`$(CC) -dumpmachine` --prefix= --disable-openssl \
		  --disable-mbedtls --with-pic \
	      $(libnl_ACARGS_$(APP_PLATFORM))

libnl_install: DESTDIR=$(BUILD_SYSROOT)
libnl_install: | $(libnl_BUILDDIR)/Makefile
	$(libnl_MAKE) DESTDIR=$(DESTDIR) $(@:libnl_%=%)
ifneq ($(strip $(filter 0 1,$(BUILD_PKGCFG_USAGE))),)
	$(call CMD_RM_FIND,.la,$(DESTDIR)/lib, libnl-3* libnl-cli-3* libnl-genl-3* libnl-nf-3* libnl-route-3*)
	$(call CMD_RM_FIND,.la,$(DESTDIR)/lib/libnl/cli/cls, basic cgroup)
	$(call CMD_RM_FIND,.la,$(DESTDIR)/lib/libnl/cli/qdisc, bfifo blackhole fq_codel htb ingress pfifo plug )
endif
ifneq ($(strip $(filter 0,$(BUILD_PKGCFG_USAGE))),)
	$(call CMD_RM_FIND,.pc,$(DESTDIR)/lib/pkgconfig, libnl-3* libnl-cli-3* libnl-genl-3* libnl-nf-3* libnl-route-3*)
endif
	$(call CMD_RM_EMPTYDIR,$(DESTDIR)/lib/pkgconfig)

$(eval $(call DEF_DESTDEP,libnl))

libnl: | $(libnl_BUILDDIR)/Makefile
	$(libnl_MAKE) $(PARALLEL_BUILD)

libnl_%: | $(libnl_BUILDDIR)/Makefile
	$(libnl_MAKE) $(PARALLEL_BUILD) $(@:libnl_%=%)

#------------------------------------
#
iw_DEP=libnl
iw_DIR=$(PKGDIR2)/iw
iw_BUILDDIR?=$(BUILDDIR2)/iw-$(APP_BUILD)
iw_INCDIR=$(BUILD_SYSROOT)/include $(BUILD_SYSROOT)/include/libnl3
iw_LIBDIR=$(BUILD_SYSROOT)/lib
iw_MAKE=$(BUILD_PKGCFG_ENV) DESTDIR=$(DESTDIR) PREFIX=/ CC=$(CC) \
    CFLAGS="$(addprefix -I,$(iw_INCDIR)) -DCONFIG_LIBNL30" \
    LDFLAGS="$(addprefix -L,$(iw_LIBDIR)) -lm -pthread -lnl-3 -lnl-genl-3" \
	NO_PKG_CONFIG=1 \
	$(MAKE) -C $(iw_BUILDDIR)

GENDIR+=$(iw_BUILDDIR)

iw_defconfig $(iw_BUILDDIR)/Makefile: | $(iw_BUILDDIR)
	rsync -a $(RSYNC_VERBOSE) $(iw_DIR)/* $(iw_BUILDDIR)/

iw_install: DESTDIR=$(BUILD_SYSROOT)
iw_install: | $(iw_BUILDDIR)/Makefile
	$(iw_MAKE) DESTDIR=$(DESTDIR) install

$(eval $(call DEF_DESTDEP,iw))

iw: | $(iw_BUILDDIR)/Makefile
	$(iw_MAKE) $(PARALLEL_BUILD)

iw_%: | $(iw_BUILDDIR)/Makefile
	$(iw_MAKE) $(PARALLEL_BUILD) $(@:iw_%=%)

#------------------------------------
# apt: gettext
#
utilinux_DEP=ncursesw
utilinux_DIR=$(PKGDIR2)/util-linux
utilinux_BUILDDIR?=$(BUILDDIR2)/utilinux-$(APP_BUILD)
utilinux_INCDIR=$(BUILD_SYSROOT)/include $(BUILD_SYSROOT)/include/ncursesw
utilinux_LIBDIR=$(BUILD_SYSROOT)/lib $(BUILD_SYSROOT)/lib64
utilinux_MAKE=$(MAKE) -C $(utilinux_BUILDDIR)

$(utilinux_DIR)/configure: | $(utilinux_DIR)/autogen.sh
	cd $(utilinux_DIR) \
	  && ./autogen.sh

GENDIR+=$(utilinux_BUILDDIR)

utilinux_defconfig $(utilinux_BUILDDIR)/Makefile: | $(utilinux_DIR)/configure $(utilinux_BUILDDIR)
	cd $(utilinux_BUILDDIR) \
	  && $(utilinux_DIR)/configure \
	      --host=`$(CC) -dumpmachine` --prefix= \
	      --disable-liblastlog2 --without-python \
	      --disable-makeinstall-chown --disable-makeinstall-setuid \
	      CFLAGS="$(addprefix -I,$(utilinux_INCDIR))" \
	      LDFLAGS="$(addprefix -L,$(utilinux_LIBDIR))" \
	      $(utilinux_ACARGS_$(APP_PLATFORM))

utilinux_install: DESTDIR=$(BUILD_SYSROOT)
utilinux_install:  | $(utilinux_BUILDDIR)/Makefile
	$(utilinux_MAKE) DESTDIR=$(DESTDIR) install
ifneq ($(strip $(filter 0 1,$(BUILD_PKGCFG_USAGE))),)
	$(call CMD_RM_FIND,.la,$(DESTDIR)/lib, \
	    blkid fdisk mount smartcols uuid \
	    libblkid libfdisk libmount libsmartcols libuuid)
endif
ifneq ($(strip $(filter 0,$(BUILD_PKGCFG_USAGE))),)
	$(call CMD_RM_FIND,.pc,$(DESTDIR)/lib/pkgconfig, \
	    blkid fdisk mount smartcols uuid)
endif
	$(call CMD_RM_EMPTYDIR,$(DESTDIR)/lib/pkgconfig)

$(eval $(call DEF_DESTDEP,utilinux))

utilinux: | $(utilinux_BUILDDIR)/Makefile
	$(utilinux_MAKE) $(PARALLEL_BUILD)

utilinux_%: | $(utilinux_BUILDDIR)/Makefile
	$(utilinux_MAKE) $(PARALLEL_BUILD) $(@:utilinux_%=%)

#------------------------------------
# https://download.gnome.org/sources/glib/2.82/glib-2.82.1.tar.xz
#
glib_DEP=iconvgettext pcre2 utilinux libffi
glib_DIR=$(PKGDIR2)/glib
glib_BUILDDIR?=$(BUILDDIR2)/glib-$(APP_BUILD)
glib_MESON=. $(PYVENVDIR)/bin/activate && meson

glib_ACARGS_CPPFLAGS+=-I$(BUILD_SYSROOT)/include \
    -I$(BUILD_SYSROOT)/include/libmount \
	-I$(BUILD_SYSROOT)/include/blkid
glib_ACARGS_LDFLAGS+=-L$(BUILD_SYSROOT)/lib64 \
    -L$(BUILD_SYSROOT)/lib \
	-liconv
glib_ACARGS_PKGDIR+=$(BUILD_SYSROOT)/lib/pkgconfig \
    $(BUILD_SYSROOT)/share/pkgconfig

#   -Dc_args="$(subst $(SPACE),$(COMMA),$(glib_ACARGS_CPPFLAGS))" \
#   -Dc_link_args="$(subst $(SPACE),$(COMMA),$(glib_ACARGS_LDFLAGS))" \
#   -Dcpp_args="$(subst $(SPACE),$(COMMA),$(glib_ACARGS_CPPFLAGS))" \
#   -Dcpp_link_args="$(subst $(SPACE),$(COMMA),$(glib_ACARGS_LDFLAGS))" \
#   -Dpkg_config_path="$(subst $(SPACE),:,$(glib_ACARGS_PKGDIR))" \

glib_defconfig $(glib_BUILDDIR)/build.ninja: | $(BUILDDIR)/meson-aarch64.ini
	. $(PYVENVDIR)/bin/activate \
	  && $(BUILD_PKGCFG_ENV) meson setup \
	      -Dprefix=/ \
		  -Dc_args="$(subst $(SPACE),$(SPACE),$(glib_ACARGS_CPPFLAGS))" \
	      -Dc_link_args="$(subst $(SPACE),$(SPACE),$(glib_ACARGS_LDFLAGS))" \
		  -Dcpp_args="$(subst $(SPACE),$(SPACE),$(glib_ACARGS_CPPFLAGS))" \
	      -Dcpp_link_args="$(subst $(SPACE),$(SPACE),$(glib_ACARGS_LDFLAGS))" \
		  -Dpkg_config_path="$(subst $(SPACE),:,$(glib_ACARGS_PKGDIR))" \
		  -Dinstalled_tests=false \
		  -Dselinux=disabled \
		  -Db_coverage=false \
		  --cross-file=$(BUILDDIR)/meson-aarch64.ini \
		  $(glib_BUILDDIR) $(glib_DIR)

glib_install: DESTDIR=$(BUILD_SYSROOT)
glib_install: | $(glib_BUILDDIR)/build.ninja
	$(glib_MESON) compile -C $(glib_BUILDDIR)
	$(glib_MESON) install -C $(glib_BUILDDIR) --destdir=$(DESTDIR)

$(eval $(call DEF_DESTDEP,glib))

glib: | $(glib_BUILDDIR)/build.ninja
	$(glib_MESON) compile -C $(glib_BUILDDIR)

GENPYVENV+=meson ninja

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
# build package and install to sysroot
# packages are higher priority then busybox
	$(MAKE) $(addsuffix _destdep_install, \
	    busybox)
	$(MAKE) $(addsuffix _destdep_install, \
	    tmux mmcutils mtdutils glib)

dist_rootfs_phase2: DESTDIR=$(dist_DIR)/rootfs
dist_rootfs_phase2:
# install prebuilt to rootfs
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
	$(RMTREE) $(BUILD_SYSROOT)/lib/modules
	$(MAKE) INSTALL_MOD_PATH=$(BUILD_SYSROOT) linux_modules_install
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
	$(MAKE) DESTDIR=$(dist_DIR)/$(APP_PLATFORM)/rootfs locale_destdep_install
	# $(MAKE) dist-qemuarm64_phase2
	$(MAKE) dist-qemuarm64_phase3

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
dist-bp_phase2: | $(BUILD_SYSROOT)/root
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
	rsync -L $(RSYNC_VERBOSE) $(call uboot_BUILDDIR,bp-a53-emmc)/tispl.bin_unsigned \
	    $(BUILD_SYSROOT)/root/tispl-emmc.bin
	rsync -L $(RSYNC_VERBOSE) $(call uboot_BUILDDIR,bp-a53-emmc)/u-boot.img_unsigned \
	    $(BUILD_SYSROOT)/root/u-boot-emmc.img
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
GENDIR+=$(BUILD_SYSROOT)/root

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

