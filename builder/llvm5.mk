llvm_DIR=$(PKGDIR2)/llvm-project
llvm_BUILDDIR=$(BUILDDIR2)/llvm-project-$(APP_BUILD)

GENDIR+=$(llvm_BUILDDIR)

$(BUILDDIR)/llvm-aarch64-toolchain.cmake: $(PROJDIR)/builder/llvm-aarch64-toolchain.cmake
	cp $(PROJDIR)/builder/llvm-aarch64-toolchain.cmake $@

llvm_defconfig $(llvm_BUILDDIR)/Makefile: | $(llvm_BUILDDIR) $(BUILDDIR)/llvm-aarch64-toolchain.cmake
	. $(PYVENVDIR)/bin/activate \
	    && cmake -G Ninja -B $(llvm_BUILDDIR) -S $(llvm_DIR)/llvm \
	        -DCMAKE_TOOLCHAIN_FILE=$(BUILDDIR)/llvm-aarch64-toolchain.cmake \
	        -DCMAKE_BUILD_TYPE=Release \
	        -DCMAKE_INSTALL_PREFIX=$(PROJDIR)/tool/llvm-aarch64 \
	        -DLLVM_TARGETS_TO_BUILD=AArch64 \
	        -DLLVM_ENABLE_PROJECTS="clang" \
	        -DLLVM_ENABLE_RUNTIMES="" \
	        -DLLVM_ENABLE_ASSERTIONS=OFF \
	        -DLLVM_INCLUDE_TESTS=OFF \
	        -DLLVM_INCLUDE_EXAMPLES=OFF \
	        -DLLVM_INCLUDE_BENCHMARKS=OFF \
	        -DLLVM_ENABLE_DUMP=ON \
	        -DLLVM_BUILD_TOOLS=ON

llvm: $(llvm_BUILDDIR)/Makefile
	. $(PYVENVDIR)/bin/activate \
	    && cmake --build $(llvm_BUILDDIR) $(PARALLEL_BUILD)

llvm_install:
	. $(PYVENVDIR)/bin/activate \
	    && cmake --install $(llvm_BUILDDIR)
