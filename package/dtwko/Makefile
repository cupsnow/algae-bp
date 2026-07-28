#------------------------------------
# example for external Makefile
#
# dtwko_DIR?=$(PKGDIR)/dtwko
# dtwko_BUILDDIR?=$(BUILDDIR)/dtwko-$(APP_PLATFORM)
# dtwko_MAKE=$(MAKE) $(dtwko_MAKEARGS_$(APP_PLATFORM)) -C $(linux_BUILDDIR) \
#   M=$(or $(dtwko_DIR),$(error miss dtwko_DIR)) MO=$(dtwko_BUILDDIR)
# dtwko_MAKE+=V=1

# dtwko_MAKEARGS_bp+=ARCH=arm64 CROSS_COMPILE=$(CROSS_COMPILE)
# dtwko_MAKEARGS_qemuarm64+=ARCH=arm64 CROSS_COMPILE=$(CROSS_COMPILE)

# GENDIR+=$(dtwko_BUILDDIR)

# .PHONY: dtwko
# dtwko: | $(dtwko_BUILDDIR)
# 	$(dtwko_MAKE)

# dtwko_%: 
# 	$(dtwko_MAKE) $(@:dtwko_%=%)

ko?=dtw

ifeq ($(KERNELRELEASE),)
kmod_DIR?=$(shell pwd)
linux_BUILDDIR?=/lib/modules/$(shell uname -r)/build
kmod_MAKE=$(MAKE) -C $(linux_BUILDDIR) M=$(kmod_DIR) \
  CFLAGS_MODULE=$(CFLAGS_MODULE) LDFLAGS_MODULE=$(LDFLAGS_MODULE)

modules:
	$(kmod_MAKE) modules

modules_install:
	$(kmod_MAKE) modules_install

clean:
	cd $(kmod_DIR) \
	  && rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions

.PHONY: modules modules_install clean

else
	# called from kernel build system: just declare what our modules are
	obj-m := $(ko).o
endif
