Build GCC
====


Build Relocatable GCC
----

- Build dependencies

      apt install build-essential libgmp-dev libmpfr-dev libmpc-dev flex bison texinfo

- Download prerequiste

      cd gcc \
        && ./contrib/download_prerequisites

- Build gcc

      mkdir -p build_gcc-x86_64-pc-linux-gnu \
        && cd build_gcc-x86_64-pc-linux-gnu
      ../gcc/configure \
          --prefix= \
          --enable-languages=c,c++ \
          --disable-multilib \
          --enable-static \
          --disable-bootstrap \
          --disable-nls \
          --with-system-zlib
      make -j24
      make DESTDIR=`pwd`/../gcc-x86_64-pc-linux-gnu install

  > - --prefix as a placeholder, then install and move it.
  > - --disable-bootstrap: speeds up build
  > - --disable-nls: skips native language support
  > - --enable-static: helps ensure it doesn't depend on system libs
  > - --with-system-zlib: avoids building its own zlib

Build Relocatable Cross Compiler Without Glibc
----

- Build dependencies

      apt install build-essential texinfo libgmp-dev libmpfr-dev libmpc-dev libisl-dev flex bison

- Build binutils

      mkdir -p build_binutils-aarch64-linux-gnu \
        && cd build_binutils-aarch64-linux-gnu
      ../binutils/configure \
          --target=aarch64-linux-gnu \
          --prefix= \
          --disable-nls \
          --disable-werror
      make -j24
      make DESTDIR=`pwd`/../gcc-aarch64-linux-gnu install

- Build gcc (without glibc for now)

      mkdir -p build_gcc-aarch64-linux-gnu \
        && cd build_gcc-aarch64-linux-gnu
      ../gcc/configure \
          --target=aarch64-linux-gnu \
          --prefix= \
          --disable-nls \
          --enable-languages=c,c++ \
          --without-headers \
          --disable-multilib \
          --disable-shared \
          --disable-threads \
          --disable-libssp \
          --disable-libmudflap \
          --disable-libgomp \
          --disable-werror
      make -j24 all-gcc
      make DESTDIR=`pwd`/../gcc-aarch64-linux-gnu install-gcc


Build Relocatable Cross Compiler With Glibc
----

- Build dependencies

      apt install build-essential texinfo libgmp-dev libmpfr-dev libmpc-dev libisl-dev flex bison python3 gawk libtool-bin

- Build environment

      <!-- export TARGET=aarch64-linux-gnu
      export PREFIX=$HOME/toolchains/$TARGET
      export SYSROOT=$PREFIX/$TARGET/sysroot
      export PATH=$PREFIX/bin:$PATH
      mkdir -p $PREFIX $SYSROOT
      cd $HOME/toolchains/src  -->

      export PATH=`pwd`/gcc-aarch64-linux-gnu/bin:`pwd`/gcc-14.2.1-x86_64-pc-linux-gnu/bin:$PATH

- Build binutils

      mkdir -p build_binutils-aarch64-linux-gnu \
          gcc-aarch64-linux-gnu/aarch64-linux-gnu/sysroot/usr/lib \
        && cd build_binutils-aarch64-linux-gnu
      ../binutils/configure \
          --target=aarch64-linux-gnu \
          --prefix= \
          --with-sysroot=`pwd`/../gcc-aarch64-linux-gnu/aarch64-linux-gnu/sysroot \
          --disable-nls \
          --disable-werror
      make -j24
      make DESTDIR=`pwd`/../gcc-aarch64-linux-gnu install

- Install linux header

      make ARCH=arm64 \
          INSTALL_HDR_PATH=`pwd`/../gcc-aarch64-linux-gnu/aarch64-linux-gnu/sysroot/usr \
          -C ../linux headers_install

- Build gcc (without glibc for now)

      mkdir -p build_gcc-aarch64-linux-gnu_stage1 \
        && cd build_gcc-aarch64-linux-gnu_stage1
      ../gcc/configure \
          --target=aarch64-linux-gnu \
          --prefix= \
          --with-sysroot=`pwd`/../gcc-aarch64-linux-gnu/aarch64-linux-gnu/sysroot \
          --enable-languages=c,c++ \
          --disable-multilib \
          --disable-nls \
          --disable-shared \
          --disable-threads \
          --disable-libatomic \
          --disable-libquadmath \
          --disable-libssp \
          --disable-libgomp \
          --without-headers
      make -j24 all-gcc
      make DESTDIR=`pwd`/../gcc-aarch64-linux-gnu install-gcc

- Build glibc (headers and startup files)

      mkdir -p build_glibc-aarch64-linux-gnu_stage1 \
        && cd build_glibc-aarch64-linux-gnu_stage1
      ../glibc/configure \
          --prefix= \
          --host=aarch64-linux-gnu \
          --target=aarch64-linux-gnu \
          --build=$(../glibc/scripts/config.guess) \
          --with-headers=`pwd`/../gcc-aarch64-linux-gnu/aarch64-linux-gnu/sysroot/usr/include \
          --disable-multilib \
          --enable-shared \
          --enable-kernel=4.15 \
          --disable-mathvec \
          --disable-werror \
          --with-sysroot=`pwd`/../gcc-aarch64-linux-gnu/aarch64-linux-gnu/sysroot
      make install-bootstrap-headers=yes \
          cross-compiling=yes \
          install_root=`pwd`/../gcc-aarch64-linux-gnu/aarch64-linux-gnu/sysroot \
          install-headers

  - Build required startup files

        make csu/subdir_lib
        cp csu/crt*.o `pwd`/../gcc-aarch64-linux-gnu/aarch64-linux-gnu/sysroot/usr/lib/
        aarch64-linux-gnu-gcc -nostdlib -nostartfiles -shared -x c /dev/null -o `pwd`/../gcc-aarch64-linux-gnu/aarch64-linux-gnu/sysroot/usr/lib/libc.so
        touch `pwd`/../gcc-aarch64-linux-gnu/aarch64-linux-gnu/sysroot/include/gnu/stubs.h

    > To fix error message like these
    >
    >     error: implicit declaration of function '__builtin_thread_pointer'; did you mean '__builtin_extend_pointer'? [-Wimplicit-function-declaration]
    >
    > Apply to **glibc/sysdeps/nptl/pthreadP.h**
    >
    >     extern void* __builtin_thread_pointer(void);

- Build gcc (full compiler)

      mkdir -p build_gcc-aarch64-linux-gnu_stage2 \
        && cd build_gcc-aarch64-linux-gnu_stage2
      ../gcc/configure \
          --target=aarch64-linux-gnu \
          --prefix= \
          --with-sysroot=`pwd`/../gcc-aarch64-linux-gnu/aarch64-linux-gnu/sysroot \
          --enable-languages=c,c++ \
          --disable-multilib \
          --disable-nls
      make -j24 all
      make DESTDIR=`pwd`/../gcc-aarch64-linux-gnu install
