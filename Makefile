#------------------------------------
#
include builder/proj.mk

PARALLEL_BUILD=-j10

PKGDIR=$(PROJDIR)/package
PKGDIR2=$(abspath $(PROJDIR)/..)

BUILDDIR2=$(abspath $(PROJDIR)/../build)

APP_ATTR_ub20?=ub20
APP_ATTR_bp?=bp

APP_PLATFORM?=bp

export APP_ATTR?=$(APP_ATTR_$(APP_PLATFORM))

ifneq ($(strip $(filter bp bpim64,$(APP_PLATFORM))),)
APP_BUILD=aarch64
else
APP_BUILD=$(APP_PLATFORM)
endif

ifeq (1,1)
# built with crosstool-NG
ARM_TOOLCHAIN_PATH?=$(PROJDIR)/tool/toolchain-arm-none-eabi
ARM_CROSS_COMPILE?=arm-none-eabi-
AARCH64_TOOLCHAIN_PATH?=$(PROJDIR)/tool/toolchain-aarch64-unknown-linux-gnu
AARCH64_CROSS_COMPILE?=aarch64-unknown-linux-gnu-
else ifeq (1,1)
# from arm
ARM_TOOLCHAIN_PATH?=$(abspath $(PROJDIR)/../arm-gnu-toolchain-13.2.Rel1-x86_64-arm-none-linux-gnueabihf)
ARM_CROSS_COMPILE?=arm-none-linux-gnueabihf-
AARCH64_TOOLCHAIN_PATH?=$(abspath $(PROJDIR)/../arm-gnu-toolchain-13.2.Rel1-x86_64-aarch64-none-linux-gnu)
AARCH64_CROSS_COMPILE?=aarch64-none-linux-gnu-
endif

ifneq ($(strip $(filter bp bpim64,$(APP_PLATFORM))),)
TOOLCHAIN_PATH?=$(AARCH64_TOOLCHAIN_PATH)
CROSS_COMPILE?=$(AARCH64_CROSS_COMPILE)
else ifneq ($(strip $(filter bbb xm,$(APP_PLATFORM))),)
TOOLCHAIN_PATH?=$(ARM_TOOLCHAIN_PATH)
CROSS_COMPILE?=$(ARM_CROSS_COMPILE)
endif

PATH_PUSH+=$(AARCH64_TOOLCHAIN_PATH)/bin $(ARM_TOOLCHAIN_PATH)/bin

export PATH:=$(call ENVPATH,$(PROJDIR)/tool/bin $(PATH_PUSH) $(PATH))

CPPFLAGS+=
CFLAGS+=
CXXFLAGS+=
LDFLAGS+=

GENDIR:=
GENPYVENV:=

#------------------------------------
#
.DEFAULT_GOAL=help
help:
	$(Q)echo "APP_ATTR: $(APP_ATTR)"
	$(AARCH64_CROSS_COMPILE)gcc -dumpmachine
	$(ARM_CROSS_COMPILE)gcc -dumpmachine

#------------------------------------
#
atfa_DIR=$(PKGDIR2)/arm-trusted-firmware-upstream
atfa-bp_BUILDDIR=$(BUILDDIR2)/atfa-bp
atfa-bp_MAKE=$(MAKE) BUILD_BASE=$(atfa-bp_BUILDDIR) ARCH=aarch64 PLAT=k3 \
    TARGET_BOARD=lite SPD=opteed K3_PM_SYSTEM_SUSPEND=1 DEBUG=1 \
	CROSS_COMPILE=$(AARCH64_CROSS_COMPILE) -C $(atfa_DIR)

atfa-bp:
	$(atfa-bp_MAKE) $(PARALLEL_BUILD) all

atf: atfa_$(APP_PLATFORM)
atf_%:
	$(MAKE) atfa-$(APP_PLATFORM)$(@:atf%=%)

#------------------------------------
# pip install pyelftools cryptography
#
optee_DIR=$(PKGDIR2)/optee_os-upstream
optee-bp_BUILDDIR=$(BUILDDIR2)/optee-bp
optee-bp_MAKE=$(MAKE) O=$(optee-bp_BUILDDIR) CFG_ARM64_core=y \
    PLATFORM=k3-am62x CFG_TEE_CORE_LOG_LEVEL=2 CFG_TEE_CORE_DEBUG=y \
	CFG_WITH_SOFTWARE_PRNG=y CROSS_COMPILE=$(ARM_CROSS_COMPILE) \
	CROSS_COMPILE64=$(AARCH64_CROSS_COMPILE) -C $(optee_DIR)

optee-bp: | $(BUILDDIR)/pyvenv
	. $(BUILDDIR)/pyvenv/bin/activate && \
	  $(optee-bp_MAKE) $(PARALLEL_BUILD)

