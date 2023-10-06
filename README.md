p44mbrd
=======

*[[if you want to support p44mbrd development, please consider to sponsor plan44]](https://github.com/sponsors/plan44)*

*p44mbrd* (plan44 matter bridge daemon) is a free (opensource, GPLv3) *matter* bridge daemon, originally developed as a companion to the [**vdcd**](https://github.com/plan44/vdcd) home automation controller (which controls devices of many different technologies (DALI, EnOcean, hue, SmartLEDs, and many more). By now, *p44mbrd* has been extended to access devices via *bridge adapters*, of which the *vdcd bridge API* is only one. In parallel, development of a second *bridge adapter* for the Becker-Antriebe "CentralControl" *cc API* is in progress.

*p44mbrd* connects devices from its *bridge adapters* into the new [smart-home standard *matter*](https://buildwithmatter.com), making them available in matter-enabled smart home "oecosystems" such as Apple Home, Smart Things, Alexa, Google and hopefully many more independent ones in the near future.

## Implementation notes

### vdcd bridge adapter

*p44mbrd* uses a **JSON-based representation of the [vdc API](http://developer.digitalstrom.org/Architecture/vDC-API.pdf)** to communicate with *vdcd*.

The *vdc-API* was originally defined in 2013 as an abstraction for the [digitalSTROM](https://digitalstrom.com) device model to allow integration of third-party devices into digitalSTROM. The vdc API device model was designed to be as generic as possible, with a strong focus on being self-descriptive through a [structured tree of device properties](http://developer.digitalstrom.org/Architecture/vDC-API-properties.pdf).

This now helps *p44mbrd* to obtain the needed information from devices to be able to map them to *matter bridged device*.

### cc bridge adapter

The *cc bridge adapter* uses a JSON-RPC 2.0 based API to access "CentralControl" devices and map their functionality into matter, in particular the WindowCovering device type.

### matter SDK - connectedhomeip

For the implementation of the matter standard, *p44mbrd* uses the official Apache 2 licensed matter SDK which is called [connectedhomeip](https://github.com/project-chip/connectedhomeip).

In particular, *p44mbrd* was evolved out of the *bridge-app* example in CHIP.

The *connectedhomeip* is included as a submodule, but **using a plan44 forked github repository**.

A forked version is currently required because not all needed additions to the SDK are already accepted (or maybe acceptable) upstream at the time of writing this.

In addition, the forked version contains an adapted `.gitmodule` file which prevents fetching a lot of very footprint heavy submodules (gigabytes!) containing SDKs for various embedded hardware not relevant to a Linux/Posix based bridge project (by adding `update = none` and `ignore = all`).

However, **the plan44 fork is not a real fork** and will not diverge from *connectedhomeip* a lot. It's just a set of commits on top of *connectedhomeip* that **will be rebased from time to time** to new upstream releases, and hopefully over time get integrated into the SDK itself.

Work on *p44mbrd* has produced a few contributions already accepted to matter mainline:

- [make compatible with time64](https://github.com/project-chip/connectedhomeip/pull/19985)
- [fix System Layer socket watch for Darwin](https://github.com/project-chip/connectedhomeip/pull/21135)
- [a build option for custom-implemented clusters](https://github.com/project-chip/connectedhomeip/pull/22042)
- [Support for libev based mainloop](https://github.com/project-chip/connectedhomeip/pull/24232)
- [Fix Avahi based dns-sd implementation](https://github.com/project-chip/connectedhomeip/pull/26397)

And two pending (at the time of writing this):

- [dynamic endpoints: add automatic attr storage, instantiation from ZAP templates](https://github.com/project-chip/connectedhomeip/pull/28372). This one in particular will help building bridge apps *a lot*!
- [external build systems: scripts/configure: allow out-of-tree project root](https://github.com/project-chip/connectedhomeip/pull/29627). This is a small contribution to the work done in the [matter-openwrt](https://github.com/project-chip/matter-openwrt) context which is extremely useful for building matter SDK based projects from within third-party build systems (not only openwrt!).

### p44utils

p44mbrd is also based on a set of generic C++ utility classes called [*p44utils*](https://github.com/plan44/p44utils), which provides basic mechanisms for mainloop-based, nonblocking I/O driven automation daemons, as well as a script language, [*p44script*](https://plan44.ch/p44-techdocs/en/#topics). p44utils is included as a submodule into this project.

## Beta - Work in Progress!

**This project is still beta**! It is not yet certified, and has some gaps in functionality. Still, it works pretty nice already with Apple Home, Smartthings, Google and maybe others for lights, color lights, plugin switches, window coverings, and some sensors  at the time of writing.

So expect a changes and additions in the next few months!

## License

*p44mbrd* is licensed under the GPLv3 License (see COPYING).

If that's a problem for your particular application, I am open to provide a commercial license, within the limits of the third party code included (in particular, *connectedhomeip*'s Apache 2 License) - please contact me at [luz@plan44.ch](mailto:luz@plan44.ch).


## Try it out

plan44.ch provides RaspberryPi images named P44-DSB-X and P44-LC-X which contain a complete OpenWrt, with vdcd and p44mbrd installed and fully configured as a digitalSTROM bridge or a standalone light controller with matter support. You can download one of these from [plan44.ch/automation/p44-dsb-x.php](https://plan44.ch/automation/p44-dsb-x.php) or [plan44.ch/automation/p44-lc-x.php](https://plan44.ch/automation/p44-lc-x.php), copy it to a SD Card and use it with a RPi B, B+, 2,3 and 4. Note that the downloaded images are not always the most current version, so use the built-in "check for update" to get the latest version. Details see https://plan44.ch/p44-techdocs/en/matter/beta_readme/.


## Build it for OpenWrt Linux

To build p44mbrd for Openwrt (22.03.5 at this time), see the [p44mbrd](https://github.com/plan44/plan44-feed/tree/main/p44mbrd) package in the [plan44 public openwrt feed](https://github.com/plan44/plan44-feed).

From within openwrt buildroot:

```bash
# add the matter-openwrt feed
echo "src-git --force matter https://github.com/project-chip/matter-openwrt.git" >>feeds.conf
 
# add the plan44 openwrt feed
echo "src-git plan44 https://github.com/plan44/plan44-feed.git;main" >>feeds.conf

# install the gn and p44mbrd packages
./scripts/feeds install -p matter gn
./scripts/feeds install -p plan44 p44mbrd

# enable `gn` in menuconfig under `Development`
# enabled `p44mbrd` in menuconfig under `plan44->products->p44mbrd`
make menuconfig
# - exit and save

# build gn
make package/gn/host/compile

# build p44mbrd
make package/p44mbrd/compile
```

Notes:

- This should work for all targets with architectures the matter SDK can be built for. At this time, it is actually tested only for `ramips` (Onion Omega) and `bcm27xx` (Raspberry Pi).
- The p44mbrd package includes `runit` control scripts and "factory data" for running p44mbrd with the identity and certificates of a (uncertified) matter test device.

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
${OUT_DIR}/p44mbrd --factorydata ${CHIPAPP_ROOT}/factory_data_0xFFF1_0x8002.txt --discriminator 3842 --KVS /file/to/store/data --chiploglevel 2 --loglevel 5
```

Note that at this time, all this **is not certified by the csa-iot** so factorydata
specifies a default set (included at project root as `factory_data_0xFFF1_0x8002.txt`), containing connectedhomeip SDK's data for development vendor 0xFFF1 and development product 0x8002. These are the test vendor and bridge-app product ids from the SDK.
The `--discriminator` should be set to a device-unique value to allow commissioning in a network with multiple bridge-app or p44mbrd instances.

You also need to have a [*vdcd*](https://github.com/plan44/vdcd) running on the same host providing the *bridge-api* on port 4444 (default, you can define other bridge API ports, allow non-local access with *vdcd* command line options, see `--help`, and you can tell *p44mbrd* using `--p44apihost` and `--p44apiport` where to look for the bridge API.

Or, if you have a CC41 bridge, you can tell *p44mbrd* using `--ccapihost` and `--ccapiport` where to look for the CC41 API.

## Support p44mbrd

1. use it!
2. support development via [github sponsors](https://github.com/sponsors/plan44) or [flattr](https://flattr.com/@luz)
3. Discuss it in the [plan44 community forum](https://forum.plan44.ch/t/matter).
3. contribute patches, report issues and suggest new functionality [on github](https://github.com/plan44/p44mbrd) or in the [forum](https://forum.plan44.ch/t/opensource-c-vdcd).
4. build cool new device integrations and contribute those
5. Buy plan44.ch [products](https://plan44.ch/automation/products.php) - sales revenue is paying the time for contributing to opensource projects 😀

*(c) 2022-2023 by Lukas Zeller / [plan44.ch](http://www.plan44.ch/automation)*
