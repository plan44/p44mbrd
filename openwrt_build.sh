#!/bin/bash

# the name of the app
CHIPAPP_NAME="p44mbrd"
# the name of the openwrt package
OPENWRT_PACKAGE_NAME="${CHIPAPP_NAME}"

FOR_DEBUG=0
if [[ $# > 1 && "$1" == "--debug" ]]; then
  FOR_DEBUG=1
  shift
fi

if [[ $# < 1 || $# > 2 ]]; then
  echo "Usage: $0 [--debug] <openwrt buildroot path> [<OpenWrt .config file>]"
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
# <case statement>							TARGET_CPU=mips
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
export TOOLCHAIN_ARCH="${TARGET_ARCH}_${TARGET_CPU_TYPE}"
export STAGING_DIR="${BUILDROOT}/staging_dir"
export SYSROOT="${STAGING_DIR}/target-${TOOLCHAIN_ARCH}_${TARGET_LIBC}"
export TOOLCHAIN_PREFIX="${STAGING_DIR}/toolchain-${TOOLCHAIN_ARCH}_${TARGET_COMPILER}_${TARGET_LIBC}/bin/${TARGET_ARCH}-openwrt-linux${TARGET_SUFFIX}"
# - only needed for finding strip-new
BUILT_TOOLS="${BUILDROOT}/build_dir/toolchain-${TOOLCHAIN_ARCH}_${TARGET_COMPILER}_${TARGET_LIBC}"

PREBUILT_BIN=
EXTRA_GN_ARGS=

# build dir for gn/ninja
if [[ ${FOR_DEBUG} -ne 0 ]]; then
  # just build, do not create prebuilt-bin
  OUT_DIR="out/openwrt/${TARGET_ARCH_PACKAGES}/debug"
  #EXTRA_GN_ARGS="target_cflags=[\"-ggdb3\"] chip_enable_schema_check=true chip_enable_transport_trace=true"
  EXTRA_GN_ARGS="target_cflags=[\"-ggdb3\"]"
else
  OUT_DIR="out/openwrt/${TARGET_ARCH_PACKAGES}/release"
  # find the feed of the openwrt wrapper package
  OPENWRT_FEED=$(${BUILDROOT}/scripts/feeds search "${OPENWRT_PACKAGE_NAME}" | sed -n -E -e "/Search/s/Search results in feed '(.*)'.*/\1/p")
  if [ -z "${OPENWRT_FEED}" ]; then
    echo "no openwrt feed found containing package ${OPENWRT_PACKAGE_NAME}"
    exit 1
  fi
  OPENWRT_WRAPPER_PACKAGE="feeds/${OPENWRT_FEED}/${OPENWRT_PACKAGE_NAME}"
  # check wrapper package prebuilt_bin dir in openwrt feed
  PREBUILT_BIN="${BUILDROOT}/${OPENWRT_WRAPPER_PACKAGE}/prebuilt_bin/${TARGET_ARCH_PACKAGES}"
  if [ ! -d "${PREBUILT_BIN}" ]; then
    echo "Wrapper package prebuilt_bin missing: ${PREBUILT_BIN}"
    exit 1
  fi
fi

#### Now build
cd "${CHIPAPP_ROOT}"

echo "   building from : ${CHIPAPP_ROOT}"
echo "            into : ${OUT_DIR}"
echo "     staging_dir : ${STAGING_DIR}"
echo "         sysroot : ${SYSROOT}"
echo "toolchain_prefix : ${TOOLCHAIN_PREFIX}"
echo "     built_tools : ${BUILT_TOOLS}"
if [ ${FOR_DEBUG} -eq 0 -a -d "${PREBUILT_BIN}" ]; then
  echo "    prebuilt_bin : ${PREBUILT_BIN}"
fi

#exit 1

# prep matter ?
#source third_party/connectedhomeip/scripts/activate.sh

# gn gen
# - w/o WPA/Wifi and openthread
# - generating a compile_commands.json for clangd language server
gn gen \
    --fail-on-unused-args \
    --export-compile-commands \
    --root=$CHIPAPP_ROOT \
    "--args=target_os=\"openwrt\" ${EXTRA_GN_ARGS} openwrt_sdk_root=\"$BUILDROOT\" openwrt_sdk_sysroot=\"$SYSROOT\" openwrt_toolchain_prefix=\"$TOOLCHAIN_PREFIX\" target_cpu=\"$TARGET_CPU\" chip_device_platform=\"linux\" chip_enable_openthread=false chip_enable_wifi=false" \
    $OUT_DIR
if [[ $? != 0 ]]; then
  echo "# gn FAILED"
  exit 1
fi

# build with ninja (only the app, not other stuff like address-resolve-tool)
ninja -C ${OUT_DIR} ${CHIPAPP_NAME}
if [[ $? != 0 ]]; then
  echo "# ninja build FAILED - can be repeated with:"
  echo "ninja -C ${OUT_DIR} ${CHIPAPP_NAME}"
  exit 1
fi

# store into files of wrapper package in openwrt
if [ ${FOR_DEBUG} -eq 0 -a -d "${PREBUILT_BIN}" ]; then
  "${BUILT_TOOLS}/binutils/binutils/strip-new" -o "${PREBUILT_BIN}/${CHIPAPP_NAME}" "${OUT_DIR}/${CHIPAPP_NAME}"
  echo "copied executable to: ${PREBUILT_BIN}"
  # also save info about the source VERSION we built that binary from
  git describe HEAD >"${PREBUILT_BIN}/git_version"
  git rev-parse HEAD >"${PREBUILT_BIN}/git_rev"
  echo "saved version/rev along with executable in: ${PREBUILT_BIN}"
  echo "# - copy ${CHIPAPP_NAME} to a target's /tmp for testing, stripped:"
  echo "\"${BUILT_TOOLS}/binutils/binutils/strip-new\" -o \"/tmp/${CHIPAPP_NAME}\" \"${OUT_DIR}/${CHIPAPP_NAME}\""
  echo "scp \"/tmp/${CHIPAPP_NAME}\" root@\${TARGET_HOST}:/tmp"
else
  # debugging
  if [ -z ${TARGET_HOST} ]; then
    echo "# TARGET_HOST is not defined, need to define it"
    # provide symbolically
    echo "export TARGET_HOST=192.168.x.x"
    TARGET_HOST='${TARGET_HOST}'
  fi
  pushd ${OUT_DIR}
    _APP=$(pwd)/${CHIPAPP_NAME}
  popd
  echo ""
  echo "# debug target executable built as ${_APP}"
  echo "# - send ${CHIPAPP_NAME} to target as debugtarget:"
  echo "pushd ${BUILDROOT}"
  echo "p44b send -s ${_APP}"
  echo "popd"
  echo "# - start debugging ${CHIPAPP_NAME}:"
  echo "${BUILDROOT}/scripts/remote-gdb ${TARGET_HOST}:9000 ${_APP}"

fi

