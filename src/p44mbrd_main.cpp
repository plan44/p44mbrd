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
#include <app/server/CommissioningWindowManager.h>
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
#include "chip_glue/deviceattestationprovider.h"

#include "actions.h"
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
#if CC_ADAPTERS
  #include "adapters/cc/ccbridge.h"
#endif // CC_ADAPTERS

// FIXME: remove debug stuff
#define CERT_DEBUG 0

#if CERT_DEBUG
// FIXME: remove debug stuff
#warning "remove debug stuff"
#include <credentials/CertificationDeclaration.h>
#endif

using namespace chip;
using namespace chip::app;
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
#define CC_DEFAULT_BRIDGE_SERVICE "18163" // RPC 18=R, 16=P, 3=C


/// Main program application object
class P44mbrd : public CmdLineApp, public AppDelegate, public BridgeMainDelegate
{
  typedef CmdLineApp inherited;

  // CHIP "globals"
  bool mChipAppInitialized;
  LinuxCommissionableDataProvider mCommissionableDataProvider;
  chip::DeviceLayer::DeviceInfoProviderImpl mExampleDeviceInfoProvider; // TODO: example? do we need our own?
  P44mbrdDeviceInfoProvider mP44dbrDeviceInstanceInfoProvider; ///< our own device **instance** info provider
  P44mbrdDeviceAttestationProvider mP44mbrdDeviceAttestationProvider; ///< our own attestation provider

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

  // actions
  ActionsManager::EndPointListsMap mEndPointLists;
  ActionsManager::ActionsMap mActions;
  ActionsManager mActionsManager;

public:

  P44mbrd() :
    mChipAppInitialized(false),
    mNumDynamicEndPoints(0),
    mFirstFreeEndpointId(kInvalidEndpointId),
    mEthernetNetworkCommissioningInstance(0, &mEthernetDriver),
    mActionsManager(mActions, mEndPointLists)
  {
  }


