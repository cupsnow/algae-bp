#------------------------------------
#
mesa3d_DEP=libdrm
mesa3d_DIR=$(PKGDIR2)/mesa3d
mesa3d_BUILDDIR?=$(BUILDDIR2)/mesa3d-$(APP_BUILD)
mesa3d_MESON=. $(PYVENVDIR)/bin/activate && $(1) meson
mesa3d_NINJA=. $(PYVENVDIR)/bin/activate && $(1) ninja

# better performance (requires LLVM cross build)
# -Dgallium-drivers=llvmpipe
# -Dllvm=enabled
mesa3d_setup $(mesa3d_BUILDDIR): | $(PYVENVDIR) $(BUILDDIR)/meson-aarch64.ini
	$(call mesa3d_MESON,$(BUILD_PKGCFG_ENV)) setup \
	    --cross-file $(BUILDDIR)/meson-aarch64.ini \
	    --prefix=/ \
	    -Dplatforms=[] \
	    -Degl=enabled \
	    -Dglx=disabled \
	    -Dgbm=enabled \
	    -Dgallium-drivers=softpipe \
	    -Dllvm=disabled \
	    -Dvulkan-drivers= \
	    $(mesa3d_BUILDDIR) $(mesa3d_DIR)

mesa3d_install: DESTDIR=$(BUILD_SYSROOT)
mesa3d_install: | $(mesa3d_BUILDDIR)
mesa3d_install:
	DESTDIR=$(DESTDIR) \
	    $(mesa3d_NINJA) -C $(mesa3d_BUILDDIR) $(@:mesa3d_%=%)

$(eval $(call DEF_DESTDEP,mesa3d))

mesa3d: | $(mesa3d_BUILDDIR)
mesa3d:
	$(mesa3d_NINJA) -C $(mesa3d_BUILDDIR)

mesa3d_setup_trial2:
	$(mesa3d_MESON) setup \
	    --cross-file $(BUILDDIR)/meson-aarch64.ini \
	    --prefix=/usr \
	    -Dplatforms=[] \
	    -Degl=enabled \
	    -Dglx=disabled \
	    -Dgbm=enabled \
	    -Dgallium-drivers=swrast \
	    -Dllvm=disabled \
	    -Dvulkan-drivers= \
	    $(mesa3d_BUILDDIR) $(mesa3d_DIR)

mesa3d_setup_trial:
	$(mesa3d_MESON) setup \
	    --cross-file $(BUILDDIR)/meson-aarch64.ini \
	    --prefix=/usr \
	    -Dplatforms=drm \
	    -Dglx=disabled \
	    -Degl=enabled \
	    -Dopengl=true \
	    -Dgallium-drivers=swrast \
	    -Dvulkan-drivers= \
	    -Dshared-glapi=enabled \
		$(mesa3d_BUILDDIR) $(mesa3d_DIR)

