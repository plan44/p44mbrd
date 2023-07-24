//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2022 plan44.ch / Lukas Zeller, Zurich, Switzerland
//  based on Apache v2 licensed bridge-app example code (c) 2021 Project CHIP Authors
//
//  Author: Lukas Zeller <luz@plan44.ch>
//
//  This file is part of p44mbrd.
//
//  p44mbrd is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  p44mbrd is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with p44mbrd. If not, see <http://www.gnu.org/licenses/>.
//


#include <platform/CHIPDeviceLayer.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/PlatformManager.h>

#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>

#include <app/EventLogging.h>
//#include <app/chip-zcl-zpro-codec.h>
#include <app/reporting/reporting.h>
#include <app/util/af-types.h>
#include <app/util/af.h>
#include <app/util/attribute-storage.h>
//#include <app/util/attribute-table.h>
#include <app/util/util.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <lib/core/CHIPError.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/ZclString.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CommissionableDataProvider.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#include <setup_payload/SetupPayload.h>

// Network commissioning cluster support
#include <app/clusters/network-commissioning/network-commissioning.h>
#if CHIP_DEVICE_LAYER_TARGET_LINUX
  #include <platform/Linux/NetworkCommissioningDriver.h>
#endif
#if CHIP_DEVICE_LAYER_TARGET_DARWIN
  #include <platform/Darwin/NetworkCommissioningDriver.h>
#endif

#include <pthread.h>
#include <sys/ioctl.h>

#include <CommissionableInit.h>

#include "p44mbrd_main.h"

// p44mbrd specific includes
#include "chip_glue/chip_error.h"
#include "chip_glue/deviceinfoprovider.h"

#include "device.h"
#include "deviceonoff.h"
#include "devicelevelcontrol.h"
#include "devicecolorcontrol.h"
#include "sensordevices.h"
#include "booleaninputdevices.h"

#include <app/server/Server.h>

#include <cassert>
#include <iostream>
#include <vector>

// Device implementation adapters
#if P44_ADAPTERS
  #include "adapters/p44/p44bridge.h"
#endif // P44_ADAPTERS
#if CC51_ADAPTERS
  #include "adapters/cc51/cc51bridge.h"
#endif // CC51_ADAPTERS


using namespace chip;
using namespace chip::Credentials;
using namespace chip::Inet;
using namespace chip::Transport;
using namespace chip::DeviceLayer;
using namespace chip::app::Clusters;

// TODO: take this somehow out of generated ZAP
// For now: Endpoint 1 must be the Matter Bridge (Endpoint 0 is the Root node)
#define MATTER_BRIDGE_ENDPOINT 1

// chipmain

#define kP44mbrNamespace "/ch.plan44.p44mbrd/"

#include <platform/TestOnlyCommissionableDataProvider.h>
#include <app/server/OnboardingCodesUtil.h>
#include <system/SystemLayerImpl.h>
#include <providers/DeviceInfoProviderImpl.h>

#if CHIP_CONFIG_TRANSPORT_TRACE_ENABLED
#include <tracing/TraceDecoder.h>
#include <tracing/TraceHandlers.h>
#endif // CHIP_CONFIG_TRANSPORT_TRACE_ENABLED

using namespace p44;


// MARK: - P44mbrd Application based on p44utils CmdLineApp

#define P44_DEFAULT_BRIDGE_SERVICE "4444"
#define CC51_DEFAULT_BRIDGE_SERVICE "4343"


/// Main program for plan44.ch P44-DSB-DEH in form of the "vdcd" daemon)
class P44mbrd : public CmdLineApp
{
  typedef CmdLineApp inherited;

  // CHIP "globals"
  bool mChipAppInitialized;
  LinuxCommissionableDataProvider mCommissionableDataProvider;
  chip::DeviceLayer::DeviceInfoProviderImpl mExampleDeviceInfoProvider; // TODO: example? do we need our own?
  P44mbrdDeviceInfoProvider mP44dbrDeviceInstanceInfoProvider; ///< our own device **instance** info provider

  // Bridged devices info
  Device * mDevices[CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT];
  EndpointId mNumDynamicEndPoints;
  EndpointId mFirstFreeEndpointId;

  // Network commissioning
  #if CHIP_DEVICE_LAYER_TARGET_LINUX
  chip::DeviceLayer::NetworkCommissioning::LinuxEthernetDriver mEthernetDriver;
  #endif // CHIP_DEVICE_LAYER_TARGET_LINUX
  #if CHIP_DEVICE_LAYER_TARGET_DARWIN
  chip::DeviceLayer::NetworkCommissioning::DarwinEthernetDriver mEthernetDriver;
  #endif // CHIP_DEVICE_LAYER_TARGET_DARWIN
  chip::app::Clusters::NetworkCommissioning::Instance mEthernetNetworkCommissioningInstance;

  // implementation adapters
  typedef std::list<BridgeAdapter*> BridgeAdaptersList;
  BridgeAdaptersList mAdapters;
  int mUnstartedAdapters;

public:

  P44mbrd() :
    mChipAppInitialized(false),
    mNumDynamicEndPoints(0),
    mFirstFreeEndpointId(kInvalidEndpointId),
    mEthernetNetworkCommissioningInstance(0, &mEthernetDriver)
  {
  }


  string logContextPrefix() override
  {
    return "P44mbrd App";
  }


