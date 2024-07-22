#------------------------------------
#
include builder/proj.mk

PARALLEL_BUILD?=$(or $(1),-j)10

PKGDIR=$(PROJDIR)/package
PKGDIR2=$(abspath $(PROJDIR)/..)

BUILDDIR2=$(abspath $(PROJDIR)/../build)

APP_ATTR_ub20?=ub20
APP_ATTR_bp?=bp
APP_ATTR_qemuarm64?=qemuarm64

APP_PLATFORM?=bp

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
	@echo "APP_ATTR: $(APP_ATTR)"
	@echo "AARCH64 build target: $$($(AARCH64_CROSS_COMPILE)gcc -dumpmachine)"
	@echo "ARM build target: $$($(ARM_CROSS_COMPILE)gcc -dumpmachine)"
	@echo "TOOLCHAIN_SYSROOT: $(TOOLCHAIN_SYSROOT)"

#------------------------------------
#
atf_DIR=$(PKGDIR2)/arm-trusted-firmware
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
optee_DIR=$(PKGDIR2)/optee_os
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
ti-linux-fw_DIR=$(PKGDIR2)/ti-linux-firmware

#------------------------------------
# apt install libssl-dev device-tree-compiler swig python3-distutils
# apt install python3-dev python3-setuptools
# for build doc: pip install yamllint jsonschema
#
# qemu-system-aarch64 -machine virt,virtualization=on,secure=off -cpu max \
#   -bios ../build/uboot-qemuarm64/u-boot.bin -nographic
#
uboot_DIR=$(PKGDIR2)/u-boot
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

uboot_tools_install: DESTDIR?=$(PROJDIR)/tool
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

# CMD_UENV=$(if $(3),,$(error "CMD_UENV invalid argument"))
CMD_UENV=$(PROJDIR)/tool/bin/mkenvimage \
    -s $(or $(3),$$($(call CMD_SED_KEYVAL1,CONFIG_ENV_SIZE) $(uboot_BUILDDIR)/.config)) \
    -o $(or $(2),$(DESTDIR)/uboot.env) $(or $(1),ubootenv-$(APP_PLATFORM).txt)

$(addprefix $(PROJDIR)/tool/bin/,$(UBOOT_TOOLS)):
	$(MAKE) DESTDIR=$(PROJDIR)/tool uboot_tools_install

#------------------------------------
# for install: make with variable INSTALL_HDR_PATH, INSTALL_MOD_PATH 
#

# linux_DIR=$(PKGDIR2)/linux-6.9.1
linux_DIR=$(PKGDIR2)/linux
linux_BUILDDIR?=$(BUILDDIR2)/linux-$(APP_PLATFORM)
linux_MAKE=$(MAKE) O=$(linux_BUILDDIR) $(linux_MAKEARGS-$(APP_PLATFORM)) \
    -C $(linux_DIR)

linux_MAKEARGS-bp+=ARCH=arm64 CROSS_COMPILE=$(AARCH64_CROSS_COMPILE)

linux_defconfig-bp=defconfig

linux_MAKEARGS-qemuarm64+=ARCH=arm64 CROSS_COMPILE=$(AARCH64_CROSS_COMPILE)

linux_defconfig-qemuarm64=defconfig

linux_defconfig $(linux_BUILDDIR)/.config: | $(linux_BUILDDIR)
	if [ -f "$(PROJDIR)/linux-$(APP_PLATFORM).config" ]; then \
	  cp -v $(PROJDIR)/linux-$(APP_PLATFORM).config $(linux_BUILDDIR)/.config \
	    && yes "" | $(linux_MAKE) oldconfig; \
	else \
	  $(linux_MAKE) $(linux_defconfig-$(APP_PLATFORM)); \
	fi

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
busybox_DIR=$(PKGDIR2)/busybox
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

$(addprefix busybox_,help doc html): | $(BUILDDIR)/pyvenv
	. $(BUILDDIR)/pyvenv/bin/activate && \
	  $(busybox_MAKE) $(@:busybox_%=%)

bb_destpkg $(bb_BUILDDIR)-destpkg.tar.xz:
	$(RMTREE) $(bb_BUILDDIR)-destpkg
	$(MAKE) DESTDIR=$(bb_BUILDDIR)-destpkg bb_install
	tar -Jcvf $(bb_BUILDDIR)-destpkg.tar.xz \
	    -C $(dir $(bb_BUILDDIR)-destpkg) \
		$(notdir $(bb_BUILDDIR)-destpkg)
	$(RMTREE) $(bb_BUILDDIR)-destpkg

