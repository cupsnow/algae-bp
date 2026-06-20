#------------------------------------
#
libdrm_DIR=$(PKGDIR2)/libdrm
libdrm_BUILDDIR?=$(BUILDDIR2)/libdrm-$(APP_BUILD)
libdrm_MESON=. $(PYVENVDIR)/bin/activate && $(1) meson
libdrm_NINJA=. $(PYVENVDIR)/bin/activate && $(1) ninja

libdrm_meson_cross_file_aarch64=$(BUILDDIR)/meson-aarch64.ini

libdrm_setup $(libdrm_BUILDDIR): | $(PYVENVDIR) $(libdrm_meson_cross_file_$(APP_BUILD))
	$(call libdrm_MESON,$(BUILD_PKGCFG_ENV)) setup \
	    $(libdrm_meson_cross_file_$(APP_BUILD):%=--cross-file %) \
	    --prefix=/ \
	    --libdir=lib \
	    -Dudev=false \
		-Dtests=true \
		-Dinstall-test-programs=true \
	    $(libdrm_BUILDDIR) $(libdrm_DIR)

libdrm_install: DESTDIR=$(BUILD_SYSROOT)
libdrm_install: | $(libdrm_BUILDDIR)
libdrm_install:
	DESTDIR=$(DESTDIR) \
	    $(libdrm_NINJA) -C $(libdrm_BUILDDIR) $(@:libdrm_%=%)

$(eval $(call DEF_DESTDEP,libdrm))

libdrm: | $(libdrm_BUILDDIR)
libdrm:
	$(libdrm_NINJA) -C $(libdrm_BUILDDIR)