  virtual int main(int argc, char **argv) override
  {
    const char *usageText =
      "Usage: ${toolname} [options]\n";

    const CmdLineOptionDescriptor options[] = {
      // Original CHIP command line args
      { 0, "vendor-id",           true, "vendorid;vendor ID as assigned by the csa-iot" },
      { 0, "product-id",          true, "productid;product ID as specified by vendor" },
      { 0, "custom-flow",         true, "flow;commissioning flow: Standard = 0, UserActionRequired = 1, Custom = 2" },
      { 0, "payloadversion",      true, "version;The version indication provides versioning of the setup payload (default is 0)" },
      { 0, "discriminator",       true, "discriminator;a 12-bit unsigned integer to advertise during commissioning" },
      { 0, "setuppin",            true, "pincode;A 27-bit unsigned integer, which serves as proof of possession during commissioning.\n"
                                        "If not provided to compute a verifier, the --spake2p-verifier must be provided." },
      { 0, "spake2p-verifier",    true, "b64_PASE_verifier;A raw concatenation of 'W0' and 'L' (67 bytes) as base64 to override the verifier\n"
                                            "Auto-computed from the passcode, if provided" },
      { 0, "spake2p-salt",        true, "b64_PASE_salt;16-32 bytes of salt to use for the PASE verifier, as base64. If omitted, will be generated randomly" },
      { 0, "spake2p-iterations",  true, "iterations;Number of PBKDF iterations to use." },
      { 0, "matter-tcp-port",     true, "port;matter TCP port (secured)" },
      { 0, "matter-udp-port",     true, "arg;matter UDP port (unsecured)" },
      { 0, "interface",           true, "interface name;The network interface name to advertise on. Must have IPv6 link local address. If not set, first network interface with Ipv6 link local is used." },
      { 0, "PICS",                true, "filepath;A file containing PICS items" },
      { 0, "KVS",                 true, "filepath;A file to store Key Value Store items" },
      #if CHIP_CONFIG_TRANSPORT_TRACE_ENABLED
      { 0, "trace_file",          true, "filepath;Output trace data to the provided file." },
      { 0, "trace_log",           false, "enables traces to go to the log" },
      { 0, "trace_decode",        false, "enables traces decoding" },
      #endif // CHIP_CONFIG_TRANSPORT_TRACE_ENABLED
      // p44mbrd command line args
      #if P44_ADAPTERS
      // - P44 device implementations
      { 0, "bridgeapihost",       true, "host;host of the p44 bridge API" },
      { 0, "bridgeapiservice",    true, "port;port of the p44 bridge API, default is " P44_DEFAULT_BRIDGE_SERVICE },
      #endif
      #if CC51_ADAPTERS
      // - CC51 device implementations
      { 0, "cc51apihost",         true, "host;host of the bridge API" },
      { 0, "cc51apiservice",      true, "port;port of the bridge API, default is " CC51_DEFAULT_BRIDGE_SERVICE },
      #endif // CC51_ADAPTERS
      #if CHIP_LOG_FILTERING
      { 0, "chiploglevel",        true, "loglevel;level of detail for logging (0..4, default=2=Progress)" },
      #endif // CHIP_LOG_FILTERING
      DAEMON_APPLICATION_LOGOPTIONS,
      CMDLINE_APPLICATION_STDOPTIONS,
      CMDLINE_APPLICATION_PATHOPTIONS,
      { 0, NULL } // list terminator
    };

    // parse the command line, exits when syntax errors occur
    setCommandDescriptors(usageText, options);
    if (parseCommandLine(argc, argv)) {
      // parsed ok, app not terminated
      processStandardLogOptions(true, LOG_ERR);
      /* NOP at this time */
    }
    // app now ready to run (or cleanup when already terminated by cmd line parsing)
    return run();
  }


  virtual void cleanup(int aExitCode) override
  {
    // close bridge API connections
    for (BridgeAdaptersList::iterator pos = mAdapters.begin(); pos!=mAdapters.end(); ++pos) {
      (*pos)->cleanup();
    }
    // cleanup chip app
    chipAppCleanup();
  }


  void initAdapters()
  {
    #if P44_ADAPTERS
    const char* p44apihost = nullptr;
    const char* p44apiservice = P44_DEFAULT_BRIDGE_SERVICE;
    getStringOption("bridgeapihost", p44apihost);
    getStringOption("bridgeapiservice", p44apiservice);
    if (p44apihost) {
      P44_BridgeImpl* p44bridgeP = &P44_BridgeImpl::adapter();
      p44bridgeP->setAPIParams(p44apihost, p44apiservice);
      mAdapters.push_back(p44bridgeP);
    }
    #endif // P44_ADAPTERS
    #if CC51_ADAPTERS
    const char* cc51apihost = nullptr;
    const char* cc51apiservice = CC51_DEFAULT_BRIDGE_SERVICE;
    getStringOption("cc51apihost", cc51apihost);
    getStringOption("cc51apiservice", cc51apiservice);
    if (cc51apihost) {
      CC51_BridgeImpl* cc51bridgeP = &CC51_BridgeImpl::adapter();
      cc51bridgeP->setAPIParams(cc51apihost, cc51apiservice);
      mAdapters.push_back(cc51bridgeP);
    }
    #endif // CC51_ADAPTERS
  }


  void updateCommissionableStatus(bool aIsCommissionable)
  {
    for (BridgeAdaptersList::iterator pos = mAdapters.begin(); pos!=mAdapters.end(); ++pos) {
      (*pos)->setCommissionable(aIsCommissionable);
    }
  }


  void updateCommissioningInfo(const string aQRCodeData, const string aManualPairingCode)
  {
    for (BridgeAdaptersList::iterator pos = mAdapters.begin(); pos!=mAdapters.end(); ++pos) {
      (*pos)->updateCommissioningInfo(aQRCodeData, aManualPairingCode);
    }
  }


  void updateRunningStatus(bool aRunning)
  {
    for (BridgeAdaptersList::iterator pos = mAdapters.begin(); pos!=mAdapters.end(); ++pos) {
      (*pos)->setBridgeRunning(aRunning);
    }
  }


