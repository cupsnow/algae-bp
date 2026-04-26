#------------------------------------
#
llvmproj_DIR=$(PKGDIR2)/llvm-project
llvm_DIR=$(llvmproj_DIR)/llvm
llvm_BUILDDIR=$(BUILDDIR2)/llvm-$(APP_BUILD)
llvm_cross_cmake_aarch64=$(BUILDDIR)/llvm-cross-aarch64.cmake

# clang;clang-tools-extra;lldb;lld;polly
llvm_LLVM_ENABLE_PROJECTS_PREPARE_ub20+=clang;lld;lldb
llvm_LLVM_ENABLE_PROJECTS_PREPARE_bp+=clang
llvm_LLVM_ENABLE_PROJECTS=$(subst $(SPACE),;,$(sort \
  $(llvm_LLVM_ENABLE_PROJECTS_PREPARE_$(APP_PLATFORM))))

# libc;libunwind;libcxxabi;libcxx;compiler-rt;openmp;llvm-libgcc;offload;flang-rt;libclc
llvm_LLVM_ENABLE_RUNTIMES_PREPARE_ub20+=libc;libcxxabi;libcxx;openmp;libclc
llvm_LLVM_ENABLE_RUNTIMES=$(subst $(SPACE),;,$(sort \
  $(llvm_LLVM_ENABLE_RUNTIMES_PREPARE_$(APP_PLATFORM))))

# AArch64;ARM;BPF;SPIRV;WebAssembly;X86
llvm_LLVM_TARGETS_TO_BUILD_PREPARE_ub20+=AArch64;ARM;BPF;SPIRV;WebAssembly;X86
llvm_LLVM_TARGETS_TO_BUILD_PREPARE_bp+=AArch64;SPIRV
llvm_LLVM_TARGETS_TO_BUILD=$(subst $(SPACE),;,$(sort \
  $(llvm_LLVM_TARGETS_TO_BUILD_PREPARE_$(APP_PLATFORM))))

# LLVM_BUILD_TOOLS default on
# LLVM_INSTALL_UTILS default off
# LLVM_ENABLE_ZLIB default on
# LLVM_ENABLE_ZSTD default on
# LLVM_INCLUDE_TESTS default on
# LLVM_INCLUDE_EXAMPLES default on
# LLVM_INCLUDE_BENCHMARKS default on
# LLVM_INCLUDE_DOCS default on
# BUILD_SHARED_LIBS default off
# LLVM_BUILD_LLVM_DYLIB default off
# LLVM_LINK_LLVM_DYLIB
# LLVM_ENABLE_RTTI default off
# LLVM_ENABLE_EH default off

llvm_CMAKEARGS+= \
  -DLLVM_ENABLE_RTTI=ON \
  -DLLVM_ENABLE_EH=ON \
  -DLLVM_ENABLE_ZSTD=OFF \
  -DLLVM_INCLUDE_TESTS=OFF \
  -DLLVM_INCLUDE_EXAMPLES=OFF \
  -DLLVM_INCLUDE_BENCHMARKS=OFF \
  -DLLVM_INCLUDE_DOCS=OFF

# BUILD_SHARED_LIBS has a misleading name. It is in fact an option for
# LLVM developers to build all LLVM libraries as separate shared libraries.
# For normal use of LLVM, it is recommended to build a single
# shared library, which is achieved by BUILD_SHARED_LIBS=OFF and
# LLVM_BUILD_LLVM_DYLIB=ON.

# LLVM_BUILD_LLVM_DYLIB to ON. We need to enable this option for the
# host as llvm-config for the host will be used in STAGING_DIR by packages
# linking against libLLVM and if this option is not selected, then llvm-config
# does not work properly. For example, it assumes that LLVM is built statically
# and cannot find libLLVM.so.

llvm_CMAKEARGS+= \
  -DLLVM_LINK_LLVM_DYLIB=ON \
  -DLLVM_BUILD_LLVM_DYLIB=ON \
  -DLLVM_ENABLE_ZLIB=OFF

llvm_CMAKEARGS_ub20+= \
  -DLLVM_INSTALL_UTILS=ON

llvm_CMAKEARGS_ub20+= \
  -DLLVM_ENABLE_LIBXML2=OFF

# This option prevents AddLLVM.cmake from adding $ORIGIN/../lib to
# binaries. Otherwise, llvm-config (host variant installed in STAGING)
# will try to use target's libc.
# llvm_CMAKEARGS_ub20+= \
#   -DCMAKE_INSTALL_RPATH="$(PROJDIR)/tool/lib"

llvm_CMAKEARGS_bp+= \
  -DLLVM_TARGET_ARCH=AArch64 \
  -DLLVM_HOST_TRIPLE=aarch64-linux-gnu \
  -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD="" \
  -DLLVM_TABLEGEN=$(LLVM_TOOLCHAIN_PATH)/bin/llvm-tblgen \
  -DLLVM_CONFIG_PATH=$(LLVM_TOOLCHAIN_PATH)/bin/llvm-config

