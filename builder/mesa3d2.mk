#------------------------------------
# Info: currently installation target set in setup, cannot use DESTDIR in make install
#
mesa3d_DEP=libdrm
mesa3d_DIR=$(PKGDIR2)/mesa3d
mesa3d_BUILDDIR?=$(BUILDDIR2)/mesa3d-$(APP_BUILD)
mesa3d_MESON=. $(PYVENVDIR)/bin/activate && $(1) meson
mesa3d_NINJA=. $(PYVENVDIR)/bin/activate && $(1) ninja

mesa3d_meson_cross_file_aarch64=$(BUILDDIR)/meson-aarch64.ini

# mesa3d_ACARGS+=-Dbuild-tests=false
# mesa3d_ACARGS+=-Dopengl=true

meson_aarch64 $(BUILDDIR)/meson-aarch64.ini: NEEDS_EXE_WRAPPER=true
# meson_aarch64 $(BUILDDIR)/meson-aarch64.ini: LLVM_CONFIG=llvm-config
meson_aarch64 $(BUILDDIR)/meson-aarch64.ini: | $(PROJDIR)/builder/meson-aarch64.ini
	rsync -a $(RSYNC_VERBOSE) $(PROJDIR)/builder/meson-aarch64.ini \
	    $(BUILDDIR)/meson-aarch64.ini
	sed -i "s|\$${BUILD_SYSROOT}|$(BUILD_SYSROOT)|" $(BUILDDIR)/meson-aarch64.ini
	sed -i "s|\$${AARCH64_CROSS_COMPILE}|$(AARCH64_CROSS_COMPILE)|" $(BUILDDIR)/meson-aarch64.ini
# 	sed -i "s|\$${NEEDS_EXE_WRAPPER}|$(if $(NEEDS_EXE_WRAPPER),needs_exe_wrapper = true)|" $(BUILDDIR)/meson-aarch64.ini
	sed -i "s|\$${NEEDS_EXE_WRAPPER}|$(NEEDS_EXE_WRAPPER:%=needs_exe_wrapper = %)|" $(BUILDDIR)/meson-aarch64.ini
	sed -i "s|\$${LLVM_CONFIG}|$(LLVM_CONFIG:%=llvm-config = '%')|" $(BUILDDIR)/meson-aarch64.ini


mesa3d_defconfig $(mesa3d_BUILDDIR): | $(PYVENVDIR) $(mesa3d_meson_cross_file_$(APP_BUILD))
	$(call mesa3d_MESON,$(BUILD_PKGCFG_ENV)) setup \
	    $(mesa3d_meson_cross_file_$(APP_BUILD):%=--cross-file %) \
	    --prefix=/usr \
	    --libdir=lib \
	    -Dbuildtype=release \
	    -Dgles1=enabled -Dgles2=enabled \
	    -Degl=enabled -Dgbm=enabled -Dglx=disabled \
	    -Dplatforms=[] \
	    -Dvulkan-drivers=[] \
	    -Dgallium-drivers=softpipe \
		-Dllvm=disabled \
	    $(mesa3d_ACARGS) \
	    $(mesa3d_BUILDDIR) $(mesa3d_DIR)

mesa3d_install: DESTDIR=$(BUILD_SYSROOT)
mesa3d_install: | $(mesa3d_BUILDDIR)
mesa3d_install:
	$(call mesa3d_NINJA,DESTDIR=$(DESTDIR)) -C $(mesa3d_BUILDDIR) $(@:mesa3d_%=%)

$(eval $(call DEF_DESTDEP,mesa3d))

mesa3d: | $(mesa3d_BUILDDIR)
mesa3d:
	$(mesa3d_NINJA) -C $(mesa3d_BUILDDIR)

