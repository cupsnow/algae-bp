#
# shellcheck disable=SC2034

PROJDIR=$(pwd)

ARM_TOOLCHAIN_PATH=${PROJDIR}/tool/gcc-arm
ARM_CROSS_COMPILE="$(${ARM_TOOLCHAIN_PATH}/bin/*-gcc -dumpmachine)-"
AARCH64_TOOLCHAIN_PATH=${PROJDIR}/tool/gcc-aarch64
AARCH64_CROSS_COMPILE="$(${AARCH64_TOOLCHAIN_PATH}/bin/*-gcc -dumpmachine)-"

_pri_env_extra="${PROJDIR}/tool/bin"
_pri_env_extra="${_pri_env_extra}:${AARCH64_TOOLCHAIN_PATH}/bin"
_pri_env_extra="${_pri_env_extra}:${ARM_TOOLCHAIN_PATH}/bin"

export PATH=${_pri_env_extra:+${_pri_env_extra}:}${PATH}

export CROSS_COMPILE=${AARCH64_CROSS_COMPILE}

