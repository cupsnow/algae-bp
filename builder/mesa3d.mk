#------------------------------------
# # Install Rust
# curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
# source $HOME/.cargo/env
#
# # Add the target architecture (e.g., for ARM64)
# rustup target add aarch64-unknown-linux-gnu
#
# # Install bindgen
# cargo install bindgen-cli
#
mesa3d_DEP=libclc expat libdrm zlib spirvllvmtranslator spirvtools
mesa3d_DIR=$(PKGDIR2)/mesa3d
mesa3d_BUILDDIR?=$(BUILDDIR2)/mesa3d-$(APP_BUILD)
mesa3d_MESON=. $(PYVENVDIR)/bin/activate && $(1) meson

mesa3d_ACARGS_CPPFLAGS+=-I$(BUILD_SYSROOT)/include \
    -I$(BUILD_SYSROOT)/include/libmount \
    -I$(BUILD_SYSROOT)/include/blkid

mesa3d_ACARGS_LDFLAGS+=-L$(BUILD_SYSROOT)/lib64 \
    -L$(BUILD_SYSROOT)/lib \
    -liconv -lLLVM

mesa3d_ACARGS_PKGDIR+=$(BUILD_SYSROOT)/lib/pkgconfig \
    $(BUILD_SYSROOT)/share/pkgconfig \
    $(BUILD_SYSROOT)/usr/lib/pkgconfig \
    $(BUILD_SYSROOT)/usr/share/pkgconfig

mesa3d_ACARGS_VULKAN_DRIVERS_PREPARE_bp+=swrast imagination
mesa3d_ACARGS_VULKAN_DRIVERS=$(subst $(SPACE),$(COMMA),$(sort \
  $(mesa3d_ACARGS_VULKAN_DRIVERS_PREPARE_$(APP_PLATFORM))))


# mesa3d_platforms+=x11,wayland

mesa3d_CMAKEARGS+= \
  -Dglx=disabled

mesa3d_CMAKEARGS+= \
  -Dshared-llvm=enabled

mesa3d_CMAKEARGS+= \
  -Dprefix=/usr \
  -Dgallium-drivers=llvmpipe,softpipe \
  -Dllvm=enabled

mesa3d_CMAKEARGS+= \
  -Dmicrosoft-clc=disabled

mesa3d_CMAKEARGS+= \
  -Dmesa-clc=system

mesa3d_CMAKEARGS+= \
  -Dvulkan-drivers="$(mesa3d_ACARGS_VULKAN_DRIVERS)"

# mesa3d_CMAKEARGS+= \
#   -Dspirv-tools=disabled

mesa3d_CMAKEARGS+= \
  -Dopengl=true

# mesa3d_CMAKEARGS+= \
#   -Dgallium-rusticl=true

mesa3d_defconfig $(mesa3d_BUILDDIR)/build.ninja: | $(BUILDDIR)/meson-aarch64.ini
	. $(PYVENVDIR)/bin/activate \
	  && $(BUILD_PKGCFG_ENV) \
	      LD_LIBRARY_PATH=$(LLVM_TOOLCHAIN_PATH)/lib$(LD_LIBRARY_PATH:%=:%) \
	      meson setup \
	          --cross-file=$(BUILDDIR)/meson-aarch64.ini \
		      $(mesa3d_CMAKEARGS) \
	          -Dc_args="$(subst $(SPACE),$(SPACE),$(mesa3d_ACARGS_CPPFLAGS))" \
	          -Dc_link_args="$(subst $(SPACE),$(SPACE),$(mesa3d_ACARGS_LDFLAGS))" \
	          -Dcpp_args="$(subst $(SPACE),$(SPACE),$(mesa3d_ACARGS_CPPFLAGS))" \
	          -Dcpp_link_args="$(subst $(SPACE),$(SPACE),$(mesa3d_ACARGS_LDFLAGS))" \
	          -Dpkg_config_path="$(subst $(SPACE),:,$(mesa3d_ACARGS_PKGDIR))" \
	          -Dplatforms=$(mesa3d_platforms) \
	          $(mesa3d_BUILDDIR) $(mesa3d_DIR)

mesa3d_install: DESTDIR=$(BUILD_SYSROOT)
mesa3d_install: | $(mesa3d_BUILDDIR)/build.ninja
	$(mesa3d_MESON) compile -C $(mesa3d_BUILDDIR)
	$(mesa3d_MESON) install -C $(mesa3d_BUILDDIR) --destdir=$(DESTDIR)

$(eval $(call DEF_DESTDEP,mesa3d))

mesa3d: | $(mesa3d_BUILDDIR)/build.ninja
	$(mesa3d_MESON) compile -C $(mesa3d_BUILDDIR)

GENPYVENV+=meson ninja