  CHIP_ERROR installAdaptersInitialDevices()
  {
    CHIP_ERROR chiperr;

    for (BridgeAdaptersList::iterator apos = mAdapters.begin(); apos!=mAdapters.end(); ++apos) {
      for (BridgeAdapter::DeviceUIDMap::iterator dpos = (*apos)->mDeviceUIDMap.begin(); dpos!=(*apos)->mDeviceUIDMap.end(); ++dpos) {
        DevicePtr dev = dpos->second;
        // install the device
        chiperr = installBridgedDevice(dev);
      }
    }
    return chiperr;
  }




  void installAdditionalDevice(DevicePtr aDev)
  {
    if (aDev) {
      if (!mChipAppInitialized) {
        // we are still waiting for the first bridged device and haven't started CHIP yet -> start now
        OLOG(LOG_NOTICE, "First bridgeable device installed, can start CHIP now, finally");
        // starting chip will take care of actually installing the device
        // - but unwind call stack before
        MainLoop::currentMainLoop().executeNow(boost::bind(&P44mbrd::startChip, this));
      }
      else {
        // already running
        installBridgedDevice(aDev);
        // save possibly modified first free endpointID (increased when previously unknown devices have been added) 
        chip::DeviceLayer::PersistedStorage::KeyValueStoreManager &kvs = chip::DeviceLayer::PersistedStorage::KeyValueStoreMgr();
        CHIP_ERROR chiperr = kvs.Put(kP44mbrNamespace "firstFreeEndpointId", mFirstFreeEndpointId);
        LogErrorOnFailure(chiperr);
        // add as new endpoint to bridge
        if (aDev->AddAsDeviceEndpoint()) {
          POLOG(aDev, LOG_NOTICE, "added as additional dynamic endpoint while CHIP is already up");
          aDev->didBecomeOperational();
          // dump status
          POLOG(aDev, LOG_INFO, "initialized from chip: %s", aDev->description().c_str());
          // also add subdevices that are part of this additional device and thus not yet present
          for (DevicesList::iterator pos = aDev->subDevices().begin(); pos!=aDev->subDevices().end(); ++pos) {
            DevicePtr subDev = *pos;
            if (subDev->AddAsDeviceEndpoint()) {
              POLOG(subDev, LOG_NOTICE, "added as part of composed device as additional dynamic endpoint while CHIP is already up");
              subDev->didBecomeOperational();
              // dump status
              POLOG(subDev, LOG_INFO, "initialized composed device part from chip: %s", aDev->description().c_str());
            }
          }
        }
      }
    }
  }



  void adapterStarted(ErrorPtr aError, BridgeAdapter &aAdapter)
  {
    mUnstartedAdapters--; // count this start
    if (Error::notOK(aError)) OLOG(LOG_WARNING, "Adapter startup error: %s", aError->text());
    if (mUnstartedAdapters>0) {
      OLOG(LOG_NOTICE, "Adapter started, %d remaining", mUnstartedAdapters);
    }
    else {
      // all adapters have called back, so we can consider all started (or not operational at all)
      bool startnow = false;
      bool infoset = false;
      for (BridgeAdaptersList::iterator pos = mAdapters.begin(); pos!=mAdapters.end(); ++pos) {
        // for now: assume we'll have only one adapter running for real application,
        // which will determine the matter bridge's identification.
        // With multiple adapters, the first instantiated will determine the device instance info
        if (!infoset) {
          infoset = true;
          mP44dbrDeviceInstanceInfoProvider.mUID = (*pos)->UID();
          mP44dbrDeviceInstanceInfoProvider.mLabel = (*pos)->label();
          mP44dbrDeviceInstanceInfoProvider.mSerial = (*pos)->serial();
          mP44dbrDeviceInstanceInfoProvider.mProductName = (*pos)->model();
          mP44dbrDeviceInstanceInfoProvider.mVendorName = (*pos)->vendor();
        }
        if ((*pos)->hasBridgeableDevices()) {
          startnow = true;
        }
      }
      // initial devices collected
      if (!startnow) {
        OLOG(LOG_WARNING, "Bridge has no devices yet, NOT starting CHIP now, waiting for first device to appear");
      }
      else {
        // start chip only now
        OLOG(LOG_NOTICE, "End of bridge adapter setup, starting CHIP now");
        // - start but unwind call stack before
        MainLoop::currentMainLoop().executeNow(boost::bind(&P44mbrd::startChip, this));
      }
    }
  }


  void addDevice(DevicePtr aDevice)
  {
    installAdditionalDevice(aDevice);
  }


  virtual void initialize() override
  {
    OLOG(LOG_NOTICE, "p44: p44utils mainloop started");
    // Instantiate and initialize adapters
    initAdapters();
    // start the adapters
    mUnstartedAdapters = (int)mAdapters.size();
    for (BridgeAdaptersList::iterator pos = mAdapters.begin(); pos!=mAdapters.end(); ++pos) {
      (*pos)->startup(boost::bind(&P44mbrd::adapterStarted, this, _1, _2), boost::bind(&P44mbrd::addDevice, this, _1));
    }
  }



  void startChip()
  {
    ErrorPtr err;
    if (mChipAppInitialized) {
      err = TextError::err("trying to call chipAppInit() a second time");
    }
    else {
      err = chipAppInit();
    }
    if (Error::notOK(err)) {
      OLOG(LOG_ERR, "chipAppInit failed: %s ", err->text());
      terminateApp(EXIT_FAILURE);
      return;
    }
    // update commissionable status in adapters
    bool commissionable = Server::GetInstance().GetFabricTable().FabricCount() == 0;
    updateCommissionableStatus(commissionable);
    // install the devices we have
    installInitiallyBridgedDevices();
    // stack is now operational
    stackDidBecomeOperational();
  }


  #ifndef MIGRATE_DYNAMIC_ENDPOINT_IDX_KVS
    #define MIGRATE_DYNAMIC_ENDPOINT_IDX_KVS 1
  #endif