busybox_destpkg_install: DESTDIR?=$(BUILD_SYSROOT)
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

busybox_install: DESTDIR?=$(BUILD_SYSROOT)
busybox_install: $(busybox_BUILDDIR)/.config
	$(busybox_MAKE) CONFIG_PREFIX=$(DESTDIR) $(PARALLEL_BUILD) $(@:busybox_%=%)

busybox_%: $(busybox_BUILDDIR)/.config
	$(busybox_MAKE) $(PARALLEL_BUILD) $(@:busybox_%=%)

GENDIR+=$(busybox_BUILDDIR)

#------------------------------------
#
ncursesw_DIR=$(PKGDIR2)/ncurses
ncursesw_BUILDDIR?=$(BUILDDIR2)/ncursesw-$(APP_BUILD)
ncursesw_TINFODIR=/usr/share/terminfo

# refine to comma saperated list when use in tic
ncursesw_TINFO=ansi ansi-m color_xterm,linux,pcansi-m,rxvt-basic,vt52,vt100 \
    vt102,vt220,xterm,tmux-256color,screen-256color,xterm-256color screen

# ncursesw_CFGPARAM_$(APP_PLATFORM)+=--without-debug
ncursesw_ACARGS_ub20+=--with-pkg-config=/lib
ncursesw_ACARGS_sa7715+=--disable-db-install --without-tests --without-manpages

ncursesw_MAKE=$(MAKE) -C $(ncursesw_BUILDDIR)
ncursesw_TIC=LD_LIBRARY_PATH=$(PROJDIR)/tool/lib \
    TERMINFO=$(PROJDIR)/tool/$(ncursesw_TINFODIR) $(PROJDIR)/tool/bin/tic

# ncursesw_host: DESTDIR=$(PROJDIR)/tool
# ncursesw_host:
# 	$(MAKE) APP_PLATFORM=ub20 DESTDIR=$(DESTDIR) $(@:ncursesw_host%=ncursesw%)

# ncursesw_host%: DESTDIR=$(PROJDIR)/tool 
# ncursesw_host%:
# 	$(MAKE) APP_PLATFORM=ub20 DESTDIR=$(DESTDIR) $(@:ncursesw_host%=ncursesw%)

# no strip to prevent not recoginize crosscompiled executable
ncursesw_defconfig $(ncursesw_BUILDDIR)/Makefile: | $(ncursesw_BUILDDIR)
	cd $(ncursesw_BUILDDIR) \
	  && $(BUILD_ENV) $(ncursesw_DIR)/configure --host=`$(CC) -dumpmachine` \
	    --prefix= --with-termlib --with-ticlib --enable-widec --enable-pc-files \
	    --with-default-terminfo-dir=$(ncursesw_TINFODIR) --disable-stripping \
	    CFLAGS="$(ncursesw_CFLAGS)" $(ncursesw_CFGPARAM_$(APP_PLATFORM))

# remove wrong pc file for the crosscompiled lib
ncursesw_install: DESTDIR=$(BUILD_SYSROOT)
ncursesw_install: | $(ncursesw_BUILDDIR)/Makefile
	$(ncursesw_MAKE) $(BUILDPARALLEL:%=-j%)
	$(ncursesw_MAKE) $(BUILDPARALLEL:%=-j%) install
	echo "INPUT(-lncursesw)" > $(DESTDIR)/lib/libcurses.so;
	for i in ncurses form panel menu tinfo; do \
	  if [ -e $(DESTDIR)/lib/lib$${i}w.so ]; then \
	    echo "INPUT(-l$${i}w)" > $(DESTDIR)/lib/lib$${i}.so; \
	  fi; \
	  if [ -e $(DESTDIR)/lib/lib$${i}w.a ]; then \
	    ln -sf lib$${i}w.a $(DESTDIR)/lib/lib$${i}.a; \
	  fi; \
	done

ncursesw_dist_install: DESTDIR=$(BUILD_SYSROOT)
ncursesw_dist_install:
	$(MKDIR) $(dir $(ncursesw_BUILDDIR)_footprint)
	echo "$(ncursesw_CFGPARAM_$(APP_PLATFORM))" > $(ncursesw_BUILDDIR)_footprint
	$(call RUN_DIST_INSTALL1,ncursesw,$(ncursesw_BUILDDIR)/Makefile)


