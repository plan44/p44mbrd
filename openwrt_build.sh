#!/bin/bash

# the name of the app
CHIPAPP_NAME="p44mbrd"
OPENWRT_WRAPPER_PACKAGE="feeds/p44i/p44mbrd"

if [[ $# < 1 || $# > 2 ]]; then
  echo "Usage: $0 <openwrt buildroot path>"
  echo "  builds '${CHIPAPP_NAME}' app from current dir"
  exit 1
fi

BUILDROOT="$1"
CONFIGFILE="${BUILDROOT}/.config"
if [ ! -f "${CONFIGFILE}" ]; then
  echo "${BUILDROOT} does not seem to be a openwrt buildroot (no .config found)"
  exit 1
fi

if [[ $# > 1 ]]; then
  CONFIGFILE="$2"
  if [ ! -f "${CONFIGFILE}" ]; then
    echo "config file not found: '${CONFIGFILE}'"
    exit 1
  fi
fi


# the root of the app is the current dir
CHIPAPP_ROOT="$(pwd -P)"


# get openwrt config
source "${CONFIGFILE}" 2>/dev/null

# OpenWrt config                Definitions we need
# ===========================   =========================================
# <case statemnet>							TARGET_CPU=mips
# CONFIG_CPU_TYPE					      TARGET_CPU_TYPE=24kc
# CONFIG_TARGET_ARCH_PACKAGES 	TARGET_ARCH_PACKAGES=mipsel_24kc
# CONFIG_ARCH                 	TARGET_ARCH=mipsel
# <case statemnet>      				TARGET_LIBC=musl
# CONFIG_TARGET_SUFFIX			    TARGET_SUFFIX=-musl
#
#
# <case statemnet>              TARGET_CPU=arm
# CONFIG_CPU_TYPE					      TARGET_CPU_TYPE=arm1176jzf-s+vfp
# CONFIG_TARGET_ARCH_PACKAGES		TARGET_ARCH_PACKAGES=arm_arm1176jzf-s_vfp
# CONFIG_ARCH						        TARGET_ARCH=arm
# <case statemnet>              TARGET_LIBC=musl_eabi
# CONFIG_TARGET_SUFFIX			    TARGET_SUFFIX=-muslgnueabi

# figure out target CPU for gn build system
case "${CONFIG_ARCH}" in
  mips*) TARGET_CPU="mips"; TARGET_LIBC="musl" ;;
  arm*) TARGET_CPU="arm"; TARGET_LIBC="musl_eabi" ;;
  *) echo "Architecture '${CONFIG_ARCH}' not yet supported"; exit 1;;
esac

# those we can use directly
TARGET_ARCH="${CONFIG_ARCH}"
TARGET_CPU_TYPE="${CONFIG_CPU_TYPE}"
TARGET_ARCH_PACKAGES="${CONFIG_TARGET_ARCH_PACKAGES}"
if [ -n "${CONFIG_TARGET_SUFFIX}" ]; then
  TARGET_SUFFIX="-${CONFIG_TARGET_SUFFIX}"
fi
TARGET_COMPILER="gcc-${CONFIG_GCC_VERSION}"

# derived paths
TOOLCHAIN_ARCH="${TARGET_ARCH}_${TARGET_CPU_TYPE}"
STAGING_DIR="${BUILDROOT}/staging_dir"
SYSROOT="${STAGING_DIR}/target-${TOOLCHAIN_ARCH}_${TARGET_LIBC}"
TOOLCHAIN_PREFIX="${STAGING_DIR}/toolchain-${TOOLCHAIN_ARCH}_${TARGET_COMPILER}_${TARGET_LIBC}/bin/${TARGET_ARCH}-openwrt-linux${TARGET_SUFFIX}"
# - only needed for finding strip-new
BUILT_TOOLS="${BUILDROOT}/build_dir/toolchain-${TOOLCHAIN_ARCH}_${TARGET_COMPILER}_${TARGET_LIBC}"

# check wrapper package prebuilt_bin dir in openwrt feed
PREBUILT_BIN="${BUILDROOT}/${OPENWRT_WRAPPER_PACKAGE}/prebuilt_bin/${TARGET_ARCH_PACKAGES}"
if [ ! -d "${PREBUILT_BIN}" ]; then
  echo "Wrapper package prebuilt_bin missing: ${PREBUILT_BIN}"
  exit 1
fi

# build dir for gn/ninja
OUT_DIR="out/openwrt/${TARGET_ARCH_PACKAGES}/release"

#### Now build
cd "${CHIPAPP_ROOT}"

echo "   building from : ${CHIPAPP_ROOT}"
echo "            into : ${OUT_DIR}"
echo "     staging_dir : ${STAGING_DIR}"
echo "         sysroot : ${SYSROOT}"
echo "toolchain_prefix : ${TOOLCHAIN_PREFIX}"
echo "     built_tools : ${BUILT_TOOLS}"
echo "    prebuilt_bin : ${PREBUILT_BIN}"

#exit 1

# prep matter
#source third_party/connectedhomeip/scripts/activate.sh

# gn gen
# - w/o WPA/Wifi and openthread
# - generating a compile_commands.json for clangd language server
gn gen \
    --fail-on-unused-args \
    --export-compile-commands \
    --root=$CHIPAPP_ROOT \
    "--args=target_os=\"openwrt\" openwrt_sdk_root=\"$BUILDROOT\" openwrt_sdk_sysroot=\"$SYSROOT\" openwrt_toolchain_prefix=\"$TOOLCHAIN_PREFIX\" target_cpu=\"$TARGET_CPU\" chip_device_platform=\"linux\" chip_enable_openthread=false chip_enable_wifi=false" \
    $OUT_DIR
if [[ $? != 0 ]]; then
  echo "# gn FAILED"
fi

# build with ninja (only the app, not other stuff like address-resolve-tool)
ninja -C ${OUT_DIR} ${CHIPAPP_NAME}
if [[ $? != 0 ]]; then
  echo "# ninja build FAILED - can be repeated with:"
  echo "ninja -C ${OUT_DIR} ${CHIPAPP_NAME}"
  exit 1
fi

# store into files of wrapper package in openwrt
if [ -d "${PREBUILT_BIN}" ]; then
  "${BUILT_TOOLS}/binutils/binutils/strip-new" -o "${PREBUILT_BIN}/${CHIPAPP_NAME}" "${OUT_DIR}/${CHIPAPP_NAME}"
  echo "copied executable to: ${PREBUILT_BIN}"
fi