  #if MIGRATE_DYNAMIC_ENDPOINT_IDX_KVS
  static const EndpointId kLegacyFirstDynamicEndpointID = 3;
  #endif

  CHIP_ERROR installSingleBridgedDevice(DevicePtr dev, chip::EndpointId aParentEndpointId)
  {
    CHIP_ERROR chiperr;

    // check if we can add any new devices at all
    if (mNumDynamicEndPoints>=CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT) {
      POLOG(dev, LOG_ERR, "No free endpoint available - all %d dynamic endpoints are occupied -> cannot add new device", CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT);
      return CHIP_ERROR_NO_ENDPOINT;
    }
    // signal installation to device (which, at this point, is a fully constructed class)
    dev->willBeInstalled();
    // now install
    chip::DeviceLayer::PersistedStorage::KeyValueStoreManager &kvs = chip::DeviceLayer::PersistedStorage::KeyValueStoreMgr();
    // note: endpointUID identifies the endpoint, of which one adapted device might have multiple.
    string key = kP44mbrNamespace "device_eps/"; key += dev->deviceInfoDelegate().endpointUID();
    EndpointId endpointId = kInvalidEndpointId;
    #if MIGRATE_DYNAMIC_ENDPOINT_IDX_KVS
    // need to look up in the old table first
    string legacy_key = kP44mbrNamespace "devices/"; legacy_key += dev->deviceInfoDelegate().endpointUID(); // old endpointUID -> dynamicEndpointIdx mapping
    EndpointId legacyDynamicEndpointIdx = kInvalidEndpointId;
    chiperr = kvs.Get(legacy_key.c_str(), &legacyDynamicEndpointIdx);
    if (chiperr==CHIP_NO_ERROR) {
      // this device was mapped before with the fixed index/endpointID scheme
      // calculate the endpointId
      endpointId = legacyDynamicEndpointIdx + kLegacyFirstDynamicEndpointID;
      // migrate kvs entry into new format
      chiperr = kvs.Delete(legacy_key.c_str()); // delete from legacy
      LogErrorOnFailure(chiperr);
      chiperr = kvs.Put(key.c_str(), endpointId); // store in new
      LogErrorOnFailure(chiperr);
      POLOG(dev, LOG_NOTICE, "migrated KVS entry from legacy dynamicEndpointIdx (%u) to endpointId (%u)", legacyDynamicEndpointIdx, endpointId);
    }
    else if (chiperr!=CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND) {
      LogErrorOnFailure(chiperr);
    }
    else // not found in legacy store
    #endif
    {
      // lookup previously used endpointId
      chiperr = kvs.Get(key.c_str(), &endpointId);
      if (chiperr==CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND) {
        POLOG(dev, LOG_NOTICE, "is NEW (was not previously mapped to an endpoint) -> adding to bridge");
        endpointId = kInvalidEndpointId;
        chiperr = CHIP_NO_ERROR;
      }
      LogErrorOnFailure(chiperr);
    }
    if (endpointId!=kInvalidEndpointId) {
      // This UID was already assigned to an endpoint earlier - re-use the endpoint ID for this dSUID
      // - check for no colliding with fixed endpoints (in case we increase those over time)
      if (endpointId<emberAfFixedEndpointCount()) {
        POLOG(dev, LOG_WARNING, "This device's former endpoint (%d) is now occupied by a fixed endpoint", endpointId);
        endpointId = kInvalidEndpointId; // must assign a new one
      }
      else {
        // - check for being already in use by one of the devices already in the list
        //   Note: this does not happen, normally, but CAN happen once endpointIds wrap around at 0xFFFF
        for (size_t i=0; i<mNumDynamicEndPoints; i++) {
          if (mDevices[i]->GetEndpointId()==mFirstFreeEndpointId) {
            // the supposedly free next endpointID is in use
            if (++mFirstFreeEndpointId==0xFFFF) mFirstFreeEndpointId = emberAfEndpointCount(); // increment and wraparound from 0xFFFE to first dynamic endpoont
            POLOG(mDevices[i], LOG_WARNING, "is already using what was recorded as next free endpointID -> adjusted the latter to %d", mFirstFreeEndpointId);
          }
          if (mDevices[i]->GetEndpointId()==endpointId) {
            // this endpointId is already in use, must create a new one
            POLOG(dev, LOG_WARNING, "This device's former endpoint (%d) is already in use by '%s'", endpointId, mDevices[i]->logContextPrefix().c_str());
            endpointId = kInvalidEndpointId; // must assign a new one
            break;
          }
        }
      }
    }
    if (endpointId!=kInvalidEndpointId) {
      // has an endpoint id we can use
      POLOG(dev, LOG_NOTICE, "was previously mapped to endpoint #%d -> using same endpoint again", endpointId);
    }
    else {
      // must get a new endpointId
      endpointId = mFirstFreeEndpointId;
      POLOG(dev, LOG_NOTICE, "will be assigned new endpointId %d", endpointId);
      // save new UID<->endpointId relation
      chiperr = kvs.Put(key.c_str(), endpointId);
      LogErrorOnFailure(chiperr);
      // determine next free
      if (++mFirstFreeEndpointId==0xFFFF) mFirstFreeEndpointId = emberAfEndpointCount(); // increment and wraparound from 0xFFFE to first dynamic endpoint
    }
    // assign to dynamic endpoint array
    if (endpointId!=kInvalidEndpointId) {
      dev->SetEndpointId(endpointId);
      dev->SetDynamicEndpointIdx(mNumDynamicEndPoints);
      mDevices[mNumDynamicEndPoints] = dev.get();
      mNumDynamicEndPoints++;
      // also set parent endpoint id
      dev->SetParentEndpointId(aParentEndpointId);
    }
    return chiperr;
  }