# llvm_CMAKEARGS_bp+= \
#   -DCMAKE_CROSSCOMPILING=1

llvm_CMAKEARGS_bp+= \
  -DLLVM_BUILD_RUNTIME=Off \

llvm_CMAKEARGS_bp+= \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,-rpath-link=$(TOOLCHAIN_SYSROOT)/../lib64"

llvm_MAKE=$(MAKE) $(if $(filter 1,$(CLIARGS_VERBOSE)),VERBOSE=1) -C $(llvm_BUILDDIR)

GENDIR+=$(llvm_BUILDDIR)

$(llvm_cross_cmake_aarch64): | $(PROJDIR)/builder/$(notdir $(llvm_cross_cmake_aarch64))
	rsync -a $(RSYNC_VERBOSE) $(PROJDIR)/builder/$(notdir $(llvm_cross_cmake_aarch64)) $@
	sed -i "s|\$${BUILD_SYSROOT}|$(BUILD_SYSROOT)|" $@
	sed -i "s|\$${AARCH64_CROSS_COMPILE}|$(AARCH64_CROSS_COMPILE)|" $@

llvm_defconfig $(llvm_BUILDDIR)/Makefile: | $(llvm_BUILDDIR) $(llvm_cross_cmake_$(APP_BUILD))
	. $(PYVENVDIR)/bin/activate \
	    && $(BUILD_PKGCFG_ENV) cmake -B $(llvm_BUILDDIR) -S $(llvm_DIR) \
	        -DCMAKE_BUILD_TYPE=Release \
	        -DCMAKE_INSTALL_PREFIX=/usr \
			$(CMAKE_STAGING_PREFIX:%=-DCMAKE_STAGING_PREFIX=%) \
	        $(foreach i,COMPILE LINK TABLEGEN,-DLLVM_PARALLEL_$(i)_JOBS=15) \
	        $(llvm_cross_cmake_$(APP_BUILD):%=-DCMAKE_TOOLCHAIN_FILE="%") \
	        -DLLVM_ENABLE_PROJECTS="$(llvm_LLVM_ENABLE_PROJECTS)" \
	        -DLLVM_ENABLE_RUNTIMES="$(llvm_LLVM_ENABLE_RUNTIMES)" \
	        -DLLVM_TARGETS_TO_BUILD="$(llvm_LLVM_TARGETS_TO_BUILD)" \
	        $(llvm_CMAKEARGS_$(APP_PLATFORM)) $(llvm_CMAKEARGS)

llvm_install: DESTDIR=$(BUILD_SYSROOT)/
llvm_install:
	$(MAKE) llvm
	. $(PYVENVDIR)/bin/activate \
	    && cd $(llvm_BUILDDIR) \
	    && DESTDIR=$(DESTDIR) cmake --install .
# ifneq ($(strip $(filter 0,$(BUILD_PKGCFG_USAGE))),)
# 	$(call CMD_RM_FIND,.pc,$(DESTDIR)/lib/pkgconfig,llvm)
# endif
	$(call CMD_RM_EMPTYDIR,$(DESTDIR)/lib/pkgconfig)

$(eval $(call DEF_DESTDEP,llvm))

llvm: | $(llvm_BUILDDIR)/Makefile
	$(llvm_MAKE) $(PARALLEL_BUILD)


#------------------------------------
llvm_host2_DIR=$(llvm_DIR)
llvm_host2_BUILDDIR?=$(BUILDDIR2)/llvm-ub20

GENDIR+=$(llvm_host2_BUILDDIR)

llvm_host2_defconfig $(llvm_host2_BUILDDIR)/Makefile: CMAKE_STAGING_PREFIX=$(call BUILD_SYSROOT,bp)/usr
llvm_host2_defconfig $(llvm_host2_BUILDDIR)/Makefile: | $(llvm_host2_BUILDDIR)
	. $(PYVENVDIR)/bin/activate \
	    && $(BUILD_PKGCFG_ENV) cmake -B $(llvm_host2_BUILDDIR) -S $(llvm_host2_DIR) \
	        -G Ninja \
	        -DCMAKE_BUILD_TYPE=Release \
	        -DCMAKE_INSTALL_PREFIX=/usr \
	        $(CMAKE_STAGING_PREFIX:%=-DCMAKE_STAGING_PREFIX=%) \
	        $(foreach i,COMPILE LINK TABLEGEN,-DLLVM_PARALLEL_$(i)_JOBS=15) \
	        -DLLVM_DEFAULT_TARGET_TRIPLE=aarch64-linux-gnu \
	        -DLLVM_ENABLE_PROJECTS="clang;lld;lldb" \
	        -DLLVM_ENABLE_RUNTIMES="libc;libcxxabi;libcxx;openmp;libclc" \
	        -DLLVM_TARGETS_TO_BUILD="AArch64;ARM;BPF;SPIRV;WebAssembly;X86" \
	        -DLLVM_INSTALL_UTILS=ON \
			-DLLVM_ENABLE_RTTI=ON \
			-DLLVM_ENABLE_LIBXML2=OFF -DLLVM_ENABLE_EH=ON -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_ZLIB=OFF \
			-DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_DOCS=OFF 
			-DLLVM_LINK_LLVM_DYLIB=ON \
			-DLLVM_BUILD_LLVM_DYLIB=ON