  virtual ~P44mbrd()
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
      { 0, "factorydata",         true, "path[:path...];file paths of factory data files to be processed to gather product specific "
                                        "data such as PID, VID, certificates, etc. Paths are read in the order specified here, "
                                        "duplicate items overriding already defined ones."},
      { 0, "discriminator",       true, "discriminator;override the discriminator from factorydata" },
      { 0, "setuppin",            true, "pincode;override the pincode from factorydata\n"
                                        "If not provided to compute a verifier, the spake2p-verifier must be provided in factorydata." },
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
      { 0, "p44apihost",          true, "host;host of the p44 bridge API" },
      { 0, "p44apiservice",       true, "port;port of the p44 bridge API, default is " P44_DEFAULT_BRIDGE_SERVICE },
      // TODO: remove legacy options
      { 0, "bridgeapihost",       true, nullptr },
      { 0, "bridgeapiservice",    true, nullptr },
      #endif
      #if CC_ADAPTERS
      // - CC device implementations
      { 0, "ccapihost",           true, "host;host of the CC bridge API" },
      { 0, "ccapiservice",        true, "port;port of the CC bridge API, default is " CC_DEFAULT_BRIDGE_SERVICE },
      #endif // CC_ADAPTERS
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
    // TODO: remove compatibiliy options
    if (!getStringOption("p44apihost", p44apihost)) getStringOption("bridgeapihost", p44apihost);
    if (!getStringOption("p44apiservice", p44apiservice))  getStringOption("bridgeapiservice", p44apihost);
    if (p44apihost) {
      P44_BridgeImpl* p44bridgeP = &P44_BridgeImpl::adapter();
      p44bridgeP->setAPIParams(p44apihost, p44apiservice);
      mAdapters.push_back(p44bridgeP);
    }
    #endif // P44_ADAPTERS
    #if CC_ADAPTERS
    const char* ccapihost = nullptr;
    const char* ccapiservice = CC_DEFAULT_BRIDGE_SERVICE;
    getStringOption("ccapihost", ccapihost);
    getStringOption("ccapiservice", ccapiservice);
    if (ccapihost) {
      CC_BridgeImpl* ccbridgeP = &CC_BridgeImpl::adapter();
      ccbridgeP->setAPIParams(ccapihost, ccapiservice);
      mAdapters.push_back(ccbridgeP);
    }
    #endif // CC_ADAPTERS
  }


  void updateCommissionableStatus(bool aIsCommissionable)
  {
    OLOG(LOG_NOTICE, "Commissioning Window changes to %scommissionable)", aIsCommissionable ? "OPEN (" : "CLOSED (not ");
    for (BridgeAdaptersList::iterator pos = mAdapters.begin(); pos!=mAdapters.end(); ++pos) {
      (*pos)->reportCommissionable(aIsCommissionable);
    }
  }


  void updateCommissionableStatus()
  {
    using csta = chip::app::Clusters::AdministratorCommissioning::CommissioningWindowStatusEnum;
    csta commissioningstatus =
    Server::GetInstance().GetCommissioningWindowManager().CommissioningWindowManager::CommissioningWindowStatusForCluster();
    bool commissionable = commissioningstatus != csta::kWindowNotOpen;
    updateCommissionableStatus(commissionable);
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


  void makeCommissionable(bool aIsCommissionable)
  {
    if (aIsCommissionable) {
      Server::GetInstance().GetCommissioningWindowManager().OpenBasicCommissioningWindow();
    }
    else {
      Server::GetInstance().GetCommissioningWindowManager().CloseCommissioningWindow();
    }
  }


  CHIP_ERROR installAdaptersInitialDevices()
  {
    CHIP_ERROR chiperr;

    for (BridgeAdaptersList::iterator apos = mAdapters.begin(); apos!=mAdapters.end(); ++apos) {
      (*apos)->installInitialDevices(chiperr);
    }
    return chiperr;
  }


  virtual void initialize() override
  {
    OLOG(LOG_NOTICE, "p44: p44utils mainloop started");
    // Instantiate and initialize adapters
    initAdapters();
    // start the adapters
    mUnstartedAdapters = (int)mAdapters.size();
    for (BridgeAdaptersList::iterator pos = mAdapters.begin(); pos!=mAdapters.end(); ++pos) {
      (*pos)->startup(static_cast<BridgeMainDelegate&>(*this));
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
    // TODO: maybe remove, probably callbacks should post the correct state automatically?
    // update commissionable status in adapters
    if (Server::GetInstance().GetFabricTable().FabricCount() == 0) {
      // with no fabrics, we are commissionable from start
      OLOG(LOG_NOTICE, "Fabric table is empty - starting up commissionable")
      updateCommissionableStatus(true);
    }
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
      OLOG(LOG_ERR, "No free endpoint available - all %d dynamic endpoints are occupied -> cannot add new device", CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT);
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
          if (mDevices[i]->endpointId()==mFirstFreeEndpointId) {
            // the supposedly free next endpointID is in use
            if (++mFirstFreeEndpointId==0xFFFF) mFirstFreeEndpointId = emberAfEndpointCount(); // increment and wraparound from 0xFFFE to first dynamic endpoont
            POLOG(mDevices[i], LOG_WARNING, "is already using what was recorded as next free endpointID -> adjusted the latter to %d", mFirstFreeEndpointId);
          }
          if (mDevices[i]->endpointId()==endpointId) {
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
      if (mDevices[i]->addAsDeviceEndpoint()) {
        POLOG(mDevices[i], LOG_DEBUG, "registered before starting Matter mainloop");
        // report installed
        mDevices[i]->didGetInstalled();
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


  // MARK: Access to application level data

  DevicePtr deviceForDynamicEndpointIndex(EndpointId aDynamicEndpointIndex)
  {
    DevicePtr dev;
    if (aDynamicEndpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT) {
      dev = mDevices[aDynamicEndpointIndex];
    }
    return dev;
  }


  ActionsManager& getActionsManager()
  {
    return mActionsManager;
  }


  // MARK: - BridgeMainDelegate

  void adapterStartupComplete(ErrorPtr aError, BridgeAdapter &aAdapter) override
  {
    mUnstartedAdapters--; // count this start
    if (Error::notOK(aError)) OLOG(LOG_WARNING, "Adapter startup error: %s", aError->text());
    if (mUnstartedAdapters>0) {
      OLOG(LOG_NOTICE, "Adapter started, %d remaining", mUnstartedAdapters);
    }
    else {
      // all adapters have called back, so we can consider all started (or not operational at all)
      bool startnow = false;
      for (BridgeAdaptersList::iterator pos = mAdapters.begin(); pos!=mAdapters.end(); ++pos) {
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


  CHIP_ERROR installDevice(DevicePtr aDevice, BridgeAdapter& aAdapter) override
  {
    CHIP_ERROR chiperr;
    // install base (or single) device
    chiperr = installSingleBridgedDevice(aDevice, MATTER_BRIDGE_ENDPOINT);
    if (chiperr==CHIP_NO_ERROR) {
      // also add subdevices AFTER the main device (if any)
      for (DevicesList::iterator pos = aDevice->subDevices().begin(); pos!=aDevice->subDevices().end(); ++pos) {
        (*pos)->flagAsPartOfComposedDevice();
        installSingleBridgedDevice(*pos, aDevice->endpointId());
      }
    }
    return chiperr;
  }


  ErrorPtr addAdditionalDevice(DevicePtr aDevice, BridgeAdapter& aAdapter) override
  {
    ErrorPtr err;
    if (!aDevice) {
      return TextError::err("addAdditionalDevice: no device");
    }
    if (!mChipAppInitialized) {
      // we are still waiting for the first bridged device and haven't started CHIP yet -> start now
      OLOG(LOG_NOTICE, "First bridgeable device installed, can start CHIP now, finally");
      // starting chip will take care of actually installing the device
      // - but unwind call stack before
      MainLoop::currentMainLoop().executeNow(boost::bind(&P44mbrd::startChip, this));
    }
    else {
      // already running
      installDevice(aDevice, aAdapter);
      // save possibly modified first free endpointID (increased when previously unknown devices have been added)
      chip::DeviceLayer::PersistedStorage::KeyValueStoreManager &kvs = chip::DeviceLayer::PersistedStorage::KeyValueStoreMgr();
      CHIP_ERROR chiperr = kvs.Put(kP44mbrNamespace "firstFreeEndpointId", mFirstFreeEndpointId);
      LogErrorOnFailure(chiperr);
      // add as new endpoint to bridge
      if (aDevice->addAsDeviceEndpoint()) {
        POLOG(aDevice, LOG_NOTICE, "added as additional dynamic endpoint while Matter already running");
        // report installed
        aDevice->didGetInstalled();
        aDevice->didBecomeOperational();
        // dump status
        POLOG(aDevice, LOG_INFO, "initialized from chip: %s", aDevice->description().c_str());
        // also add subdevices that are part of this additional device and thus not yet present
        for (DevicesList::iterator pos = aDevice->subDevices().begin(); pos!=aDevice->subDevices().end(); ++pos) {
          DevicePtr subDev = *pos;
          if (subDev->addAsDeviceEndpoint()) {
            POLOG(subDev, LOG_NOTICE, "added as part of composed device as additional dynamic endpoint while CHIP is already up");
            subDev->didBecomeOperational();
            // dump status
            POLOG(subDev, LOG_INFO, "initialized composed device as part of %s", aDevice->description().c_str());
          }
        }
      }
      err = TextError::err("failed adding device as endpoint");
    }
    return err;
  }


  void disableDevice(DevicePtr aDevice, BridgeAdapter& aAdapter) override
  {
    aDevice->willBeDisabled();
    // subdevices first
    for (DevicesList::iterator pos = aDevice->subDevices().begin(); pos!=aDevice->subDevices().end(); ++pos) {
      (*pos)->willBeDisabled();
      emberAfEndpointEnableDisable((*pos)->endpointId(), false);
      POLOG((*pos), LOG_NOTICE, "subdevice endpoint disabled, device no longer operational");
    }
    // disable main device now
    emberAfEndpointEnableDisable(aDevice->endpointId(), false);
    POLOG(aDevice, LOG_NOTICE, "main device endpoint disabled, device no longer operational");
  }


  void reEnableDevice(DevicePtr aDevice, BridgeAdapter& aAdapter) override
  {
    POLOG(aDevice, LOG_NOTICE, "re-enabling as dynamic endpoint");
    // report re-installed
    aDevice->didGetInstalled();
    aDevice->didBecomeOperational();
    // subdevices
    for (DevicesList::iterator pos = aDevice->subDevices().begin(); pos!=aDevice->subDevices().end(); ++pos) {
      (*pos)->didGetInstalled();
      emberAfEndpointEnableDisable((*pos)->endpointId(), true);
      aDevice->didBecomeOperational();
    }
  }


  ErrorPtr makeCommissionable(bool aCommissionable, BridgeAdapter& aAdapter) override
  {
    makeCommissionable(aCommissionable);
    return ErrorPtr();
  }


  void addOrReplaceAction(ActionPtr aAction, BridgeAdapter& aAdapter) override
  {
    mActions[aAction->getActionId()] = aAction;
    MatterReportingAttributeChangeCallback(MATTER_BRIDGE_ENDPOINT, Actions::Id, Actions::Attributes::ActionList::Id);
  }


  void addOrReplaceEndpointsList(EndpointListInfoPtr aEndPointList, BridgeAdapter& aAdapter) override
  {
    mEndPointLists[aEndPointList->GetEndpointListId()] = aEndPointList;
    MatterReportingAttributeChangeCallback(MATTER_BRIDGE_ENDPOINT, Actions::Id, Actions::Attributes::EndpointLists::Id);
  }



  // MARK: - chip application delegate


  //virtual void OnCommissioningSessionEstablishmentStarted() {}
  //virtual void OnCommissioningSessionStarted() {}
  //virtual void OnCommissioningSessionStopped(CHIP_ERROR err) {}

  /*
   * This is called anytime a basic or enhanced commissioning window is opened.
   *
   * The type of the window can be retrieved by calling
   * CommissioningWindowManager::CommissioningWindowStatusForCluster(), but
   * being careful about how to handle the None status when a window is in
   * fact open.
   */
  void OnCommissioningWindowOpened() override
  {
    updateCommissionableStatus(true);
  }


  void OnCommissioningWindowClosed() override
  {
    updateCommissionableStatus(false);
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


  ErrorPtr InitCommissionableDataProvider(LinuxCommissionableDataProvider& aCommissionableDataProvider, chip::PayloadContents& aOnBoardingPayload, FactoryDataProviderPtr aFactoryData)
  {
    chip::Optional<std::vector<uint8_t>> spake2pVerifier;
    chip::Optional<std::vector<uint8_t>> spake2pSalt;

    string s;
    // spake2p-verifier
    if (aFactoryData->getOptionalString("spake2p-verifier", s)) {
      std::vector<uint8_t> s2pvec;
      if (Base64ArgToVector(s.c_str(), chip::Crypto::kSpake2p_VerifierSerialized_Length, s2pvec)) {
        if (s2pvec.size()!=chip::Crypto::kSpake2p_VerifierSerialized_Length) {
          return TextError::err("--spake2p-verifier must be %zu bytes", chip::Crypto::kSpake2p_VerifierSerialized_Length);
        }
        spake2pVerifier.SetValue(s2pvec);
      }
      else {
        return TextError::err("invalid b64 in spake2p-verifier");
      }
    }
    // spake2p-salt
    if (aFactoryData->getOptionalString("spake2p-salt", s)) {
      std::vector<uint8_t> saltvec;
      if (Base64ArgToVector(s.c_str(), chip::Crypto::kSpake2p_Max_PBKDF_Salt_Length, saltvec)) {
        if (
          saltvec.size()>chip::Crypto::kSpake2p_Max_PBKDF_Salt_Length ||
          saltvec.size()<chip::Crypto::kSpake2p_Min_PBKDF_Salt_Length
        ) {
          return TextError::err("--spake2p-salt must be %zu..%zu bytes", chip::Crypto::kSpake2p_Min_PBKDF_Salt_Length, chip::Crypto::kSpake2p_Max_PBKDF_Salt_Length);
        }
        spake2pSalt.SetValue(saltvec);
      }
      else {
        return TextError::err("invalid b64 in spake2p-salt");
      }
    }
    // spake2p-iterations
    unsigned int spake2pIterationCount = aFactoryData->getUInt32("spake2p-iterations");
    if (spake2pIterationCount==0) spake2pIterationCount = chip::Crypto::kSpake2p_Min_PBKDF_Iterations;
    if (spake2pIterationCount<chip::Crypto::kSpake2p_Min_PBKDF_Iterations || spake2pIterationCount>chip::Crypto::kSpake2p_Max_PBKDF_Iterations) {
      return TextError::err("spake2p-iterations must be in range %u..%u", chip::Crypto::kSpake2p_Min_PBKDF_Iterations, chip::Crypto::kSpake2p_Max_PBKDF_Iterations);
    }
    // setup pincode
    chip::Optional<uint32_t> setUpPINCode;
    if (aOnBoardingPayload.setUpPINCode==0) {
      if (!spake2pVerifier.HasValue()) {
        return TextError::err("missing setuppin or spake2p-verifier");
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

    // parse the factory data
    string paths;
    if (!getStringOption("factorydata", paths)) return TextError::err("Missing factory data paths");
    FactoryDataProviderPtr factoryData = new FileBasedFactoryDataProvider(paths, "p44mbrd");

    // pass factory data to the device instance info provider for initialisation
    mP44dbrDeviceInstanceInfoProvider.loadFromFactoryData(factoryData);
    // TODO: maybe remove this, when all info comes from factory data
    // augment factory data with information from adapter(s)
    bool infoset = false;
    for (BridgeAdaptersList::iterator pos = mAdapters.begin(); pos!=mAdapters.end(); ++pos) {
      // for now: assume we'll have only one adapter running for real application,
      // which will determine the matter bridge's identification.
      // With multiple adapters, the first instantiated will determine the device instance info
      if (!infoset) {
        infoset = true;
        // Override if those are not yet set from factory data
        if (mP44dbrDeviceInstanceInfoProvider.mProductName.empty()) mP44dbrDeviceInstanceInfoProvider.mProductName = (*pos)->model();
        if (mP44dbrDeviceInstanceInfoProvider.mProductLabel.empty()) mP44dbrDeviceInstanceInfoProvider.mProductLabel = (*pos)->label();
        if (mP44dbrDeviceInstanceInfoProvider.mUID.empty()) mP44dbrDeviceInstanceInfoProvider.mUID = (*pos)->UID();
        if (mP44dbrDeviceInstanceInfoProvider.mSerial.empty()) mP44dbrDeviceInstanceInfoProvider.mSerial = (*pos)->serial();
      }
    }

    // pass factory data to the device attestationprovider for initialisation
    mP44mbrdDeviceAttestationProvider.loadFromFactoryData(factoryData);

    // prepare the onboarding payload
    chip::PayloadContents onBoardingPayload;
    // - always on-network
    onBoardingPayload.rendezvousInformation.SetValue(RendezvousInformationFlag::kOnNetwork);
    // - initialize fields from factory data
    mP44dbrDeviceInstanceInfoProvider.GetVendorId(onBoardingPayload.vendorID);
    #ifdef CHIP_DEVICE_CONFIG_DEVICE_VENDOR_ID
    if (onBoardingPayload.vendorID==0) {
      onBoardingPayload.vendorID = CHIP_DEVICE_CONFIG_DEVICE_VENDOR_ID;
      OLOG(LOG_WARNING, "No VendorID in factorydata: using development default VID=0x%04X", onBoardingPayload.vendorID);
    }
    #endif
    mP44dbrDeviceInstanceInfoProvider.GetProductId(onBoardingPayload.productID);
    #ifdef CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_ID
    if (onBoardingPayload.productID==0) {
      onBoardingPayload.productID = CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_ID;
      OLOG(LOG_WARNING, "No ProductId in factorydata: using development default PID=0x%04X", onBoardingPayload.productID);
    }
    #endif
    onBoardingPayload.version = factoryData->getUInt8("PAYLOADVERSION"); // defaults to 0
    onBoardingPayload.commissioningFlow = (CommissioningFlow)factoryData->getUInt8("COMMISSIONINGFLOW"); // defaults to 0 = CommissioningFlow::kStandard
    onBoardingPayload.discriminator.SetLongValue(factoryData->getUInt16("DISCRIMINATOR") & ((1<<12)-1)); // defaults to 0, 12 bits max
    onBoardingPayload.setUpPINCode = factoryData->getUInt32("SETUPPIN") & ((1<<27)-1);
    // - override from command line
    int i;
    if (getIntOption("discriminator", i)) onBoardingPayload.discriminator.SetLongValue(static_cast<uint16_t>(i & ((1<<12)-1)));
    if (getIntOption("setuppin", i)) onBoardingPayload.setUpPINCode = i & ((1<<27)-1);

    // MARK: basically reduced ChipLinuxAppInit() from here
    ErrorPtr err;

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
    err = InitCommissionableDataProvider(mCommissionableDataProvider, onBoardingPayload, factoryData);
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
      string qrCodeStr = qrCode.data();
      string manualParingCodeStr;
      if (GetManualPairingCode(manualPairingCode, onBoardingPayload) == CHIP_NO_ERROR) {
        manualParingCodeStr = manualPairingCode.data();
      }
      updateCommissioningInfo(qrCodeStr, manualParingCodeStr);
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


    DeviceAttestationCredentialsProvider *dacProvider = Examples::GetExampleDACProvider();
    SetDeviceAttestationCredentialsProvider(dacProvider);

    #if CERT_DEBUG
    #warning "allow actual certs"

    // FIXME: remove debug stuff
    #warning "remove debug stuff"
    uint8_t certDeclBuf[Credentials::kMaxCMSSignedCDMessage]; // Sized to hold the example certificate declaration with 100 PIDs.                                                              // See DeviceAttestationCredsExample
    MutableByteSpan certDeclSpan(certDeclBuf);
    dacProvider->GetCertificationDeclaration(certDeclSpan);
    dacProvider->GetDeviceAttestationCert(certDeclSpan);
    dacProvider->GetProductAttestationIntermediateCert(certDeclSpan);

    #endif // CERT_DEBUG

    // Set our own device info provider (before initializing server, which wants to see it installed)
    SetDeviceInstanceInfoProvider(&mP44dbrDeviceInstanceInfoProvider);

    // prepare the onboarding payload

    // TODO: avoid using example-only CommonCaseDeviceServerInitParams
    // copied from there:
    /*
    * ACTION ITEMS FOR TRANSITION from a example in-tree to a product:
    *
    * While this could be used indefinitely, it does not exemplify orderly management of
    * application-injected resources. It is recommended for actual products to instead:
    *   - Use the basic ServerInitParams in the application
    *   - Have the application own an instance of the resources being injected in its own
    *     state (e.g. an implementation of PersistentStorageDelegate and GroupDataProvider
    *     interfaces).
    *   - Initialize the injected resources prior to calling Server::Init()
    *   - De-initialize the injected resources after calling Server::Shutdown()
    */

    static chip::CommonCaseDeviceServerInitParams serverInitParams;
    VerifyOrDie(serverInitParams.InitializeStaticResourcesBeforeServerInit() == CHIP_NO_ERROR);

    serverInitParams.appDelegate = this;

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


void MatterActionsPluginServerInitCallback()
{
  // register actions server attribute access class
  P44mbrd& app = static_cast<P44mbrd&>(*p44::Application::sharedApplication());
  registerAttributeAccessOverride(&app.getActionsManager());
}


chip::Protocols::InteractionModel::Status MatterPreAttributeChangeCallback(
  const ConcreteAttributePath & attributePath,
  uint8_t type, uint16_t size, uint8_t * value
)
{
  // TODO: maybe implement forwarding to devices
  return chip::Protocols::InteractionModel::Status::Success;
}


void MatterPostAttributeChangeCallback(
  const ConcreteAttributePath& attributePath,
  uint8_t type, uint16_t size, uint8_t* value
)
{
  DevicePtr dev = deviceForEndPointId(attributePath.mEndpointId);
  if (dev) {
    dev->handleAttributeChange(attributePath.mClusterId, attributePath.mAttributeId);
  }
}



bool emberAfActionsClusterInstantActionCallback(
  CommandHandler * commandObj, const ConcreteCommandPath & commandPath,
  const Clusters::Actions::Commands::InstantAction::DecodableType & commandData
)
{
  if (commandPath.mEndpointId==MATTER_BRIDGE_ENDPOINT) {
    P44mbrd& app = static_cast<P44mbrd&>(*p44::Application::sharedApplication());
    Protocols::InteractionModel::Status status = app.getActionsManager().invokeInstantAction(
      commandPath,
      commandData.actionID,
      commandData.invokeID,
      Optional<uint16_t>()
    );
    commandObj->AddStatus(commandPath, status);
    return true;
  }
  return false; // not handled
}

bool emberAfActionsClusterInstantActionWithTransitionCallback(
  CommandHandler * commandObj, const ConcreteCommandPath & commandPath,
  const Clusters::Actions::Commands::InstantActionWithTransition::DecodableType & commandData
)
{
  if (commandPath.mEndpointId==MATTER_BRIDGE_ENDPOINT) {
    P44mbrd& app = static_cast<P44mbrd&>(*p44::Application::sharedApplication());
    Protocols::InteractionModel::Status status = app.getActionsManager().invokeInstantAction(
      commandPath,
      commandData.actionID,
      commandData.invokeID,
      Optional<uint16_t>(commandData.transitionTime)
    );
    commandObj->AddStatus(commandPath, status);
    return true;
  }
  return false; // not handled
}


EmberAfStatus emberAfExternalAttributeReadCallback(
  EndpointId endpoint, ClusterId clusterId,
  const EmberAfAttributeMetadata * attributeMetadata, uint8_t * buffer,
  uint16_t maxReadLength
)
{
  EmberAfStatus ret = EMBER_ZCL_STATUS_FAILURE;
  DevicePtr dev = deviceForEndPointId(endpoint);
  if (dev) {
    POLOG(dev, LOG_DEBUG,
      "read external attr 0x%04x in cluster 0x%04x, expecting %d bytes, attr.size=%d",
      (int)attributeMetadata->attributeId, (int)clusterId, (int)maxReadLength, (int)attributeMetadata->size
    );
    ret = dev->handleReadAttribute(clusterId, attributeMetadata->attributeId, buffer, maxReadLength);
    if (ret!=EMBER_ZCL_STATUS_SUCCESS) {
      POLOG(dev, LOG_ERR, "NOT HANDLED: reading external attr 0x%04x in cluster 0x%04x", (int)attributeMetadata->attributeId, (int)clusterId);
    }
    else {
      POLOG(dev, LOG_DEBUG, "- result[%d] = %s%s", maxReadLength, dataToHexString(buffer, maxReadLength>16 ? 16 : maxReadLength, ' ').c_str(), maxReadLength>16 ? " ..." : "");
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
    POLOG(dev, LOG_DEBUG, "write external attr 0x%04x in cluster 0x%04x, attr.size=%d", (int)attributeMetadata->attributeId, (int)clusterId, (int)attributeMetadata->size);
    POLOG(dev, LOG_DEBUG, "- new data = %s", dataToHexString(buffer, attributeMetadata->size, ' ').c_str());
    ret = dev->handleWriteAttribute(clusterId, attributeMetadata->attributeId, buffer);
    if (ret!=EMBER_ZCL_STATUS_SUCCESS) {
      POLOG(dev, LOG_ERR, "NOT HANDLED: writing external attr 0x%04x in cluster 0x%04x", (int)attributeMetadata->attributeId, (int)clusterId);
    }
    else {
      POLOG(dev, LOG_DEBUG, "- processed external attribute write");
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

static void chipLoggingCallback(const char* aModule, uint8_t aCategory, const char *aMsg, va_list aArgs)
{
  // Print the log on console for debug
//  va_list argsCopy;
//  va_copy(argsCopy, aArgs);
  int lvl = 0;
  switch (aCategory) {
    case chip::Logging::kLogCategory_Error:
      lvl = LOG_ERR; break;
    case chip::Logging::kLogCategory_Progress:
      lvl = LOG_NOTICE; break;
    default:
    case chip::Logging::kLogCategory_Detail:
    case chip::Logging::kLogCategory_Automation:
      lvl = LOG_DEBUG; break;
  }
  string msg;
  string_format_v(msg, false, aMsg, aArgs);
  globalLogger.contextLogStr_always(lvl, string_format("CHIP:%-3s", aModule), msg);
}


#ifndef IS_MULTICALL_BINARY_MODULE

int main(int argc, char **argv)
{
  // prevent all logging until command line determines level
  SETLOGLEVEL(LOG_EMERG);
  SETERRLEVEL(LOG_EMERG, false); // messages, if any, go to stderr
  // redirect chip logging
  chip::Logging::SetLogRedirectCallback(&chipLoggingCallback);
  // create app with current mainloop
  P44mbrd* application = new(P44mbrd);
  // pass control
  int status = application->main(argc, argv);
  // done
  delete application;
  return status;
}

#endif // !IS_MULTICALL_BINARY_MODULE