optee: optee-$(APP_PLATFORM)
optee_%:
	$(MAKE) optee-$(APP_PLATFORM)$(@:optee%=%)

GENPYVENV+=pyelftools cryptography

#------------------------------------
# apt install libssl-dev device-tree-compiler swig python3-distutils
# apt install python3-dev python3-setuptools
# pip install yamllint jsonschema
# ti-linux-fw_DIR: git checkout ti-linux-firmware
#
ti-linux-fw_DIR=$(PKGDIR2)/ti-linux-firmware
uboot_DIR=$(PKGDIR2)/u-boot-upstream
uboot-bp-r5_BUILDDIR=$(BUILDDIR2)/uboot-bp-r5
uboot-bp-r5_MAKE=$(MAKE) O=$(uboot-bp-r5_BUILDDIR) BINMAN_INDIRS=$(ti-linux-fw_DIR) \
    CROSS_COMPILE=$(ARM_CROSS_COMPILE) -C $(uboot_DIR)

uboot-bp-r5_defconfig $(uboot-bp-r5_BUILDDIR)/.config: | $(uboot-bp-r5_BUILDDIR)
	if [ -f uboot-am62x_beagleplay_r5_defconfig ]; then \
	  cp uboot-am62x_beagleplay_r5_defconfig $(uboot-bp-r5_BUILDDIR)/.config && \
	  yes "" | $(uboot-bp-r5_MAKE) oldconfig; \
	else \
	  $(uboot-bp-r5_MAKE) am62x_beagleplay_r5_defconfig; \
	fi

uboot-bp-r5 $(uboot-bp-r5_BUILDDIR)/spl/u-boot-spl.bin: | $(uboot-bp-r5_BUILDDIR)/.config $(BUILDDIR)/pyvenv
	. $(BUILDDIR)/pyvenv/bin/activate && \
	  $(uboot-bp-r5_MAKE) $(PARALLEL_BUILD)

$(addprefix uboot-bp-r5_,menuconfig savedefconfig oldconfig): | $(uboot-bp-r5_BUILDDIR)/.config
	$(uboot-bp-r5_MAKE) $(@:uboot-bp-r5_%=%)

uboot-bp-a53_BUILDDIR=$(BUILDDIR2)/uboot-bp-a53
uboot-bp-a53_MAKE=$(MAKE) O=$(uboot-bp-a53_BUILDDIR) \
    BINMAN_INDIRS=$(ti-linux-fw_DIR) \
    BL31=$(firstword $(wildcard $(atfa-bp_BUILDDIR)/k3/lite/release/bl31.bin $(atfa-bp_BUILDDIR)/k3/lite/debug/bl31.bin)) \
    TEE=$(optee-bp_BUILDDIR)/core/tee-raw.bin \
    CROSS_COMPILE=$(CROSS_COMPILE) \
    -C $(uboot_DIR)

uboot-bp-a53_defconfig $(uboot-bp-a53_BUILDDIR)/.config: | $(uboot-bp-a53_BUILDDIR)
	if [ -f uboot-am62x_beagleplay_a53_defconfig ]; then \
	  cp uboot-am62x_beagleplay_a53_defconfig $(uboot-bp-a53_BUILDDIR)/.config && \
	  yes "" | $(uboot-bp-a53_MAKE) oldconfig; \
	else \
	  $(uboot-bp-a53_MAKE) am62x_beagleplay_a53_defconfig; \
	fi

uboot-bp-a53 $(uboot-bp-a53_BUILDDIR)/spl/u-boot-spl.bin: | $(BUILDDIR)/pyvenv $(uboot-bp-a53_BUILDDIR)/.config
	. $(BUILDDIR)/pyvenv/bin/activate && \
	  $(uboot-bp-a53_MAKE) $(PARALLEL_BUILD)

$(addprefix uboot-bp-a53_,menuconfig savedefconfig oldconfig): | $(uboot-bp-a53_BUILDDIR)/.config
	$(uboot-bp-a53_MAKE) $(@:uboot-bp-a53_%=%)

$(addprefix uboot-bp-a53_,htmldocs): | $(BUILDDIR)/pyvenv $(uboot-bp-a53_BUILDDIR)
	. $(BUILDDIR)/pyvenv/bin/activate && \
	  $(uboot-bp-a53_MAKE) $(@:uboot-bp-a53_%=%)

$(addprefix uboot-bp-a53_,tools): | $(uboot-bp-a53_BUILDDIR)
	$(uboot-bp-a53_MAKE) $(@:uboot-bp-a53_%=%)

uboot-bp-a53_%:
	  $(uboot-bp-a53_MAKE) $(@:uboot-bp-a53_%=%)

UBOOT_TOOLS+=dumpimage fdtgrep gen_eth_addr gen_ethaddr_crc \
    mkenvimage mkimage proftool spl_size_limit
