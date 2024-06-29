#------------------------------------
#
include builder/proj.mk

PARALLEL_BUILD?=-j10

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

BUILD_SYSROOT?=$(BUILDDIR2)/sysroot-$(APP_PLATFORM)

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
# for build doc: pip install pyelftools cryptography
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
# for build doc: pip install yamllint jsonschema
#
ti-linux-fw_DIR=$(PKGDIR2)/ti-linux-firmware
uboot_DIR=$(PKGDIR2)/u-boot-upstream
uboot-bp-r5_BUILDDIR=$(BUILDDIR2)/uboot-bp-r5
uboot-bp-r5_MAKE=$(MAKE) O=$(uboot-bp-r5_BUILDDIR) BINMAN_INDIRS=$(ti-linux-fw_DIR) \
    CROSS_COMPILE=$(ARM_CROSS_COMPILE) -C $(uboot_DIR)

uboot-bp-r5_defconfig $(uboot-bp-r5_BUILDDIR)/.config: | $(uboot-bp-r5_BUILDDIR)
	if [ -f uboot-bp-am62x_beagleplay_r5_defconfig ]; then \
	  cp uboot-bp-am62x_beagleplay_r5_defconfig $(uboot-bp-r5_BUILDDIR)/.config && \
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
	if [ -f uboot-bp-am62x_beagleplay_a53_defconfig ]; then \
	  cp uboot-bp-am62x_beagleplay_a53_defconfig $(uboot-bp-a53_BUILDDIR)/.config && \
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

ubootenv-bp: UENV_SIZE=$(shell $(call CMD_SED_KEYVAL1,CONFIG_ENV_SIZE) $(firstword $(wildcard $(uboot-bp-a53_BUILDDIR)/.config uboot-am62x_beagleplay_a53_defconfig)))
ubootenv-bp $(BUILDDIR)/uboot.env: ubootenv-bp.txt | $(PROJDIR)/tool/bin/mkenvimage
	$(PROJDIR)/tool/bin/mkenvimage -s $(or $(UENV_SIZE),0x1f000) \
	  -o $(BUILDDIR)/uboot.env ubootenv-bp.txt

ifneq ($(strip $(filter bp,$(APP_PLATFORM))),)
uboot: uboot-bp-a53
uboot_%:
	$(MAKE) uboot-bp-a53$(@:uboot%=%)
else
uboot: uboot-$(APP_PLATFORM)
uboot_%:
	$(MAKE) uboot-$(APP_PLATFORM)$(@:uboot%=%)
endif

GENDIR+=$(uboot-bp-r5_BUILDDIR) $(uboot-bp-a53_BUILDDIR)

GENPYVENV+=yamllint jsonschema

# for htmldocs
GENPYVENV+=sphinx sphinx_rtd_theme six sphinx-prompt

#------------------------------------
# for install: make with variable INSTALL_HDR_PATH, INSTALL_MOD_PATH 
#

# linux_DIR=$(PKGDIR2)/linux-6.9.1
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

# linux: $(linux_BUILDDIR)/.config
# 	$(linux_MAKE) $(BUILDPARALLEL:%=-j%)

# linux_%: $(linux_BUILDDIR)/.config
# 	$(linux_MAKE) $(BUILDPARALLEL:%=-j%) $(@:linux_%=%)

kernelrelease=$(BUILDDIR)/kernelrelease-$(APP_PLATFORM)
kernelrelease $(kernelrelease): | $(dir $(kernelrelease))
	$(linux-bp_MAKE) -s --no-print-directory kernelrelease > $(kernelrelease)
	@cat "$(kernelrelease)"

GENDIR+=$(dir $(kernelrelease))

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
# for install: make with variable CONFIG_PREFIX
#
bb_DIR=$(PKGDIR2)/busybox-upstream
bb_BUILDDIR?=$(BUILDDIR2)/busybox-$(APP_BUILD)
bb_MAKE=$(MAKE) ARCH=arm64 CROSS_COMPILE=$(CROSS_COMPILE) \
    O=$(bb_BUILDDIR) -C $(bb_DIR)

bb_defconfig $(bb_BUILDDIR)/.config: | $(bb_BUILDDIR)
	if [ -f "$(PROJDIR)/busybox.config" ]; then \
	  cp -v $(PROJDIR)/busybox.config $(bb_BUILDDIR)/.config && \
	  yes "" | $(bb_MAKE) oldconfig; \
	else \
	  $(bb_MAKE) defconfig; \
	fi

$(addprefix bb_,help doc html): | $(BUILDDIR)/pyvenv
	. $(BUILDDIR)/pyvenv/bin/activate && \
	  $(bb_MAKE) $(@:bb_%=%)

