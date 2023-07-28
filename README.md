p44mbrd
=======

*[[if you want to support p44mbrd development, please consider to sponsor plan44]](https://github.com/sponsors/plan44)*

*p44mbrd* (plan44 matter bridge daemon) is a free (opensource, GPLv3) *matter* bridge daemon, intended as a companion to the [**vdcd**](https://github.com/plan44/vdcd) home automation controller.

*p44mbrd* connects vdcd-based devices from many technologies (DALI, EnOcean, hue, SmartLEDs, and many more) into the new [smart-home standard *matter*](https://buildwithmatter.com), making them available in matter-enabled smart home "oecosystems" such as Apple Home, Smart Things, Alexa, Google and hopefully many more independent ones in the near future.

## Implementation notes

### bridge API

*p44mbrd* uses a **JSON-based representation of the [vdc API](http://developer.digitalstrom.org/Architecture/vDC-API.pdf)** to communicate with *vdcd*.

The *vdc-API* was originally defined in 2013 as an abstraction for the [digitalSTROM](https://digitalstrom.com) device model to allow integration of third-party devices into digitalSTROM. The vdc API device model was designed to be as generic as possible, with a strong focus on being self-descriptive through a [structured tree of device properties](http://developer.digitalstrom.org/Architecture/vDC-API-properties.pdf).

This now helps *p44mbrd* to obtain the needed information from devices to be able to map them to *matter bridged device*.

### matter SDK - connectedhomeip

For the implementation of the matter standard, *p44mbrd* uses the official Apache 2 licensed matter SDK which is called [connectedhomeip](https://github.com/project-chip/connectedhomeip).

In particular, *p44mbrd* was evolved out of the *bridge-app* example in CHIP.

The *connectedhomeip* is included as a submodule, but **using a plan44 forked github repository**.

The forked version is currently needed because things like build support for OpenWrt or the libev based mainloop are not yet part of upstream *connectedhomeip*. In addition, the forked version omits a lot of very footprint heavy submodules (gigabytes!) containing SDKs for various embedded hardware not relevant to a Linux/Posix based bridge project.

However, the plan44 fork is not a real fork and will not diverge from *connectedhomeip* a lot. It's just a set of commits on top of *connectedhomeip* that will be rebased from time to time to new upstream releases, and hopefully over time get integrated into the SDK itself.

Work on *p44mbrd* has produced a few contributions already accepted to matter mainline:

- [make compatible with time64](https://github.com/project-chip/connectedhomeip/pull/19985)
- [fix System Layer socket watch for Darwin](https://github.com/project-chip/connectedhomeip/pull/21135)
- [a build option for custom-implemented clusters](https://github.com/project-chip/connectedhomeip/pull/22042)
- [Support for libev based mainloop](https://github.com/project-chip/connectedhomeip/pull/24232)
- [Fix Avahi based dns-sd implementation](https://github.com/project-chip/connectedhomeip/pull/26397)

And one draft pending (at the time of writing this):

- [dynamic endpoints: add automatic attr storage, instantiation from ZAP templates](https://github.com/project-chip/connectedhomeip/pull/28372). This one in particular will help building bridge apps *a lot*!

### p44utils

p44mbrd is also based on a set of generic C++ utility classes called [*p44utils*](https://github.com/plan44/p44utils), which provides basic mechanisms for mainloop-based, nonblocking I/O driven automation daemons, as well as a script language, [*p44script*](https://plan44.ch/p44-techdocs/en/#topics). p44utils is included as a submodule into this project.

## Early Beta - Work in Progress!

**This project is an early beta**! It is far from a certifiable (let alone certified) implementation, and has many gaps in functionality. Still, it works pretty nice already with Apple Home at the time of writing.

Expect a lot of changes, refactorings and additions in the next few months!

In particular, *connectedhomeip*'s (and underlying ZCL's) design is very much targeted at devices with a factory-defined static device structure defined at compile time, derived from a static data model laid out in the so-called *.zap file* generated by the *zap tool*.

A bridge and its devices however do not have a static structure, but one defined at run time. The *bridge-app* sample in the matter SDK shows some of the tricks needed to create so-called *dynamic endpoints* and implement their storage. *p44mbrd* in its current (October 2022) state is a heavily refactored and modularized version of *bridge-app*, but still using those rather arcane mechanisms available for *dynamic endpoints*.

Just recently (in the *connectedhomeip* `master` branch, not the `v1.0-branch`, a new example named `dynamic-bridge-app` has appeared which seems to address the subject in a less Q&D way than `bridge-app` did. Maybe there's things to learn from `dynamic-bridge-app`...

I also plan to look closer into *zcl/ember* implementation and maybe suggest or help extending it to better support dynamic endpoints from .zap templates instead of all the hand-woven metadata needed right now.


## License

*p44mbrd* is licensed under the GPLv3 License (see COPYING).

If that's a problem for your particular application, I am open to provide a commercial license, within the limits of the third party code included (in particular, *connectedhomeip*'s Apache 2 License) - please contact me at [luz@plan44.ch](mailto:luz@plan44.ch).


## Try it out

plan44.ch provides RaspberryPi images named P44-DSB-X and P44-LC-X which contain a complete OpenWrt, with vdcd and p44mbrd installed and fully configured as a digitalSTROM bridge or a standalone light controller with matter support. You can download one of these from [plan44.ch/automation/p44-dsb-x.php](https://plan44.ch/automation/p44-dsb-x.php) or [plan44.ch/automation/p44-lc-x.php](https://plan44.ch/automation/p44-lc-x.php), copy it to a SD Card and use it with a RPi B, B+, 2,3 and 4. Note that the downloaded images are not always the most current version, so use the built-in "check for update" to get the latest version. Details see https://plan44.ch/p44-techdocs/en/matter/beta_readme/.


## Build it for OpenWrt Linux

To cross-build for Openwrt, you can use the `openwrt_build.sh` as follows:

```bash
CHIPAPP_ROOT="/checkout/dir/of/p44mbrd"
BUILDROOT="/Volumes/CaseSens/openwrt-2"

cd "${CHIPAPP_ROOT}/src"
source third_party/connectedhomeip/scripts/activate.sh

../openwrt_build.sh --debug ${BUILDROOT} ${BUILDROOT}/.config
```

Notes:

- The example above builds for debugging (`--debug`), which does just produce the executable and then gives some hints how to proceed, assuming the [p44build](https://github.com/plan44/p44build) script is in use for managing openwrt builds.
- Without `--debug`, the script would try to copy a symbol-stripped version of the executable into a hardcoded (line 5, `OPENWRT_WRAPPER_PACKAGE`) package subdirectory, to allow an openwrt package makefile pick the prebuilt executable from there - all this in lack of having figured out building all of matter completely from within the openwrt build system.
- `openwrt_build.sh` only works for mips and arm architectures at this time, and is actually tested with MT768x (Onion Omega2) and BCM27xx (RaspberryPi) targets.
- the second argument of `openwrt_build.sh` points to the current config of the same openwrt tree as specified in the first argument in the example above, but could point to any .config copy for which the openwrt tree has a compiled toolchain.

## Build it for regular Linux

On a debian 11 system with sudo, git, build-essential already installed:

### additional packages needed by matter/connectedhomeip

```bash
sudo apt install ninja-build python3-venv python3-dev python3-gevent python3-pip
sudo apt install unzip libgirepository1.0-dev
sudo apt install libgirepository-1.0-1 python3-gevent
sudo apt install libev-dev
sudo apt install libssl-dev libdbus-1-dev libglib2.0-dev libavahi-client-dev
```

### additional packages needed by p44utils

```bash
sudo apt install libboost-dev libjson-c-dev
```

### clone p44mbrd

```bash
git clone https://github.com/plan44/p44mbrd.git
cd p44mbrd
export CHIPAPP_ROOT=$(pwd)
git submodule init
git submodule update
# doing connectedhomeip submodules separately
git submodule update --init --recursive
```

### activate for build

```bash
export CHIPAPP_NAME="p44mbrd"
cd "${CHIPAPP_ROOT}/src"
source third_party/connectedhomeip/scripts/activate.sh
```

### build

```bash
export OUT_DIR="out/linux/release"
mkdir -p ${OUT_DIR}
gn gen \
    --fail-on-unused-args \
    --export-compile-commands \
    --root=${CHIPAPP_ROOT}/src \
    "--args=chip_enable_openthread=false chip_enable_wifi=false" \
    ${OUT_DIR}
ninja -C ${OUT_DIR} ${CHIPAPP_NAME}
```

If build fails with error complaining about wrong zap-cli version, a full re-run of the matter activation (=bootstrap) might help. This should cause cipd to download and install the correct zap version (but takes a long time).

```bash
source third_party/connectedhomeip/scripts/bootstrap.sh
```

**Temporary Hack:** If the build fails because of missing `src/zap/p44mbrd.matter` file: This should be generated from the .zap file. For some reason, the current gn build setup (probably bug in our BUILD.gn) does *not generate that file automatically*. Thus, for now, `p44mbrd.matter` is included in the repo, but when it is missing or .zap has been modified, it needs to be regenerated:

```bash
${CHIPAPP_ROOT}/src/third_party/connectedhomeip/scripts/tools/zap/generate.py ${CHIPAPP_ROOT}/src/zap/p44mbrd.zap
```

## run it

Test if it loads ok (all libraries there):

```bash
${OUT_DIR}/p44mbrd --help
```

For real operation you can run *p44mbrd* with:

```bash
${OUT_DIR}/p44mbrd --payloadversion 0 --vendor-id 0xFFF1 --product-id 0x8002 --setuppin 20202021 --discriminator 3842 --KVS /file/to/store/data --chiploglevel 2 --loglevel 5
```

Note that at this time, all this **is not certified by the csa-iot** so must use `vendor-id` and `product-id` as shown above. These are the test vendor and bridge-app product ids from the SDK.
The `discriminator` should be set to a device-unique value to allow commissioning in a network with multiple bridge-app or p44mbrd instances.

You also need to have a [*vdcd*](https://github.com/plan44/vdcd) running on the same host providing the *bridge-api* on port 4444 (default, you can define other bridge API ports, allow non-local access with *vdcd* command line options, see `--help`, and you can tell *p44mbrd* using `--bridgeapihost` and `--bridgeapiport` where to look for the bridge API.

## Support p44mbrd

1. use it!
2. support development via [github sponsors](https://github.com/sponsors/plan44) or [flattr](https://flattr.com/@luz)
3. Discuss it in the [plan44 community forum](https://forum.plan44.ch/t/matter).
3. contribute patches, report issues and suggest new functionality [on github](https://github.com/plan44/p44mbrd) or in the [forum](https://forum.plan44.ch/t/opensource-c-vdcd).
4. build cool new device integrations and contribute those
5. Buy plan44.ch [products](https://plan44.ch/automation/products.php) - sales revenue is paying the time for contributing to opensource projects :-)

*(c) 2022-2023 by Lukas Zeller / [plan44.ch](http://www.plan44.ch/automation)*







