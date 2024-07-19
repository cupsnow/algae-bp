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

APP_PLATFORM?=qemuarm64

export APP_ATTR?=$(APP_ATTR_$(APP_PLATFORM))

ifneq ($(strip $(filter bp qemuarm64,$(APP_PLATFORM))),)
APP_BUILD=aarch64
else
APP_BUILD=$(APP_PLATFORM)
endif

ifeq (1,0)
# built with crosstool-NG
ARM_TOOLCHAIN_PATH?=$(PROJDIR)/tool/toolchain-arm-none-eabi
ARM_CROSS_COMPILE?=arm-none-eabi-
AARCH64_TOOLCHAIN_PATH?=$(PROJDIR)/tool/toolchain-aarch64-unknown-linux-gnu
AARCH64_CROSS_COMPILE?=aarch64-unknown-linux-gnu-
else ifeq (1,1)
# from arm
ARM_TOOLCHAIN_PATH?=$(abspath $(PROJDIR)/../arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-linux-gnueabihf)
ARM_CROSS_COMPILE?=arm-none-linux-gnueabihf-
AARCH64_TOOLCHAIN_PATH?=$(abspath $(PROJDIR)/../arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-linux-gnu)
AARCH64_CROSS_COMPILE?=aarch64-none-linux-gnu-
endif

ifneq ($(strip $(ARM_TOOLCHAIN_PATH)),)
PATH_PUSH+=$(ARM_TOOLCHAIN_PATH)/bin
endif

ifneq ($(strip $(AARCH64_TOOLCHAIN_PATH)),)
PATH_PUSH+=$(AARCH64_TOOLCHAIN_PATH)/bin
endif

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
	@echo "APP_ATTR: $(APP_ATTR)"
	@echo "AARCH64 build target: $$($(AARCH64_CROSS_COMPILE)gcc -dumpmachine)"
	@echo "ARM build target: $$($(ARM_CROSS_COMPILE)gcc -dumpmachine)"
	@echo "TOOLCHAIN_SYSROOT: $(TOOLCHAIN_SYSROOT)"

#------------------------------------
#
atf_DIR=$(PKGDIR2)/arm-trusted-firmware-upstream
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
optee_DIR=$(PKGDIR2)/optee_os-upstream
optee_BUILDDIR=$(BUILDDIR2)/optee-$(APP_PLATFORM)
optee_MAKE=$(MAKE) O=$(optee_BUILDDIR) $(optee_MAKEARGS-$(APP_PLATFORM)) \
    -C $(optee_DIR)

optee_MAKEARGS-bp+=CFG_ARM64_core=y PLATFORM=k3-am62x CFG_TEE_CORE_LOG_LEVEL=2 \
    CFG_TEE_CORE_DEBUG=y CFG_WITH_SOFTWARE_PRNG=y \
    CROSS_COMPILE=$(ARM_CROSS_COMPILE) CROSS_COMPILE64=$(AARCH64_CROSS_COMPILE)

optee: | $(BUILDDIR)/pyvenv
	. $(BUILDDIR)/pyvenv/bin/activate && \
	  $(optee_MAKE) $(PARALLEL_BUILD)

optee_%:
	. $(BUILDDIR)/pyvenv/bin/activate && \
	  $(optee_MAKE) $(PARALLEL_BUILD) $(@:optee_%=%)

GENPYVENV+=pyelftools cryptography

#------------------------------------
# git clong -b ti-linux-firmware git://git.ti.com/processor-firmware/ti-linux-firmware.git
# 
ti-linux-fw_DIR=$(PKGDIR2)/ti-linux-firmware-upstream

#------------------------------------
# apt install libssl-dev device-tree-compiler swig python3-distutils
# apt install python3-dev python3-setuptools
# for build doc: pip install yamllint jsonschema
#
# qemu-system-aarch64 -machine virt,virtualization=on,secure=off -cpu max \
#   -bios ../build/uboot-qemuarm64/u-boot.bin -nographic
#
uboot_DIR=$(PKGDIR2)/u-boot-upstream
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

uboot_defconfig $(uboot_BUILDDIR)/.config: | $(uboot_BUILDDIR)
	if [ -f uboot-$(APP_PLATFORM).defconfig ]; then \
	  cp -v uboot-$(APP_PLATFORM).defconfig $(uboot_BUILDDIR)/.config && \
	  yes "" | $(uboot_MAKE) oldconfig; \
	else \
	  $(uboot_MAKE) $(uboot_defconfig-$(APP_PLATFORM)); \
	fi