llvm_host2: $(llvm_host2_BUILDDIR)/Makefile
	. $(PYVENVDIR)/bin/activate \
	    && ninja -C $(llvm_host2_BUILDDIR)

#------------------------------------
# CMAKE_STAGING_PREFIX
llvm_host_BUILDDIR?=$(BUILDDIR2)/llvm-ub20

llvm_host_install: DESTDIR=$(or $(LLVM_TOOLCHAIN_PATH),$(BUILD_SYSROOT))
# llvm_host_install: DESTDIR=$(PROJDIR)/destdir-llvm_host
llvm_host_install: APP_PLATFORM=ub20
llvm_host_install:
	$(MAKE) APP_PLATFORM=$(APP_PLATFORM) DESTDIR=$(DESTDIR) $(@:llvm_host_%=llvm_%)

# $(info eval $(call DEF_DESTDEP,llvm_host))
llvm_host_destpkg $(llvm_host_BUILDDIR)-destpkg.tar.xz:
	$(RMTREE) $(llvm_host_BUILDDIR)-destpkg
	$(MAKE) DESTDIR=$(llvm_host_BUILDDIR)-destpkg llvm_host_install
	tar -Jcvf $(llvm_host_BUILDDIR)-destpkg.tar.xz -C $(dir $(llvm_host_BUILDDIR)-destpkg) $(notdir $(llvm_host_BUILDDIR)-destpkg)
	$(RMTREE) $(llvm_host_BUILDDIR)-destpkg

llvm_host_destpkg_install: DESTDIR=$(LLVM_TOOLCHAIN_PATH)
llvm_host_destpkg_install: | $(llvm_host_BUILDDIR)-destpkg.tar.xz
	[ -d "$(DESTDIR)" ] || $(MKDIR) $(DESTDIR)
	tar -Jxvf $(llvm_host_BUILDDIR)-destpkg.tar.xz --strip-components=1 -C $(DESTDIR)

llvm_host_destdep_install: $(foreach iter,$(llvm_host_DEP),$(iter)_destdep_install)
	$(MAKE) llvm_host_destpkg_install
# # end of DEF_DEPINSTALL for llvm_host

llvm_host_%: APP_PLATFORM=ub20
llvm_host_%:
	$(MAKE) APP_PLATFORM=$(APP_PLATFORM) CMAKE_STAGING_PREFIX=$(call BUILD_SYSROOT,bp) llvm_$*

llvm_host: APP_PLATFORM=ub20
llvm_host:
	$(MAKE) APP_PLATFORM=$(APP_PLATFORM) CMAKE_STAGING_PREFIX=$(call BUILD_SYSROOT,bp) llvm

#------------------------------------
llvm_defconfig2: | $(llvm_BUILDDIR)
	. $(PYVENVDIR)/bin/activate \
	    && $(BUILD_PKGCFG_ENV) cmake -B $(llvm_BUILDDIR) -S $(llvm_DIR) \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_INSTALL_PREFIX:PATH=/usr \
			-DCMAKE_SYSTEM_NAME=Linux \
			-DCMAKE_SYSTEM_PROCESSOR=AArch64 \
			-DCMAKE_C_COMPILER=$(CC) \
			-DCMAKE_CXX_COMPILER=$(C++) \
			-DLLVM_USE_HOST_TOOLS=TRUE \
			-DLLVM_NATIVE_TOOL_DIR=$(PROJDIR)/destdir-llvm_host/usr/local/bin \
	        -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra"

#------------------------------------
llvm_host2_defconfig2: | $(llvm_host_BUILDDIR)
	. $(PYVENVDIR)/bin/activate \
	    && $(BUILD_PKGCFG_ENV) cmake -B $(llvm_host_BUILDDIR) -S $(llvm_DIR) \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_INSTALL_PREFIX:PATH=/usr \
			-DCMAKE_SYSTEM_NAME=Linux \
	        -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra" 

llvm_host2_install: DESTDIR=$(PROJDIR)/destdir-llvm_host
llvm_host2_install:
	$(MAKE) -C $(llvm_host_BUILDDIR) $(PARALLEL_BUILD) DESTDIR=$(DESTDIR) install

llvm_host2:
	$(MAKE) -C $(llvm_host_BUILDDIR) $(PARALLEL_BUILD)

#------------------------------------
#------------------------------------
#------------------------------------
#
