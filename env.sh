do_setenv () {
  PROJDIR=/home/joelai/02_dev/algae-ws/algae-bp
  BUILDDIR=/home/joelai/02_dev/algae-ws/algae-bp/build
  PKGDIR=/home/joelai/02_dev/algae-ws/algae-bp/package
  PKGDIR2=/home/joelai/02_dev/algae-ws
  BUILDDIR2=/home/joelai/02_dev/algae-ws/build
  APP_BUILD=aarch64
  TOOLCHAIN_PATH=/home/joelai/07_sw/pkg/arm-gnu-toolchain-13.2.Rel1-x86_64-aarch64-none-linux-gnu
  CROSS_COMPILE=aarch64-none-linux-gnu-
  TOOLCHAIN_SYSROOT=/home/joelai/07_sw/pkg/arm-gnu-toolchain-13.2.Rel1-x86_64-aarch64-none-linux-gnu/aarch64-none-linux-gnu/libc
  BUILD_SYSROOT=/home/joelai/02_dev/algae-ws/build/sysroot-bp
  PYVENVDIR=/home/joelai/02_dev/algae-ws/algae-bp/.venv
}
if ! command -v aarch64-none-linux-gnu-gcc >/dev/null 2>&1; then
  do_setenv
  export PATH=${TOOLCHAIN_PATH}/bin:$PATH
fi
