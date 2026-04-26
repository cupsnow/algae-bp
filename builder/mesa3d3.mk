#------------------------------------
#
mesa3d_DEP=libdrm
mesa3d_DIR=$(PKGDIR2)/mesa3d
mesa3d_BUILDDIR?=$(BUILDDIR2)/mesa3d-$(APP_BUILD)
mesa3d_MESON=. $(PYVENVDIR)/bin/activate && $(BUILD_PKGCFG_ENV) meson
mesa3d_NINJA=. $(PYVENVDIR)/bin/activate && $(1) ninja

$(BUILDDIR)/mesa3d-meson-aarch64.ini: $(PROJDIR)/builder/meson-aarch64.ini
	<$(PROJDIR)/builder/meson-aarch64.ini \
	    sed \
	    -e "s|\$${AARCH64_CROSS_COMPILE}|$(AARCH64_CROSS_COMPILE)|" \
	    -e "s|\$${BUILD_SYSROOT}|$(BUILD_SYSROOT)|" \
	    -e "s|\$${LLVM_CONFIG}|llvm-config = '$(BUILD_SYSROOT)/usr/bin/llvm-config'|" \
	    -e "s|\$${NEEDS_EXE_WRAPPER}|needs_exe_wrapper = true|" \
	    >$(BUILDDIR)/mesa3d-meson-aarch64.ini

mesa3d_defconfig $(mesa3d_BUILDDIR): | $(PYVENVDIR) $(BUILDDIR)/mesa3d-meson-aarch64.ini
	$(mesa3d_MESON) setup --cross-file=$(BUILDDIR)/mesa3d-meson-aarch64.ini \
	    --prefix=/usr --libdir=lib \
		-Dbuildtype=release \
	    -Dgles1=enabled -Dgles2=enabled -Degl=enabled -Dgbm=enabled \
		-Dglx=disabled \
	    -Dplatforms=[] \
	    -Dvulkan-drivers=[] \
	    -Dgallium-drivers=llvmpipe,softpipe,zink \
		-Dc_args="-I$(BUILD_SYSROOT)/include -I$(BUILD_SYSROOT)/usr/include" \
		-Dc_link_args="-L$(BUILD_SYSROOT)/lib -L$(BUILD_SYSROOT)/usr/lib" \
		-Dcpp_args="-I$(BUILD_SYSROOT)/include -I$(BUILD_SYSROOT)/usr/include" \
		-Dcpp_link_args="-L$(BUILD_SYSROOT)/lib -L$(BUILD_SYSROOT)/usr/lib" \
	    $(mesa3d_BUILDDIR) $(mesa3d_DIR)

mesa3d_install: DESTDIR=$(BUILD_SYSROOT)
mesa3d_install: | $(mesa3d_BUILDDIR)
mesa3d_install:
	$(call mesa3d_NINJA,DESTDIR=$(DESTDIR)) -C $(mesa3d_BUILDDIR) $(@:mesa3d_%=%)

$(eval $(call DEF_DESTDEP,mesa3d))

mesa3d: | $(mesa3d_BUILDDIR)
mesa3d:
	$(mesa3d_NINJA) -C $(mesa3d_BUILDDIR)

