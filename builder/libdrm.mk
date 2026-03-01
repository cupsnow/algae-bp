#------------------------------------
#
libdrm_DEP=
libdrm_DIR=$(PKGDIR2)/libdrm
libdrm_BUILDDIR?=$(BUILDDIR2)/libdrm-$(APP_BUILD)
libdrm_MESON=. $(PYVENVDIR)/bin/activate && meson

libdrm_ACARGS_CPPFLAGS+=-I$(BUILD_SYSROOT)/include
libdrm_ACARGS_LDFLAGS+=-L$(BUILD_SYSROOT)/lib64 \
    -L$(BUILD_SYSROOT)/lib
libdrm_ACARGS_$(APP_PLATFORM)+=

libdrm_ACARGS_PKGDIR+=$(BUILD_SYSROOT)/lib/pkgconfig \
    $(BUILD_SYSROOT)/share/pkgconfig

GENPYVENV+=meson ninja

libdrm_defconfig $(libdrm_BUILDDIR)/build.ninja: | $(BUILDDIR)/meson-aarch64.ini
	. $(PYVENVDIR)/bin/activate \
	  && $(BUILD_PKGCFG_ENV) meson setup \
	      -Dprefix=/ \
		  -Dc_args="$(subst $(SPACE),$(SPACE),$(libdrm_ACARGS_CPPFLAGS))" \
	      -Dc_link_args="$(subst $(SPACE),$(SPACE),$(libdrm_ACARGS_LDFLAGS))" \
		  -Dcpp_args="$(subst $(SPACE),$(SPACE),$(libdrm_ACARGS_CPPFLAGS))" \
	      -Dcpp_link_args="$(subst $(SPACE),$(SPACE),$(libdrm_ACARGS_LDFLAGS))" \
		  -Dpkg_config_path="$(subst $(SPACE),:,$(libdrm_ACARGS_PKGDIR))" \
		  $(patsubst %,-D%=disabled,intel radeon amdgpu nouveau vmwgfx exynos) \
		  $(patsubst %,-D%=disabled,freedreno tegra vc4 etnaviv) \
		  $(patsubst %,-D%=disabled,man-pages) \
		  $(patsubst %,-D%=true,install-test-programs) \
		  $(libdrm_ACARGS_$(APP_PLATFORM)) \
		  --cross-file=$(BUILDDIR)/meson-aarch64.ini \
		  $(libdrm_BUILDDIR) $(libdrm_DIR)

libdrm_install: DESTDIR=$(BUILD_SYSROOT)
libdrm_install: | $(libdrm_BUILDDIR)/build.ninja
	$(libdrm_MESON) compile -C $(libdrm_BUILDDIR)
	$(libdrm_MESON) install -C $(libdrm_BUILDDIR) --destdir=$(DESTDIR)

$(eval $(call DEF_DESTDEP,libdrm))

libdrm: | $(libdrm_BUILDDIR)/build.ninja
	$(libdrm_MESON) compile -C $(libdrm_BUILDDIR)