bb_destpkg $(bb_BUILDDIR)-destpkg.tar.xz:
	$(RMTREE) $(bb_BUILDDIR)-destpkg
	$(MAKE) CONFIG_PREFIX=$(bb_BUILDDIR)-destpkg bb_install
	tar -Jcvf $(bb_BUILDDIR)-destpkg.tar.xz \
	    -C $(dir $(bb_BUILDDIR)-destpkg) \
		$(notdir $(bb_BUILDDIR)-destpkg)
	$(RMTREE) $(bb_BUILDDIR)-destpkg

bb_destpkg_install: DESTDIR?=$(BUILD_SYSROOT)
bb_destpkg_install: | $(bb_BUILDDIR)-destpkg.tar.xz
	[ -d "$(DESTDIR)" ] || $(MKDIR) $(DESTDIR)
	tar -Jxvf $(bb_BUILDDIR)-destpkg.tar.xz --strip-components=1 \
	    -C $(DESTDIR)

bb_destdep_install: $(foreach iter,$(bb_DEP),$(iter)_destdep_install)
	$(MAKE) bb_destpkg_install

bb_distclean:
	$(RMTREE) $(bb_BUILDDIR) 

bb: | $(bb_BUILDDIR)/.config
	$(bb_MAKE) $(PARALLEL_BUILD)

bb_%: $(bb_BUILDDIR)/.config
	$(bb_MAKE) $(PARALLEL_BUILD) $(@:bb_%=%)

GENDIR+=$(bb_BUILDDIR)

#------------------------------------
#
dummy_DIR=$(PROJDIR)/package/dummy1

dummy1:
	$(MAKE) PROJDIR=$(PROJDIR) CROSS_COMPILE=$(CROSS_COMPILE) -C $(dummy_DIR)

#------------------------------------
#
dist_DIR=$(PROJDIR)/destdir

linuxdtb-bp:
	$(MAKE) linux-bp_dtbs
	
dist_phase1-bp:
	$(MAKE) uboot-bp-r5 atfa-bp optee-bp
	$(MAKE) uboot-bp-a53 linux-bp_Image.gz linux-bp_modules
	$(MAKE) ubootenv-bp linuxdtb-bp
	$(RMTREE) $(BUILD_SYSROOT)/lib/modules
	$(MAKE) INSTALL_HDR_PATH=$(BUILD_SYSROOT) linux_headers_install
	$(MAKE) INSTALL_MOD_PATH=$(BUILD_SYSROOT) linux_modules_install

dist_phase2-bp:
	$(MAKE) 

dist-bp:
	$(MAKE) dist1-bp

SD_BOOT=$(firstword $(wildcard /media/$(USER)/BOOT /media/$(USER)/boot))

dist-bp_sd: | $(SD_BOOT)/boot/dtb
	cp -L $(uboot-bp-r5_BUILDDIR)/tiboot3-am62x-gp-evm.bin $(SD_BOOT)/tiboot3.bin
	cp -L $(uboot-bp-a53_BUILDDIR)/tispl.bin_unsigned $(SD_BOOT)/tispl.bin
	cp -L $(uboot-bp-a53_BUILDDIR)/u-boot.img_unsigned $(SD_BOOT)/u-boot.img
	cp -L $(BUILDDIR)/uboot.env \
		$(SD_BOOT)/
	cp -L $(PROJDIR)/uEnv-bp.txt $(SD_BOOT)/uEnv.txt
	cp -L $(linux-bp_BUILDDIR)/arch/arm64/boot/Image \
	    $(linux-bp_BUILDDIR)/arch/arm64/boot/Image.gz \
	    $(SD_BOOT)/boot/
	cp -L $(linux-bp_BUILDDIR)/arch/arm64/boot/dts/ti/k3-am625-beagleplay.dtb \
	    $(SD_BOOT)/boot/dtb/

GENDIR+=$(SD_BOOT)/boot/dtb

dist-bp_sd2: SD_ROOT=$(firstword $(wildcard /media/$(USER)/rootfs))
dist-bp_sd2:
	$(MAKE) CONFIG_PREFIX=$(SD_ROOT) bb_install
	$(MAKE) INSTALL_MOD_PATH=$(SD_ROOT) linux_modules_install

#------------------------------------
#
dist:
	$(MAKE) dist-$(APP_PLATFORM)

dist_%:
	$(MAKE) dist-$(APP_PLATFORM)_$(@:dist_%=%)

memo_git:
	@for i in $(linux_DIR) $(ti-linux-fw_DIR) $(uboot_DIR) $(optee_DIR) \
	    $(atfa_DIR) $(bb_DIR) \
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
	. $(BUILDDIR)/pyvenv/bin/activate && \
	    pip3 install $(GENPYVENV)

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