  CHIP_ERROR installBridgedDevice(DevicePtr aDev)
  {
    CHIP_ERROR chiperr;
    // install base (or single) device
    chiperr = installSingleBridgedDevice(aDev, MATTER_BRIDGE_ENDPOINT);
    if (chiperr==CHIP_NO_ERROR) {
      // also add subdevices AFTER the main device (if any)
      for (DevicesList::iterator pos = aDev->subDevices().begin(); pos!=aDev->subDevices().end(); ++pos) {
        (*pos)->flagAsPartOfComposedDevice();
        installSingleBridgedDevice(*pos, aDev->GetEndpointId());
      }
    }
    return chiperr;
  }



  void installInitiallyBridgedDevices()
  {
    // Disable last fixed endpoint, which is used as a placeholder for all of the
    // supported clusters so that ZAP will generated the requisite code.
    emberAfEndpointEnableDisable(emberAfEndpointFromIndex(static_cast<uint16_t>(emberAfFixedEndpointCount() - 1)), false);
    // Clear out the array of dynamic endpoints
    mNumDynamicEndPoints = 0;
    memset(mDevices, 0, sizeof(mDevices));
    CHIP_ERROR chiperr;
    chip::DeviceLayer::PersistedStorage::KeyValueStoreManager &kvs = chip::DeviceLayer::PersistedStorage::KeyValueStoreMgr();
    // determine highest endpointId in use
    chiperr = kvs.Get(kP44mbrNamespace "firstFreeEndpointId", &mFirstFreeEndpointId);
    if (chiperr==CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND) {
      // no highest endpoint recorded so far
      #if MIGRATE_DYNAMIC_ENDPOINT_IDX_KVS
      // - obtain from legacy map
      char legacyDynamicEndPointMap[CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT+1];
      size_t nde;
      chiperr = kvs.Get(kP44mbrNamespace "endPointMap", legacyDynamicEndPointMap, CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT, &nde);
      if (chiperr==CHIP_NO_ERROR) {
        // legacy endpoint map (still) exists, find highest index used
        mFirstFreeEndpointId = emberAfFixedEndpointCount();
        for (EndpointId i=0; i<nde; i++) {
          if (legacyDynamicEndPointMap[i]!=' ') {
            if (i>=mFirstFreeEndpointId-kLegacyFirstDynamicEndpointID) {
              mFirstFreeEndpointId = i+kLegacyFirstDynamicEndpointID+1;
            }
          }
        }
        // forget the legacy map
        OLOG(LOG_NOTICE, "migrated endpoint kvs: determined first free endpointId as %d", mFirstFreeEndpointId);
        kvs.Put(kP44mbrNamespace "firstFreeEndpointId", mFirstFreeEndpointId); // safety saving, in case something goes wrong below before it is saved again
        kvs.Delete(kP44mbrNamespace "endPointMap");
      }
      else if (chiperr==CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND) {
        // that's ok
        chiperr = CHIP_NO_ERROR;
      }
      else {
        LogErrorOnFailure(chiperr);
      }
      #endif // MIGRATE_DYNAMIC_ENDPOINT_IDX_KVS
    }
    // validate
    if (mFirstFreeEndpointId==kInvalidEndpointId || mFirstFreeEndpointId<emberAfFixedEndpointCount()) {
      // none recorded or is invalid - reset and store
      mFirstFreeEndpointId = emberAfFixedEndpointCount(); // use first possible endpointId.
      kvs.Put(kP44mbrNamespace "firstFreeEndpointId", mFirstFreeEndpointId);
      OLOG(LOG_NOTICE, "reset first free endpointID to: %d", mFirstFreeEndpointId);
    }
    // process list of to-be-bridged devices of all adapters
    chiperr = installAdaptersInitialDevices();
    // save updated next free endpoint
    chiperr = kvs.Put(kP44mbrNamespace "firstFreeEndpointId", mFirstFreeEndpointId);
    LogErrorOnFailure(chiperr);
    // Add the devices as dynamic endpoints
    for (size_t i=0; i<mNumDynamicEndPoints; i++) {
      if (mDevices[i]->AddAsDeviceEndpoint()) {
        POLOG(mDevices[i], LOG_NOTICE, "added as dynamic endpoint before starting CHIP mainloop");
      }
    }
  }


  void stackDidBecomeOperational()
  {
    for (size_t i=0; i<CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT; i++) {
      if (mDevices[i]) {
        // give device chance to do things just after becoming operational
        mDevices[i]->didBecomeOperational();
        // dump status
        POLOG(mDevices[i], LOG_INFO, "initialized from chip: %s", mDevices[i]->description().c_str());
      }
    }
  }


  DevicePtr deviceForDynamicEndpointIndex(EndpointId aDynamicEndpointIndex)
  {
    DevicePtr dev;
    if (aDynamicEndpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT) {
      dev = mDevices[aDynamicEndpointIndex];
    }
    return dev;
  }


  // MARK: - chip stack setup

  static bool Base64ArgToVector(const char * arg, size_t maxSize, std::vector<uint8_t> & outVector)
  {
    size_t maxBase64Size = BASE64_ENCODED_LEN(maxSize);
    outVector.resize(maxSize);

    size_t argLen = strlen(arg);
    if (argLen > maxBase64Size) {
      return false;
    }
    size_t decodedLen = chip::Base64Decode32(arg, (uint32_t)argLen, reinterpret_cast<uint8_t *>(outVector.data()));
    if (decodedLen == 0) {
      return false;
    }
    outVector.resize(decodedLen);
    return true;
  }


