#------------------------------------
# example for external Makefile
# ko_MAKE=$(build_MAKE) -C $(onvcam_DIR)/ipcam/sdk/kernel/linux_3.18.31

# dtwko_DIR?=$(CURDIR)/dtwko
# dtwko_BUILDDIR?=$(CURDIR)/build/dtwko
# dtwko_MAKE=$(ko_MAKE) M=$(or $(dtwko_DIR),$(error miss dtwko_DIR)) MO=$(dtwko_BUILDDIR)
# GENDIR+=$(dtwko_BUILDDIR)
# .PHONY: dtwko
# dtwko: $(BUILDENV_REQUIREMENT) hostlsb_check | $(dtwko_BUILDDIR)
# 	[ -d "$(CURDIR)/build/dtwko" ] || mkdir -p $(CURDIR)/build/dtwko
# 	$(dtwko_MAKE)

# dtwko_%: $(BUILDENV_REQUIREMENT) hostlsb_check
# 	$(dtwko_MAKE) $(@:dtwko_%=%)

ko?=dtw

ifeq ($(KERNELRELEASE),)
linux_BUILDDIR?=/lib/modules/$(shell uname -r)/build
kmod_DIR?=$(shell pwd)

modules:
	$(MAKE) -C $(linux_BUILDDIR) M=$(kmod_DIR) EXTRA_CFLAGS="-g -DDEBUG" modules

modules_install:
	$(MAKE) -C $(linux_BUILDDIR) M=$(kmod_DIR) modules_install

clean:
	cd $(kmod_DIR) \
	  && rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions

.PHONY: modules modules_install clean

else
	# called from kernel build system: just declare what our modules are
	obj-m := $(ko).o
endif