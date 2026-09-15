#------------------------------------
#
libdrm_DIR=$(PKGDIR2)/libdrm
libdrm_BUILDDIR?=$(BUILDDIR2)/libdrm-$(APP_BUILD)
libdrm_MESON=. $(PYVENVDIR)/bin/activate && $(1) meson
libdrm_NINJA=. $(PYVENVDIR)/bin/activate && $(1) ninja

libdrm_CROSSFILE-bp=$(BUILDDIR)/libdrm-cross-aarch64.txt
libdrm_CROSSFILE-qemuarm64=$(BUILDDIR)/libdrm-cross-aarch64.txt

GENDIR+=$(BUILDDIR) $(libdrm_BUILDDIR)

ifneq ($(libdrm_CROSSFILE-$(APP_PLATFORM)),)
libdrm_cross_file: $(libdrm_CROSSFILE-$(APP_PLATFORM))
endif

$(BUILDDIR)/libdrm-cross-aarch64.txt: | $(BUILDDIR) $(PROJDIR)/builder/libdrm-cross-aarch64.txt
	rsync -a $(RSYNC_VERBOSE) $(PROJDIR)/builder/libdrm-cross-aarch64.txt $@
	sed -i "s|\$${BUILD_SYSROOT}|$(BUILD_SYSROOT)|" $@
	sed -i "s|\$${AARCH64_CROSS_COMPILE}|$(AARCH64_CROSS_COMPILE)|" $@
# 	sed -i "s|\$${NEEDS_EXE_WRAPPER}|$(if $(NEEDS_EXE_WRAPPER),needs_exe_wrapper = true)|" $@
	sed -i "s|\$${NEEDS_EXE_WRAPPER}|$(NEEDS_EXE_WRAPPER:%=needs_exe_wrapper = %)|" $@
	sed -i "s|\$${LLVM_CONFIG}|$(LLVM_CONFIG:%=llvm-config = '%')|" $@

libdrm_defconfig $(libdrm_BUILDDIR)/build.ninja: | $(libdrm_BUILDDIR) $(PYVENVDIR) $(libdrm_CROSSFILE-$(APP_PLATFORM))
	$(call libdrm_MESON,$(BUILD_PKGCFG_ENV)) setup \
	    $(libdrm_CROSSFILE-$(APP_PLATFORM):%=--cross-file %) \
	    --prefix=/ \
	    --libdir=lib \
	    -Dudev=false \
		-Dtests=true \
		-Dinstall-test-programs=true \
	    $(libdrm_BUILDDIR) $(libdrm_DIR)

libdrm_install: DESTDIR=$(BUILD_SYSROOT)
libdrm_install: | $(libdrm_BUILDDIR)/build.ninja
libdrm_install:
	$(libdrm_MESON) install --destdir=$(DESTDIR) -C $(libdrm_BUILDDIR)

$(eval $(call DEF_DESTDEP,libdrm))

libdrm: | $(libdrm_BUILDDIR)/build.ninja
libdrm:
	$(call libdrm_NINJA,$(BUILD_PKGCFG_ENV)) -C $(libdrm_BUILDDIR)