# Create small terminfo refer to https://invisible-island.net/ncurses/ncurses.faq.html#big_terminfo
# opt dep: [ -x $(PROJDIR)/tool/bin/tic ] || $(MAKE) ncursesw_host_install
ncursesw_terminfo_install: DESTDIR=$(BUILD_SYSROOT)
ncursesw_terminfo_install: ncursesw_TINFO2=$(subst $(SPACE),$(COMMA),$(sort \
  $(subst $(COMMA),$(SPACE),$(ncursesw_TINFO))))
ncursesw_terminfo_install:
	[ -d $(DESTDIR)/$(ncursesw_TINFODIR) ] || $(MKDIR) $(DESTDIR)/$(ncursesw_TINFODIR)
	$(ncursesw_TIC) -s -1 -I -e'$(ncursesw_TINFO2)' \
	    $(ncursesw_DIR)/misc/terminfo.src > $(BUILDDIR)/terminfo.src
	$(ncursesw_TIC) -s -o $(DESTDIR)/$(ncursesw_TINFODIR) \
	    $(BUILDDIR)/terminfo.src

ncursesw_terminfo_dist_install: DESTDIR=$(BUILD_SYSROOT)
ncursesw_terminfo_dist_install: terminfo_BUILDDIR=$(BUILDDIR2)/ncursesw_terminfo-$(APP_BUILD)
ncursesw_terminfo_dist_install:
	echo "tic -s -1 -I" > $(terminfo_BUILDDIR)_footprint
	echo "$(ncursesw_DEF_CFG)" >> $(terminfo_BUILDDIR)_footprint
	echo "$(ncursesw_TINFO)" >> $(terminfo_BUILDDIR)_footprint
	if ! md5sum -c "$(terminfo_BUILDDIR).md5sum"; then \
	  $(MAKE) DESTDIR=$(terminfo_BUILDDIR)_destdir \
	      ncursesw_terminfo_install && \
	  tar -cvf $(terminfo_BUILDDIR).tar -C $(dir $(terminfo_BUILDDIR)_destdir) \
	      $(notdir $(terminfo_BUILDDIR)_destdir) && \
	  md5sum $(terminfo_BUILDDIR).tar $(wildcard $(terminfo_BUILDDIR)_footprint) $(ncursesw_BUILDDIR)/Makefile \
	      > $(terminfo_BUILDDIR).md5sum && \
	  $(RM) $(terminfo_BUILDDIR)_destdir; \
	fi
	[ -d "$(DESTDIR)" ] || $(MKDIR) $(DESTDIR)
	tar -xvf $(terminfo_BUILDDIR).tar --strip-components=1 -C $(DESTDIR)

ncursesw: | $(ncursesw_BUILDDIR)/Makefile
	$(ncursesw_MAKE) $(BUILDPARALLEL:%=-j%)

ncursesw_%: | $(ncursesw_BUILDDIR)/Makefile
	$(ncursesw_MAKE) $(BUILDPARALLEL:%=-j%) $(@:ncursesw_%=%)

GENDIR += $(ncursesw_BUILDDIR)

#------------------------------------
#
dummy_DIR=$(PROJDIR)/package/dummy1

dummy1:
	$(MAKE) PROJDIR=$(PROJDIR) CROSS_COMPILE=$(CROSS_COMPILE) -C $(dummy_DIR)

#------------------------------------
#
dist_DIR=$(PROJDIR)/destdir

dist-qemuarm64-phase1:
	$(MAKE) uboot linux busybox

dist-qemuarm64-phase2: GENDIR+=$(dist_DIR)/$(APP_PLATFORM)/boot
dist-qemuarm64-phase2:
	$(MAKE) ubootenv
	cp -v $(BUILDDIR)/uboot.env $(dist_DIR)/$(APP_PLATFORM)/
	cp -v $(uboot_BUILDDIR)/u-boot.bin \
	    $(linux_BUILDDIR)/arch/arm64/boot/Image.gz \
	    $(linux_BUILDDIR)/arch/arm64/boot/Image \
	    $(linux_BUILDDIR)/vmlinux \
	    $(dist_DIR)/$(APP_PLATFORM)/boot/

dist-qemuarm64:
	$(MAKE) dist-qemuarm64-phase1
	$(MAKE) dist-qemuarm64-phase2

linuxdtb-bp:
	$(MAKE) linux-bp_dtbs
	
dist-bp-phase1:
	$(MAKE) atf optee linux
	$(MAKE) uboot linux_modules
	$(MAKE) INSTALL_HDR_PATH=$(BUILD_SYSROOT) linux_headers_install