UBOOT_TOOLS+=dumpimage fdtgrep gen_eth_addr gen_ethaddr_crc \
    mkenvimage mkimage proftool spl_size_limit

ifeq ("$(MAKELEVEL)","20")
$(error Maybe endless loop, MAKELEVEL: $(MAKELEVEL))
endif

ifneq ($(strip $(filter bp,$(APP_PLATFORM))),)
# bp runs uboot for 2 different core, pass APP_PLATFORM for specified core to else
#

$(addprefix uboot_,menuconfig  htmldocs tools tools_install):
	$(MAKE) APP_PLATFORM=bp-a53 atf_BUILDDIR=$(atf_BUILDDIR) \
	    optee_BUILDDIR=$(optee_BUILDDIR) uboot_$(@:uboot_%=%)

uboot:
	$(MAKE) APP_PLATFORM=bp-r5 uboot
	$(MAKE) APP_PLATFORM=bp-a53 atf_BUILDDIR=$(atf_BUILDDIR) \
	    optee_BUILDDIR=$(optee_BUILDDIR) uboot

uboot_%:
	$(MAKE) APP_PLATFORM=bp-r5 uboot_$(@:uboot_%=%)
	$(MAKE) APP_PLATFORM=bp-a53 atf_BUILDDIR=$(atf_BUILDDIR) \
	    optee_BUILDDIR=$(optee_BUILDDIR) uboot_$(@:uboot_%=%)

ubootenv: UENV_SIZE?=$(shell $(call SED_KEYVAL1,CONFIG_ENV_SIZE) $(firstword $(wildcard \
    $(call uboot_BUILDDIR,bp-a53)/.config uboot-$(uboot_defconfig-bp-a53))))
ubootenv $(BUILDDIR)/uboot.env:
	echo "UENV_SIZE: $(UENV_SIZE)"
	$(MAKE) APP_PLATFORM=bp-a53 UENV_SIZE=$(UENV_SIZE) ubootenv

else
# normal case

$(addprefix uboot_,help):
	$(uboot_MAKE) $(PARALLEL_BUILD) $(@:uboot_%=%)

$(addprefix uboot_,htmldocs): | $(BUILDDIR)/pyvenv $(uboot_BUILDDIR)
	. $(BUILDDIR)/pyvenv/bin/activate && \
	  $(uboot_MAKE) $(PARALLEL_BUILD) $(@:uboot_%=%)

uboot_tools_install: DESTDIR?=$(PROJDIR)/tool
uboot_tools_install:
	[ -d $(DESTDIR)/bin ] || $(MKDIR) $(DESTDIR)/bin
	$(MAKE) uboot_tools
	for i in $(UBOOT_TOOLS); do \
	  cp -v $(uboot_BUILDDIR)/tools/$$i $(DESTDIR)/bin/; \
	done

$(addprefix uboot_,menuconfig savedefconfig oldconfig): | $(uboot_BUILDDIR)/.config
	$(uboot_MAKE) $(PARALLEL_BUILD) $(@:uboot_%=%)

uboot: | $(uboot_BUILDDIR)/.config $(BUILDDIR)/pyvenv
	. $(BUILDDIR)/pyvenv/bin/activate && \
	  $(uboot_MAKE) $(PARALLEL_BUILD)

uboot_%: | $(uboot_BUILDDIR)/.config $(BUILDDIR)/pyvenv
	. $(BUILDDIR)/pyvenv/bin/activate && \
	  $(uboot_MAKE) $(PARALLEL_BUILD) $(@:uboot_%=%)

ubootenv: UENV_SIZE?=$(shell $(call SED_KEYVAL1,CONFIG_ENV_SIZE) $(uboot_BUILDDIR)/.config)
ubootenv $(BUILDDIR)/uboot.env: ubootenv-$(APP_PLATFORM).txt | $(PROJDIR)/tool/bin/mkenvimage
	$(PROJDIR)/tool/bin/mkenvimage -s $(UENV_SIZE) \
	  -o $(BUILDDIR)/uboot.env ubootenv-$(APP_PLATFORM).txt

GENPYVENV+=yamllint jsonschema

# for tools_install
GENDIR+=$(PROJDIR)/tool/bin

GENDIR+=$(uboot_BUILDDIR)

# for htmldocs
GENPYVENV+=sphinx sphinx_rtd_theme six sphinx-prompt

# end of uboot APP_PLATFORM
endif

$(addprefix $(PROJDIR)/tool/bin/,$(UBOOT_TOOLS)):
	$(MAKE) DESTDIR=$(PROJDIR)/tool uboot_tools_install

#------------------------------------
# for install: make with variable INSTALL_HDR_PATH, INSTALL_MOD_PATH 
#