  ErrorPtr InitCommissionableDataProvider(LinuxCommissionableDataProvider& aCommissionableDataProvider, chip::PayloadContents& aOnBoardingPayload)
  {
    chip::Optional<std::vector<uint8_t>> spake2pVerifier;
    chip::Optional<std::vector<uint8_t>> spake2pSalt;

    const char* s = nullptr;
    // spake2p-verifier
    if (getStringOption("spake2p-verifier", s)) {
      std::vector<uint8_t> s2pvec;
      if (Base64ArgToVector(s, chip::Crypto::kSpake2p_VerifierSerialized_Length, s2pvec)) {
        if (s2pvec.size()!=chip::Crypto::kSpake2p_VerifierSerialized_Length) {
          return TextError::err("--spake2p-verifier must be %zu bytes", chip::Crypto::kSpake2p_VerifierSerialized_Length);
        }
        spake2pVerifier.SetValue(s2pvec);
      }
      else {
        return TextError::err("invalid b64 in --spake2p-verifier");
      }
    }
    // spake2p-salt
    if (getStringOption("spake2p-salt", s)) {
      std::vector<uint8_t> saltvec;
      if (Base64ArgToVector(s, chip::Crypto::kSpake2p_Max_PBKDF_Salt_Length, saltvec)) {
        if (
          saltvec.size()>chip::Crypto::kSpake2p_Max_PBKDF_Salt_Length ||
          saltvec.size()<chip::Crypto::kSpake2p_Min_PBKDF_Salt_Length
        ) {
          return TextError::err("--spake2p-salt must be %zu..%zu bytes", chip::Crypto::kSpake2p_Min_PBKDF_Salt_Length, chip::Crypto::kSpake2p_Max_PBKDF_Salt_Length);
        }
        spake2pSalt.SetValue(saltvec);
      }
      else {
        return TextError::err("invalid b64 in --spake2p-salt");
      }
    }
    // spake2p-iterations
    unsigned int spake2pIterationCount = chip::Crypto::kSpake2p_Min_PBKDF_Iterations;
    getUIntOption("spake2p-iterations", spake2pIterationCount);
    if (spake2pIterationCount<chip::Crypto::kSpake2p_Min_PBKDF_Iterations || spake2pIterationCount>chip::Crypto::kSpake2p_Max_PBKDF_Iterations) {
      return TextError::err("--spake2p-iterations must be in range %u..%u", chip::Crypto::kSpake2p_Min_PBKDF_Iterations, chip::Crypto::kSpake2p_Max_PBKDF_Iterations);
    }
    // setup pincode
    chip::Optional<uint32_t> setUpPINCode;
    if (aOnBoardingPayload.setUpPINCode==0) {
      if (!spake2pVerifier.HasValue()) {
        return TextError::err("missing --setuppin or --spake2p-verifier");
      }
      // Passcode is 0, so will be ignored, and verifier will take over. Onboarding payload
      // printed for debug will be invalid, but if the onboarding payload had been given
      // properly to the commissioner later, PASE will succeed.
    }
    else {
      // assign to optional
      setUpPINCode.SetValue(aOnBoardingPayload.setUpPINCode);
    }

    return P44ChipError::err(aCommissionableDataProvider.Init(
      spake2pVerifier, spake2pSalt, spake2pIterationCount, setUpPINCode, aOnBoardingPayload.discriminator.GetLongValue()
    ));
  }


  ErrorPtr InitConfigurationManager(ConfigurationManagerImpl& aConfigManager, chip::PayloadContents& aOnBoardingPayload)
  {
    ErrorPtr err;
    // TODO: do we really need to store these IDs? For p44mbrd these should be commandline params only
    if (aOnBoardingPayload.vendorID != 0)
    {
      err = P44ChipError::err(aConfigManager.StoreVendorId(aOnBoardingPayload.vendorID));
      if (Error::notOK(err)) return err;
    }
    if (aOnBoardingPayload.productID != 0)
    {
      err = P44ChipError::err(aConfigManager.StoreProductId(aOnBoardingPayload.productID));
      if (Error::notOK(err)) return err;
    }
    return err;
  }