$(dist_DIR)/$(APP_PLATFORM)/boot/dtb:
	$(MKDIR) $@

dist-bp-phase2: | $(dist_DIR)/$(APP_PLATFORM)/boot/dtb
	$(RMTREE) $(BUILD_SYSROOT)/lib/modules
	$(MAKE) INSTALL_MOD_PATH=$(BUILD_SYSROOT) linux_modules_install
	cp -L $(call uboot_BUILDDIR,bp-r5)/tiboot3-am62x-gp-evm.bin \
	    $(dist_DIR)/$(APP_PLATFORM)/boot/tiboot3.bin
	cp -L $(call uboot_BUILDDIR,bp-a53)/tispl.bin_unsigned \
	    $(dist_DIR)/$(APP_PLATFORM)/boot/tispl.bin
	cp -L $(call uboot_BUILDDIR,bp-a53)/u-boot.img_unsigned \
	    $(dist_DIR)/$(APP_PLATFORM)/boot/u-boot.img
	$(MAKE) DESTDIR=$(dist_DIR)/$(APP_PLATFORM)/boot ubootenv
	cp -L $(linux_BUILDDIR)/arch/arm64/boot/Image \
	    $(linux_BUILDDIR)/arch/arm64/boot/Image.gz \
		$(linux_BUILDDIR)/arch/arm64/boot/dts/ti/k3-am625-beagleplay.dtb \
	    $(dist_DIR)/$(APP_PLATFORM)/boot/

dist-bp:
	$(MAKE) dist-bp-phase1
	$(MAKE) dist-bp-phase2

CMD_RSYNC_TOOLCHAIN_SYSROOT=$(if $(1),,$(error "CMD_RSYNC_TOOLCHAIN_SYSROOT invalid argument")) \
  cd $(TOOLCHAIN_SYSROOT) \
    && rsync -aR --ignore-missing-args $(VERBOSE_RSYNC) \
        $(foreach i,audit/ gconv/ locale/ libasan.* libgfortran.* libubsan.* \
	        *.a *.o *.la,--exclude="${i}") \
        lib lib64 usr/lib usr/lib64 \
        $(1) \
    && rsync -aR --ignore-missing-args $(VERBOSE_RSYNC) \
        $(foreach i,sbin/sln usr/bin/gdbserver,--exclude="${i}") \
        sbin usr/bin usr/sbin \
        $(1)

CMD_RSYNC_PREBUILT=$(if $(2),,$(error "CMD_RSYNC_PREBUILT invalid argument")) \
    $(if $(strip $(wildcard $(2))), \
      rsync -a $(VERBOSE_RSYNC) -I $(wildcard $(2)) $(1))

dist_lfs:
	$(MAKE) DESTDIR=$(dist_DIR)/lfs busybox_destdep_install
	$(call CMD_RSYNC_TOOLCHAIN_SYSROOT,$(dist_DIR)/lfs/)
	$(call CMD_RSYNC_PREBUILT,$(dist_DIR)/lfs/,$(PROJDIR)/prebuilt/common/*)
	$(call CMD_RSYNC_PREBUILT,$(dist_DIR)/lfs/,$(PROJDIR)/prebuilt/$(APP_PLATFORM)/common/*)
	$(RMTREE) $(dist_DIR)/lfs.bin
	truncate -s 512M $(dist_DIR)/lfs.bin
	mkfs.ext4 -d $(dist_DIR)/lfs $(dist_DIR)/lfs.bin

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
	$(MAKE) CONFIG_PREFIX=$(SD_ROOT) busybox_install
	$(MAKE) INSTALL_MOD_PATH=$(SD_ROOT) linux_modules_install


#------------------------------------
# 
# qemu-system-aarch64 \
#   -machine virt,virtualization=true,gic-version=3 \
#   -nographic -m size=1024M -cpu cortex-a57 -smp 2 \
#   -kernel ../build/linux-bp/arch/arm64/boot/Image \
#   --append "console=ttyAMA0"

# qemu-system-aarch64 -m 2048 -cpu cortex-a57 -smp 2 -M virt \
#   -kernel $(linux_BUILDDIR)/arch/arm64/boot/Image.gz \
#   -bios QEMU_EFI.fd -nographic \
#   -device virtio-scsi-device -drive if=none,file=ubuntuimg.img,format=raw,index=0,id=hd0 \
#   -device virtio-blk-device,drive=hd0

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