# linux_DIR=$(PKGDIR2)/linux-6.9.1
linux_DIR=$(PKGDIR2)/linux-upstream
linux_BUILDDIR?=$(BUILDDIR2)/linux-$(APP_PLATFORM)
linux_MAKE=$(MAKE) O=$(linux_BUILDDIR) $(linux_MAKEARGS-$(APP_PLATFORM)) \
    -C $(linux_DIR)

linux_MAKEARGS-bp+=ARCH=arm64 CROSS_COMPILE=$(AARCH64_CROSS_COMPILE)

linux_defconfig-bp=defconfig

linux_MAKEARGS-qemuarm64+=ARCH=arm64 CROSS_COMPILE=$(AARCH64_CROSS_COMPILE)

linux_defconfig-qemuarm64=defconfig

linux_defconfig $(linux_BUILDDIR)/.config: | $(linux_BUILDDIR)
	if [ -f "$(PROJDIR)/linux-$(APP_PLATFORM).config" ]; then \
	  cp -v $(PROJDIR)/linux-$(APP_PLATFORM).config $(linux_BUILDDIR)/.config && \
	  yes "" | $(linux_MAKE) oldconfig; \
	else \
	  $(linux_MAKE) $(linux_defconfig-$(APP_PLATFORM)); \
	fi

$(addprefix linux_,help):
	$(linux_MAKE) $(PARALLEL_BUILD) $(@:linux_%=%)

# dep: apt install dvipng imagemagick
#      pip install sphinx_rtd_theme six
$(addprefix linux_,htmldocs): | $(BUILDDIR)/pyvenv $(linux_BUILDDIR)
	. $(BUILDDIR)/pyvenv/bin/activate && \
	  $(linux_MAKE) $(PARALLEL_BUILD) $(@:linux_%=%)

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
bb_DIR=$(HOME)/02_dev/busybox-upstream
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
	$(MAKE) DESTDIR=$(bb_BUILDDIR)-destpkg bb_install
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

bb_install: DESTDIR?=$(BUILD_SYSROOT)
bb_install: $(bb_BUILDDIR)/.config
	$(bb_MAKE) CONFIG_PREFIX=$(DESTDIR) $(PARALLEL_BUILD) $(@:bb_%=%)

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

dist-qemuarm64:
	[ -d "$(dist_DIR)/$(APP_PLATFORM)/boot" ] || $(MKDIR) $(dist_DIR)/$(APP_PLATFORM)/boot
	$(MAKE) ubootenv
	cp -v $(BUILDDIR)/uboot.env $(dist_DIR)/$(APP_PLATFORM)/
	cp -v $(uboot_BUILDDIR)/u-boot.bin \
	    $(linux_BUILDDIR)/arch/arm64/boot/Image.gz \
	    $(linux_BUILDDIR)/arch/arm64/boot/Image \
	    $(linux_BUILDDIR)/vmlinux \
	    $(dist_DIR)/$(APP_PLATFORM)/boot/

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

dist_lfs:
	$(MAKE) DESTDIR=$(dist_DIR)/lfs bb_destdep_install
	cd $(TOOLCHAIN_SYSROOT) && \
	  rsync -aR --ignore-missing-args $(VERBOSE_RSYNC) \
	      $(foreach i,audit/ gconv/ locale/ libasan.* libgfortran.* libubsan.* \
		    *.a *.o *.la,--exclude="${i}") \
	      lib lib64 usr/lib usr/lib64 \
	      $(dist_DIR)/lfs/
	cd $(TOOLCHAIN_SYSROOT) && \
	  rsync -aR --ignore-missing-args $(VERBOSE_RSYNC) \
	      $(foreach i,sbin/sln usr/bin/gdbserver,--exclude="${i}") \
	      sbin usr/bin usr/sbin \
	      $(dist_DIR)/lfs/
	# $(MAKE) dist_strip_DIR=$(dist_DIR)/lfs/ \
	#     dist_strip_log=$(BUILDDIR)/lfs_strip.log dist_strip
	rsync -a $(VERBOSE_RSYNC) -I $(wildcard $(PROJDIR)/prebuilt/common/*) \
	    $(dist_DIR)/lfs/
	rsync -a $(VERBOSE_RSYNC) -I $(wildcard $(PROJDIR)/prebuilt/$(APP_PLATFORM)/common/*) \
	    $(dist_DIR)/lfs/
	rm -rf $(dist_DIR)/lfs.bin
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
	$(MAKE) CONFIG_PREFIX=$(SD_ROOT) bb_install
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
	    pip3 install $(sort $(GENPYVENV))

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