uboot-bp-a53_tools_install $(addprefix $(PROJDIR)/tool/bin/,$(UBOOT_TOOLS)): | $(PROJDIR)/tool/bin
	$(MAKE) uboot-bp-a53_tools
	for i in $(UBOOT_TOOLS); do \
	  cp -v $(uboot-bp-a53_BUILDDIR)/tools/$$i $(PROJDIR)/tool/bin/; \
	done

GENDIR+=$(PROJDIR)/tool/bin

uenv-bp: UENV_SIZE=$(shell $(call CMD_SED_KEYVAL1,CONFIG_ENV_SIZE) uboot-am62x_beagleplay_a53_defconfig)
uenv-bp $(BUILDDIR)/uboot.env: | $(PROJDIR)/tool/bin/mkenvimage
	$(PROJDIR)/tool/bin/mkenvimage -s $(or $(UENV_SIZE),0x1f000) \
	  -o $(BUILDDIR)/uboot.env uenv-bp.txt

ifneq ($(strip $(filter bp,$(APP_PLATFORM))),)
uboot: uboot-bp-a53
uboot_%:
	$(MAKE) uboot-bp-a53$(@:uboot%=%)
else
uboot: uboot-$(APP_PLATFORM)
uboot_%:
	$(MAKE) uboot-$(APP_PLATFORM)$(@:uboot%=%)
endif

uenv: uenv-$(APP_PLATFORM)

GENDIR+=$(uboot-bp-r5_BUILDDIR) $(uboot-bp-a53_BUILDDIR)

GENPYVENV+=yamllint jsonschema

# for htmldocs
GENPYVENV+=sphinx sphinx_rtd_theme six sphinx-prompt

#------------------------------------
# linux-sa7715
#
linux_DIR=$(PKGDIR2)/linux-upstream
linux-bp_BUILDDIR?=$(BUILDDIR2)/linux-bp
linux-bp_MAKE=$(MAKE) ARCH=arm64 CROSS_COMPILE=$(CROSS_COMPILE) \
    O=$(linux-bp_BUILDDIR) -C $(linux_DIR)

linux-bp_defconfig $(linux-bp_BUILDDIR)/.config: | $(linux-bp_BUILDDIR)
	if [ -f "$(PROJDIR)/linux-bp.config" ]; then \
	  cp -v $(PROJDIR)/linux-bp.config $(linux-bp_BUILDDIR)/.config && \
	  yes "" | $(linux-bp_MAKE) oldconfig; \
	else \
	  $(linux-bp_MAKE) defconfig; \
	fi

linux-bp: | $(linux-bp_BUILDDIR)/.config
	$(linux-bp_MAKE) $(PARALLEL_BUILD)

linux-bp_%: | $(linux-bp_BUILDDIR)/.config
	$(linux-bp_MAKE) $(PARALLEL_BUILD) $(@:linux-bp_%=%)

# linux_headers_install: DESTDIR=$(BUILD_SYSROOT)
# linux_modules_install: DESTDIR=$(BUILD_SYSROOT)

# linux: $(linux_BUILDDIR)/.config
# 	$(linux_MAKE) $(BUILDPARALLEL:%=-j%)

# linux_%: $(linux_BUILDDIR)/.config
# 	$(linux_MAKE) $(BUILDPARALLEL:%=-j%) $(@:linux_%=%)


# kernelrelease=$(BUILDDIR)/kernelrelease-$(APP_PLATFORM)
# kernelrelease $(kernelrelease):
# 	[ -d $(dir $(kernelrelease)) ] || $(MKDIR) $(dir $(kernelrelease))
# 	"make" -s --no-print-directory linux_kernelrelease | tee $(kernelrelease)

# dep: apt install dvipng imagemagick
#      pip install sphinx_rtd_theme six
$(addprefix linux-bp_,help htmldocs): | $(BUILDDIR)/pyvenv
	. $(BUILDDIR)/pyvenv/bin/activate && \
	  $(linux-bp_MAKE) $(@:linux-bp_%=%)

linux: linux-$(APP_PLATFORM)
linux_%:
	$(MAKE) linux-$(APP_PLATFORM)$(@:linux%=%)

GENPYVENV+=sphinx_rtd_theme six

GENDIR+=$(linux-bp_BUILDDIR)

#------------------------------------
#
bb_DIR=$(PKGDIR2)/busybox
bb_BUILDDIR?=$(BUILDDIR2)/busybox-$(APP_BUILD)
bb_CFLAGS+=$(BUILD_CFLAGS2_$(APP_PLATFORM))
ifneq ($(strip $(filter release1,$(APP_ATTR))),)
bb_CFLAGS+=-O3
else ifneq ($(strip $(filter debug1,$(APP_ATTR))),)
bb_CFLAGS+=-g
endif
bb_DEF_MAKE=$(MAKE) CROSS_COMPILE=$(CROSS_COMPILE) \
    "CONFIG_EXTRA_CFLAGS=$(bb_CFLAGS)"
