#------------------------------------
# https://download.gnome.org/sources/glib/2.82/glib-2.82.1.tar.xz
#
glib_DEP=iconvgettext pcre2 utilinux libffi
glib_DIR=$(PKGDIR2)/glib
glib_BUILDDIR?=$(BUILDDIR2)/glib-$(APP_BUILD)
glib_MESON=. $(PYVENVDIR)/bin/activate && $(1) meson
glib_NINJA=. $(PYVENVDIR)/bin/activate && $(1) ninja

glib_setup $(glib_BUILDDIR): | $(PYVENVDIR) $(BUILDDIR)/meson-aarch64.ini
	$(call glib_MESON,$(BUILD_PKGCFG_ENV)) setup \
	    --cross-file $(BUILDDIR)/meson-aarch64.ini \
	    --prefix=/ \
	    --libdir=lib \
	    -Dinstalled_tests=false \
	    -Dselinux=disabled \
	    -Db_coverage=false \
		$(glib_BUILDDIR) $(glib_DIR)

glib_install: DESTDIR=$(BUILD_SYSROOT)
glib_install: | $(glib_BUILDDIR)
glib_install:
	DESTDIR=$(DESTDIR) \
	    $(glib_NINJA) -C $(glib_BUILDDIR) $(@:glib_%=%)

$(eval $(call DEF_DESTDEP,glib))

glib: | $(glib_BUILDDIR)
glib:
	$(glib_NINJA) -C $(glib_BUILDDIR)
