#------------------------------------
# https://download.gnome.org/sources/glib/2.82/glib-2.82.1.tar.xz
#
glib_DEP=iconvgettext pcre2 utilinux libffi
glib_DIR=$(PKGDIR2)/glib
glib_BUILDDIR?=$(BUILDDIR2)/glib-$(APP_BUILD)
glib_MESON=. $(PYVENVDIR)/bin/activate && meson

glib_ACARGS_CPPFLAGS+=-I$(BUILD_SYSROOT)/include \
    -I$(BUILD_SYSROOT)/include/libmount \
	-I$(BUILD_SYSROOT)/include/blkid
glib_ACARGS_LDFLAGS+=-L$(BUILD_SYSROOT)/lib64 \
    -L$(BUILD_SYSROOT)/lib \
	-liconv
glib_ACARGS_PKGDIR+=$(BUILD_SYSROOT)/lib/pkgconfig \
    $(BUILD_SYSROOT)/share/pkgconfig

glib_defconfig $(glib_BUILDDIR)/build.ninja: | $(BUILDDIR)/meson-aarch64.ini
	. $(PYVENVDIR)/bin/activate \
	  && $(BUILD_PKGCFG_ENV) meson setup \
	      -Dprefix=/ \
		  -Dc_args="$(subst $(SPACE),$(SPACE),$(glib_ACARGS_CPPFLAGS))" \
	      -Dc_link_args="$(subst $(SPACE),$(SPACE),$(glib_ACARGS_LDFLAGS))" \
		  -Dcpp_args="$(subst $(SPACE),$(SPACE),$(glib_ACARGS_CPPFLAGS))" \
	      -Dcpp_link_args="$(subst $(SPACE),$(SPACE),$(glib_ACARGS_LDFLAGS))" \
		  -Dpkg_config_path="$(subst $(SPACE),:,$(glib_ACARGS_PKGDIR))" \
		  -Dinstalled_tests=false \
		  -Dselinux=disabled \
		  -Db_coverage=false \
		  --cross-file=$(BUILDDIR)/meson-aarch64.ini \
		  $(glib_BUILDDIR) $(glib_DIR)

glib_install: DESTDIR=$(BUILD_SYSROOT)
glib_install: | $(glib_BUILDDIR)/build.ninja
	$(glib_MESON) compile -C $(glib_BUILDDIR)
	$(glib_MESON) install -C $(glib_BUILDDIR) --destdir=$(DESTDIR)

$(eval $(call DEF_DESTDEP,glib))

glib: | $(glib_BUILDDIR)/build.ninja
	$(glib_MESON) compile -C $(glib_BUILDDIR)

GENPYVENV+=meson ninja