  ErrorPtr chipAppInit()
  {
    #if CHIP_LOG_FILTERING
    // set up the log level
    int chiplogmaxcategory = chip::Logging::kLogCategory_Progress;
    getIntOption("chiploglevel", chiplogmaxcategory);
    chip::Logging::SetLogFilter((uint8_t)chiplogmaxcategory);
    #endif // CHIP_LOG_FILTERING

    // MARK: basically reduced ChipLinuxAppInit() from here
    ErrorPtr err;

    // prepare the onboarding payload
    chip::PayloadContents onBoardingPayload;
    // - always on-network
    onBoardingPayload.rendezvousInformation.SetValue(RendezvousInformationFlag::kOnNetwork);
    // - use baked in product and vendor IDs if defined
    #ifdef CHIP_DEVICE_CONFIG_DEVICE_VENDOR_ID
    onBoardingPayload.vendorID = CHIP_DEVICE_CONFIG_DEVICE_VENDOR_ID;
    OLOG(LOG_WARNING, "May need to use hard-coded Vendor ID 0x%04X (set as default)", onBoardingPayload.vendorID);
    #endif
    #ifdef CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_ID
    onBoardingPayload.productID = CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_ID;
    OLOG(LOG_WARNING, "May need to use hard-coded Product ID 0x%04X (set as default)", onBoardingPayload.productID);
    #endif
    // - get from command line
    int i;
    if (getIntOption("payloadversion", i)) onBoardingPayload.version = (uint8_t)i;
    if (getIntOption("vendor-id", i)) onBoardingPayload.vendorID = (uint16_t)i;
    if (getIntOption("product-id", i)) onBoardingPayload.productID = (uint16_t)i;
    if (getIntOption("custom-flow", i)) onBoardingPayload.commissioningFlow = static_cast<CommissioningFlow>(i);
    if (getIntOption("discriminator", i)) onBoardingPayload.discriminator.SetLongValue(static_cast<uint16_t>(i & ((1<<12)-1)));
    if (getIntOption("setuppin", i)) onBoardingPayload.setUpPINCode = i & ((1<<27)-1);

    // TODO: avoid duplicating these, later
    mP44dbrDeviceInstanceInfoProvider.mVendorId = onBoardingPayload.vendorID;
    mP44dbrDeviceInstanceInfoProvider.mProductId = onBoardingPayload.productID;

    // TODO: remove this later - Safety check, we're still forced to use predefined testing IDs for now
    #ifdef CHIP_DEVICE_CONFIG_DEVICE_VENDOR_ID
    if (CHIP_DEVICE_CONFIG_DEVICE_VENDOR_ID!=onBoardingPayload.vendorID) {
      ChipLogError(DeviceLayer, "CHIP_DEVICE_CONFIG_DEVICE_VENDOR_ID (0x%04X) != command line vendor ID (0x%04X)", CHIP_DEVICE_CONFIG_DEVICE_VENDOR_ID, onBoardingPayload.vendorID);
    }
    #endif
    #ifdef CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_ID
    if (CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_ID!=onBoardingPayload.productID) {
      ChipLogError(DeviceLayer, "CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_ID (0x%04X) != command line product ID (0x%04X)", CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_ID, onBoardingPayload.productID);
    }
    #endif

    // memory init
    err = P44ChipError::err(Platform::MemoryInit());
    if (Error::notOK(err)) return err;

    // KVS path
    #ifdef CHIP_CONFIG_KVS_PATH
    const char* strP;
    if (getStringOption("KVS", strP)) {
      err = P44ChipError::err(DeviceLayer::PersistedStorage::KeyValueStoreMgrImpl().Init(strP));
    }
    else {
      err = P44ChipError::err(DeviceLayer::PersistedStorage::KeyValueStoreMgrImpl().Init(tempPath("chip_kvs").c_str()));
    }
    if (Error::notOK(err)) return err;
    #endif

    // IMPORTANT: pass the p44utils mainloop to the system layer!
    static_cast<System::LayerSocketsLoop &>(DeviceLayer::SystemLayer()).SetLibEvLoop(MainLoop::currentMainLoop().libevLoop());

    // chip stack init
    err = P44ChipError::err(DeviceLayer::PlatformMgr().InitChipStack());
    if (Error::notOK(err)) return err;

    // Init the commissionable data provider based on command line options and onboardingpayload
    err = InitCommissionableDataProvider(mCommissionableDataProvider, onBoardingPayload);
    if (Error::notOK(err)) return err;
    DeviceLayer::SetCommissionableDataProvider(&mCommissionableDataProvider);

    // Init the configuration manager
    err = InitConfigurationManager(reinterpret_cast<ConfigurationManagerImpl &>(ConfigurationMgr()), onBoardingPayload);
    if (Error::notOK(err)) return err;

// FIXME: we don't need to re-fetch this info here?? Should all be contained in onBoardingPayload already
//    if (LinuxDeviceOptions::GetInstance().payload.rendezvousInformation.HasValue()) {
//        rendezvousFlags = LinuxDeviceOptions::GetInstance().payload.rendezvousInformation.Value();
//    }
//
//    err = GetPayloadContents(LinuxDeviceOptions::GetInstance().payload, rendezvousFlags);
//    SuccessOrExit(err);

    // Show device config
    ConfigurationMgr().LogDeviceConfig();
    OLOG(LOG_NOTICE, "==== Onboarding payload for %s Commissioning Flow ====",
      onBoardingPayload.commissioningFlow == chip::CommissioningFlow::kStandard ? "STANDARD" : "USER-ACTION or CUSTOM"
    );
    PrintOnboardingCodes(onBoardingPayload);

    // let the bridged app know
    char payloadBuffer[chip::QRCodeBasicSetupPayloadGenerator::kMaxQRCodeBase38RepresentationLength + 1];
    chip::MutableCharSpan qrCode(payloadBuffer);
    chip::MutableCharSpan manualPairingCode(payloadBuffer);
    if (GetQRCode(qrCode, onBoardingPayload) == CHIP_NO_ERROR) {
      string manualParingCodeStr;
      if (GetManualPairingCode(manualPairingCode, onBoardingPayload) == CHIP_NO_ERROR) {
        manualParingCodeStr = manualPairingCode.data();
      }
      updateCommissioningInfo(qrCode.data(), manualParingCodeStr);
      // FIXME: figure out if actually commissionable or not
      updateCommissionableStatus(true /* fixed for now */);
    }

    // init the network commissioning instance (which is needed by the Network Commissioning Cluster)
    mEthernetNetworkCommissioningInstance.Init();

    #if CHIP_CONFIG_TRANSPORT_TRACE_ENABLED
    const char *s;
    if (getStringOption("trace_file", s)) {
      auto traceStream = new chip::trace::TraceStreamFile(s);
      chip::trace::AddTraceStream(traceStream);
    }
    else if (getOption("trace_log")) {
      auto traceStream = new chip::trace::TraceStreamLog();
      chip::trace::AddTraceStream(traceStream);
    }
    if (getOption("trace_decode")) {
      chip::trace::TraceDecoderOptions options;
      options.mEnableProtocolInteractionModelResponse = false;
      chip::trace::TraceDecoder * decoder = new chip::trace::TraceDecoder();
      decoder->SetOptions(options);
      chip::trace::AddTraceStream(decoder);
    }
    chip::trace::InitTrace();
    #endif // CHIP_CONFIG_TRANSPORT_TRACE_ENABLED


    // MARK: basically reduced ChipLinuxAppMainLoop() from here, without actually starting the mainloop

    // TODO: implement our own real DAC provider later
    SetDeviceAttestationCredentialsProvider(Examples::GetExampleDACProvider());

    // Set our own device info provider (before initializing server, which wants to see it installed)
    SetDeviceInstanceInfoProvider(&mP44dbrDeviceInstanceInfoProvider);

    // prepare the onboarding payload
    static chip::CommonCaseDeviceServerInitParams serverInitParams;
    VerifyOrDie(serverInitParams.InitializeStaticResourcesBeforeServerInit() == CHIP_NO_ERROR);
    serverInitParams.operationalServicePort        = CHIP_PORT;
    serverInitParams.userDirectedCommissioningPort = CHIP_UDC_PORT;
    if (getIntOption("matter-tcp-port", i)) serverInitParams.operationalServicePort = (uint16_t)i;
    if (getIntOption("matter-udp-port", i)) serverInitParams.userDirectedCommissioningPort = (uint16_t)i;
    const char* ifname;
    if (getStringOption("interface", ifname)) {
      VerifyOrDie(Inet::InterfaceId::InterfaceNameToId(ifname, serverInitParams.interfaceId) == CHIP_NO_ERROR);
    }

    // We need to set DeviceInfoProvider before Server::Init to setup the storage of DeviceInfoProvider properly.
    DeviceLayer::SetDeviceInfoProvider(&mExampleDeviceInfoProvider);

    // Init ZCL Data Model and CHIP App Server
    Server::GetInstance().Init(serverInitParams);

    // prepare the storage delegate
    mExampleDeviceInfoProvider.SetStorageDelegate(&chip::Server::GetInstance().GetPersistentStorage());

    #ifdef __APPLE__
    // we need the dispatch queue for DnsSD, even if the mainloop runs on libev
    DeviceLayer::PlatformMgr().StartEventLoopTask();
    #endif

    // done, ready to run
    mChipAppInitialized = true;
    // let adapters know
    updateRunningStatus(true);
    return err;
  }


