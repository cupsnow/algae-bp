#------------------------------------
# https://download.gnome.org/sources/glib/2.82/glib-2.82.1.tar.xz
#
glib_DEP?=pcre2 utilinux libffi iconvgettext
glib_DIR=$(PKGDIR2)/glib
glib_BUILDDIR?=$(BUILDDIR2)/glib-$(APP_PLATFORM)
glib_MESON=. $(PYVENVDIR)/bin/activate && $(1) meson
glib_NINJA=. $(PYVENVDIR)/bin/activate && $(1) ninja

glib_CROSSFILE_bp=$(BUILDDIR)/meson-aarch64-$(APP_PLATFORM).ini
glib_CROSSFILE_qemuarm64=$(BUILDDIR)/meson-aarch64-$(APP_PLATFORM).ini

glib_INCDIR=$(BUILD_INCDIR)
glib_LIBDIR=$(BUILD_LIBDIR)

# meson setup check by c++, but glib build by c -> set both c and cpp args
# need -rpath-link to link the dependent of linked libraries (iconv, ...)
# 
# 
# 

glib_setup $(glib_BUILDDIR): | $(PYVENVDIR) $(glib_CROSSFILE_$(APP_PLATFORM))
	$(call glib_MESON,$(BUILD_PKGCFG_ENV)) setup \
	    $(glib_CROSSFILE_$(APP_PLATFORM):%=--cross-file=%) \
	    --prefix=/ \
	    --libdir=lib \
		-Dc_args="$(glib_INCDIR:%=-I%)" \
		-Dcpp_args="$(glib_INCDIR:%=-I%)" \
		-Dc_link_args="$(glib_LIBDIR:%=-L%) $(glib_LIBDIR:%=-Wl,-rpath-link=%)" \
		-Dcpp_link_args="$(glib_LIBDIR:%=-L%) $(glib_LIBDIR:%=-Wl,-rpath-link=%)" \
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
