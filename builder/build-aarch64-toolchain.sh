#!/usr/bin/env bash
set -euo pipefail

# === Configuration ===
TARGET=aarch64-linux-gnu
BUILDDIR="$(pwd)/cross"
PREFIX=${BUILDDIR}/$TARGET
SYSROOT=$PREFIX/$TARGET/sysroot
SRC=${BUILDDIR}/src
NPROC=$(nproc)

mkdir -p "$PREFIX" "$SYSROOT" "$SRC"

# === Versions ===
BINUTILS_VER=2.42
GCC_VER=13.2.0
GLIBC_VER=2.39
LINUX_VER=6.9.4

# === Download sources ===
cd "$SRC"
wget -nc https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_VER.tar.xz
wget -nc https://ftp.gnu.org/gnu/gcc/gcc-$GCC_VER/gcc-$GCC_VER.tar.xz
wget -nc https://ftp.gnu.org/gnu/libc/glibc-$GLIBC_VER.tar.xz
wget -nc https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$LINUX_VER.tar.xz

tar -xf binutils-$BINUTILS_VER.tar.xz
tar -xf gcc-$GCC_VER.tar.xz
tar -xf glibc-$GLIBC_VER.tar.xz
tar -xf linux-$LINUX_VER.tar.xz

# GCC prerequisites
(cd gcc-$GCC_VER && ./contrib/download_prerequisites)

export PATH=$PREFIX/bin:$PATH

# === 1. Build binutils ===
mkdir -p build-binutils && cd build-binutils
../binutils-$BINUTILS_VER/configure \
  --target=$TARGET --prefix=$PREFIX \
  --with-sysroot=$SYSROOT \
  --disable-nls --disable-werror
make -j$NPROC
make install
cd ..

# === 2. Install Linux headers ===
cd linux-$LINUX_VER
make ARCH=arm64 INSTALL_HDR_PATH=$SYSROOT/usr headers_install
cd ..

# === 3. Build GCC stage1 (C only, no libc) ===
mkdir -p build-gcc1 && cd build-gcc1
../gcc-$GCC_VER/configure \
  --target=$TARGET --prefix=$PREFIX \
  --with-sysroot=$SYSROOT \
  --enable-languages=c \
  --disable-multilib --disable-nls \
  --without-headers
make all-gcc -j$NPROC
make install-gcc
cd ..

# === 4. Install glibc headers and startup files ===
mkdir -p build-glibc1 && cd build-glibc1
../glibc-$GLIBC_VER/configure \
  --prefix=/usr --host=$TARGET \
  --build=$(../glibc-$GLIBC_VER/scripts/config.guess) \
  --with-headers=$SYSROOT/usr/include \
  --enable-kernel=4.15 \
  --disable-multilib \
  --with-sysroot=$SYSROOT
make install-bootstrap-headers=yes install-headers install_root=$SYSROOT
make -j$NPROC csu/subdir_lib
mkdir -p $SYSROOT/usr/lib
cp csu/crt1.o csu/crti.o csu/crtn.o $SYSROOT/usr/lib
$TARGET-gcc -nostdlib -nostartfiles -shared -x c /dev/null -o $SYSROOT/usr/lib/libc.so
touch $SYSROOT/usr/include/gnu/stubs.h
cd ..

# === 5. Build full GCC (C & C++) ===
mkdir -p build-gcc2 && cd build-gcc2
../gcc-$GCC_VER/configure \
  --target=$TARGET --prefix=$PREFIX \
  --with-sysroot=$SYSROOT \
  --enable-languages=c,c++ \
  --disable-multilib --disable-nls

# failure to build libquadmath, libssp, etc
# make -j$NPROC all
# make install

make -j$NPROC all-gcc
make install-gcc
make -j$NPROC all-target-libgcc
make install-target-libgcc

cd ..

# === 6. Build full glibc ===
mkdir -p build-glibc2 && cd build-glibc2
../glibc-$GLIBC_VER/configure \
  --prefix=/usr --host=$TARGET \
  --build=$(../glibc-$GLIBC_VER/scripts/config.guess) \
  --with-headers=$SYSROOT/usr/include \
  --enable-kernel=4.15 \
  --disable-multilib \
  --with-sysroot=$SYSROOT
make -j$NPROC
make install install_root=$SYSROOT
cd ..

# === 7. Rebuild libstdc++ ===
cd build-gcc2
make -j$NPROC all-target-libstdc++-v3
make install-target-libstdc++-v3
cd ..

# === 8. Package the toolchain ===
cd "${BUILDDIR}"
tar -czf $TARGET-toolchain.tar.gz $TARGET

echo "✅ Cross toolchain built and packaged:"
echo "   ${BUILDDIR}/$TARGET-toolchain.tar.gz"
echo "Add to PATH: export PATH=\${BUILDDIR}/$TARGET/bin:\$PATH"