  void chipAppCleanup()
  {
    if (mChipAppInitialized) {
      Server::GetInstance().Shutdown();
      DeviceLayer::PlatformMgr().Shutdown();
      #if CHIP_CONFIG_TRANSPORT_TRACE_ENABLED
      chip::trace::DeInitTrace();
      #endif // CHIP_CONFIG_TRANSPORT_TRACE_ENABLED
      // no more
      mChipAppInitialized = false;
    }
  }

};


// MARK: device lookup utilities

DevicePtr deviceForEndPointIndex(EndpointId aDynamicEndpointIndex)
{
  P44mbrd& app = static_cast<P44mbrd&>(*p44::Application::sharedApplication());
  return app.deviceForDynamicEndpointIndex(aDynamicEndpointIndex);
}

DevicePtr deviceForEndPointId(EndpointId aEndpointId)
{
  uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(aEndpointId);
  return deviceForEndPointIndex(endpointIndex);
}


// MARK: - gloabl CHIP callbacks

EmberAfStatus emberAfExternalAttributeReadCallback(
  EndpointId endpoint, ClusterId clusterId,
  const EmberAfAttributeMetadata * attributeMetadata, uint8_t * buffer,
  uint16_t maxReadLength
)
{
  EmberAfStatus ret = EMBER_ZCL_STATUS_FAILURE;
  DevicePtr dev = deviceForEndPointId(endpoint);
  if (dev) {
    POLOG(dev, LOG_NOTICE,
      "read external attr 0x%04x in cluster 0x%04x, expecting %d bytes",
      (int)attributeMetadata->attributeId, (int)clusterId, (int)maxReadLength
    );
    ret = dev->HandleReadAttribute(clusterId, attributeMetadata->attributeId, buffer, maxReadLength);
    if (ret!=EMBER_ZCL_STATUS_SUCCESS) {
      POLOG(dev, LOG_ERR, "Attribute read not handled!");
    }
    else {
      POLOG(dev, LOG_NOTICE, "- result = %s", dataToHexString(buffer, maxReadLength, ' ').c_str());
    }
  }
  return ret;
}


EmberAfStatus emberAfExternalAttributeWriteCallback(
  EndpointId endpoint, ClusterId clusterId,
  const EmberAfAttributeMetadata * attributeMetadata, uint8_t * buffer
)
{
  EmberAfStatus ret = EMBER_ZCL_STATUS_FAILURE;
  DevicePtr dev = deviceForEndPointId(endpoint);
  if (dev) {
    POLOG(dev, LOG_NOTICE, "write external attr 0x%04x in cluster 0x%04x", (int)attributeMetadata->attributeId, (int)clusterId);
    POLOG(dev, LOG_NOTICE, "- new data = %s", dataToHexString(buffer, attributeMetadata->size, ' ').c_str());
    ret = dev->HandleWriteAttribute(clusterId, attributeMetadata->attributeId, buffer);
    if (ret!=EMBER_ZCL_STATUS_SUCCESS) {
      POLOG(dev, LOG_ERR, "Attribute write not handled!");
    }
    else {
      POLOG(dev, LOG_INFO, "processed attribute write: %s", dev->description().c_str());
    }
  }
  return ret;
}


// callback for terminating mainloop
void chip::DeviceLayer::Internal::ExitExternalMainLoop()
{
  P44mbrd::sharedApplication()->terminateApp(EXIT_SUCCESS);
}


// MARK: - main (entry point)

#ifndef IS_MULTICALL_BINARY_MODULE

int main(int argc, char **argv)
{
  // prevent all logging until command line determines level
  SETLOGLEVEL(LOG_EMERG);
  SETERRLEVEL(LOG_EMERG, false); // messages, if any, go to stderr
  // create app with current mainloop
  P44mbrd* application = new(P44mbrd);
  // pass control
  int status = application->main(argc, argv);
  // done
  delete application;
  return status;
}

#endif // !IS_MULTICALL_BINARY_MODULE
