#------------------------------------
#
llvmproj_DIR?=$(PKGDIR2)/llvm-project
llvm_DIR?=$(llvmproj_DIR)/llvm
llvm_host_DESTDIR?=$(PROJDIR)/tool/llvm

llvm_CMAKE=. $(PYVENVDIR)/bin/activate \
  && $(BUILD_PKGCFG_ENV) $(1) cmake -S $(llvm_DIR) \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX:PATH=/usr

#------------------------------------
#
llvm_host_BUILDDIR=$(BUILDDIR2)/llvm_host
llvm_host_MAKE=$(MAKE) -C $(llvm_host_BUILDDIR)

GENDIR+=$(llvm_host_BUILDDIR)

llvm_host_defconfig $(llvm_host_BUILDDIR)/Makefile : | $(llvm_host_BUILDDIR)
	$(llvm_CMAKE) -B $(llvm_host_BUILDDIR) \
	    -DCMAKE_SYSTEM_NAME=Linux \
	    -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra" 

llvm_host_install: DESTDIR=$(llvm_host_DESTDIR)
llvm_host_install: | $(llvm_host_BUILDDIR)/Makefile
	$(llvm_host_MAKE) $(PARALLEL_BUILD) DESTDIR=$(DESTDIR) install

llvm_host: | $(llvm_host_BUILDDIR)/Makefile
	$(llvm_host_MAKE) $(PARALLEL_BUILD)

#------------------------------------
#
llvm_BUILDDIR=$(BUILDDIR2)/llvm-$(APP_BUILD)
llvm_MAKE=$(MAKE) -C $(llvm_BUILDDIR)

GENDIR+=$(llvm_BUILDDIR)

$(llvm_host_DESTDIR)/usr/bin/clang:
	$(MAKE) llvm_host_install

llvm_defconfig $(llvm_BUILDDIR)/Makefile: | $(llvm_BUILDDIR) $(llvm_host_DESTDIR)/usr/bin/clang
	$(llvm_CMAKE) -B $(llvm_BUILDDIR) \
	    -DCMAKE_SYSTEM_NAME=Linux \
	    -DCMAKE_SYSTEM_PROCESSOR=AArch64 \
	    -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra" \
	    -DCMAKE_C_COMPILER=$(CC) \
	    -DCMAKE_CXX_COMPILER=$(C++) \
	    -DLLVM_USE_HOST_TOOLS=TRUE \
	    -DLLVM_NATIVE_TOOL_DIR=$(llvm_host_DESTDIR)/usr/bin \
	    -DLLVM_LINK_LLVM_DYLIB=ON \
	    -DLLVM_BUILD_LLVM_DYLIB=ON \
	    -DLLVM_ENABLE_RTTI=ON \
	    -DLLVM_ENABLE_EH=ON \
	    -DLLVM_TARGETS_TO_BUILD="AArch64;SPIRV" \
	    -DLLVM_INCLUDE_TESTS=OFF \
	    -DLLVM_INCLUDE_EXAMPLES=OFF \
	    -DLLVM_INCLUDE_BENCHMARKS=OFF \
	    -DLLVM_INCLUDE_DOCS=OFF \
	    -DCMAKE_SHARED_LINKER_FLAGS="-Wl,-rpath-link=$(BUILD_SYSROOT)/lib:$(BUILD_SYSROOT)/lib64:$(BUILD_SYSROOT)/usr/lib:$(BUILD_SYSROOT)/usr/lib64:$(TOOLCHAIN_SYSROOT)/../lib64" \
	    -DCMAKE_EXE_LINKER_FLAGS="-Wl,-rpath-link=$(BUILD_SYSROOT)/lib:$(BUILD_SYSROOT)/lib64:$(BUILD_SYSROOT)/usr/lib:$(BUILD_SYSROOT)/usr/lib64:$(TOOLCHAIN_SYSROOT)/../lib64" \
	    -DCMAKE_MODULE_LINKER_FLAGS="-Wl,-rpath-link=$(BUILD_SYSROOT)/lib:$(BUILD_SYSROOT)/lib64:$(BUILD_SYSROOT)/usr/lib:$(BUILD_SYSROOT)/usr/lib64:$(TOOLCHAIN_SYSROOT)/../lib64"

llvm_install: DESTDIR=$(BUILD_SYSROOT)
llvm_install: | $(llvm_BUILDDIR)/Makefile
	$(llvm_MAKE) $(PARALLEL_BUILD) DESTDIR=$(DESTDIR) install

llvm: | $(llvm_BUILDDIR)/Makefile
	$(llvm_MAKE) $(PARALLEL_BUILD)