bb_MAKE=$(bb_DEF_MAKE) CONFIG_PREFIX=$(or $(CONFIG_PREFIX),$(DESTDIR)) \
    -C $(bb_BUILDDIR)

bb_mrproper:
	$(bb_DEF_MAKE) -C $(bb_DIR) $(@:bb_%=%)

APP_PLATFORM_bb_defconfig:
	$(MAKE) bb_mrproper
	[ -d "$(bb_BUILDDIR)" ] || $(MKDIR) $(bb_BUILDDIR)
	if [ -f "$(DOTCFG)" ]; then \
	  rsync -aL $(VERBOSE_RSYNC) $(DOTCFG) $(bb_BUILDDIR)/.config && \
	  yes "" | $(bb_DEF_MAKE) O=$(bb_BUILDDIR) -C $(bb_DIR) oldconfig; \
	else \
	  yes "" | $(bb_DEF_MAKE) O=$(bb_BUILDDIR) -C $(bb_DIR) defconfig; \
	fi

ub20_bb_defconfig: DOTCFG=$(PROJDIR)/busybox_ub20.config
ub20_bb_defconfig: APP_PLATFORM_bb_defconfig

sa7715_bb_defconfig: DOTCFG=$(PROJDIR)/busybox.config
sa7715_bb_defconfig: APP_PLATFORM_bb_defconfig

bb_defconfig $(bb_BUILDDIR)/.config:
	$(MAKE) bb_mrproper
	$(MAKE) $(APP_PLATFORM)_bb_defconfig

bb_distclean:
	$(RM) $(bb_BUILDDIR)

# dep: apt install docbook
bb_doc: | $(bb_BUILDDIR)/.config
	$(bb_MAKE) doc
	tar -Jcvf $(BUILDDIR)/busybox-docs.tar.xz --show-transformed-names \
	  --transform="s/docs/busybox-docs/" \
	  -C $(bb_BUILDDIR) docs

bb_install: DESTDIR=$(BUILD_SYSROOT)

bb_dist_install: DESTDIR=$(BUILD_SYSROOT)
bb_dist_install:
	$(RM) $(bb_BUILDDIR)_footprint
	$(call RUN_DIST_INSTALL1,bb,$(bb_BUILDDIR)/.config $(PROJDIR)/busybox.config)

bb: $(bb_BUILDDIR)/.config
	$(bb_MAKE) $(BUILDPARALLEL:%=-j%)

bb_%: $(bb_BUILDDIR)/.config
	$(bb_MAKE) $(BUILDPARALLEL:%=-j%) $(@:bb_%=%)

#------------------------------------
#
dummy_DIR=$(PROJDIR)/package/dummy1

dummy1:
	$(MAKE) PROJDIR=$(PROJDIR) CROSS_COMPILE=$(CROSS_COMPILE) -C $(dummy_DIR)

#------------------------------------
#
dist-bp:
	$(MAKE) uboot-bp-r5 atfa-bp optee-bp
	$(MAKE) uboot-bp-a53 linux-bp_Image.gz linux-bp_dtbs
	$(MAKE) uenv-bp

dist-bp_sd: SD_BOOT=$(firstword $(wildcard /media/$(USER)/BOOT /media/$(USER)/boot))
dist-bp_sd:
	cp -L $(uboot-bp-r5_BUILDDIR)/tiboot3-am62x-gp-evm.bin \
	    $(SD_BOOT)/tiboot3.bin
	cp -L $(uboot-bp-a53_BUILDDIR)/tispl.bin_unsigned \
	    $(SD_BOOT)/tispl.bin
	cp -L $(uboot-bp-a53_BUILDDIR)/u-boot.img_unsigned \
	    $(SD_BOOT)/u-boot.img
	cp -L $(BUILDDIR)/uboot.env \
	    $(linux-bp_BUILDDIR)/arch/arm64/boot/Image.gz \
	    $(linux-bp_BUILDDIR)/arch/arm64/boot/dts/ti/k3-am625-beagleplay.dtb \
		$(SD_BOOT)/

#------------------------------------
#
dist:
	$(MAKE) dist-$(APP_PLATFORM)

dist_%:
	$(MAKE) dist-$(APP_PLATFORM)_$(@:dist_%=%)

#------------------------------------
#
distclean:
	$(RMTREE) $(BUILDDIR) $(BUILDDIR2)

#------------------------------------
#
$(BUILDDIR)/pyvenv:
	python3 -m venv $@
	. $(BUILDDIR)/pyvenv/bin/activate && \
	    pip3 install $(GENPYVENV)

#------------------------------------
#
$(sort $(GENDIR)):
	$(MKDIR) $@

#------------------------------------
#------------------------------------
#------------------------------------
#

