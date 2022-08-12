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



#include <AppMain.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/PlatformManager.h>

#include <app-common/zap-generated/af-structs.h>

#include <app-common/zap-generated/attribute-id.h>
#include <app-common/zap-generated/cluster-id.h>
#include <app/EventLogging.h>
#include <app/chip-zcl-zpro-codec.h>
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
#include <platform/CommissionableDataProvider.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#include <setup_payload/SetupPayload.h>

#include <pthread.h>
#include <sys/ioctl.h>

#include "CommissionableInit.h"

// plan44
#include "bridgeapi.h"
#include "deviceinfoprovider.hpp"

#include "device.h"
#include "deviceonoff.h"
#include "devicelevelcontrol.h"
#include "devicecolorcontrol.h"

#include <app/server/Server.h>

#include <cassert>
#include <iostream>
#include <vector>

using namespace chip;
using namespace chip::Credentials;
using namespace chip::Inet;
using namespace chip::Transport;
using namespace chip::DeviceLayer;
using namespace chip::app::Clusters;

namespace {

// Current ZCL implementation of Struct uses a max-size array of 254 bytes
const int kDescriptorAttributeArraySize = 254;

EndpointId gFirstDynamicEndpointId;

Device * gDevices[CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT];
typedef std::map<string, DevicePtr> DeviceDSUIDMap;
DeviceDSUIDMap gDeviceDSUIDMap;


// TODO: take this somehow out of generated ZAP
// For now: Endpoint 1 must be the Matter Bridge (Endpoint 0 is the Root node)
#define MATTER_BRIDGE_ENDPOINT 1

} // namespace


// MARK: - global callback handlers

// helper for getting device object by endpointId
DevicePtr deviceForEndPointId(EndpointId aEndpointId)
{
  uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(aEndpointId);
  if (endpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT) {
    return gDevices[endpointIndex];
  }
  return DevicePtr();
}



EmberAfStatus emberAfExternalAttributeReadCallback(
  EndpointId endpoint, ClusterId clusterId,
  const EmberAfAttributeMetadata * attributeMetadata, uint8_t * buffer,
  uint16_t maxReadLength
)
{
  EmberAfStatus ret = EMBER_ZCL_STATUS_FAILURE;
  uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(endpoint);
  if ((endpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT) && (gDevices[endpointIndex] != nullptr))
  {
    Device * dev = gDevices[endpointIndex];
    if (dev) {
      ChipLogProgress(DeviceLayer,
        "p44 Endpoint %d [%s]: read external attr 0x%04x in cluster 0x%04x, expecting %d bytes",
        (int)endpoint, dev->GetName().c_str(), (int)attributeMetadata->attributeId, (int)clusterId, (int)maxReadLength
      );
      ret = dev->HandleReadAttribute(clusterId, attributeMetadata->attributeId, buffer, maxReadLength);
      if (ret!=EMBER_ZCL_STATUS_SUCCESS) {
        ChipLogError(DeviceLayer, "p44 Endpoint %d: Attribute read not handled!", (int)endpoint);
      }
      else {
        dev->logStatus("processed attribute read");
      }
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
  uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(endpoint);
  if (endpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT) {
    Device * dev = gDevices[endpointIndex];
    if (dev) {
      ChipLogProgress(DeviceLayer, "p44 Endpoint %d [%s]: write external attr 0x%04x in cluster 0x%04x", (int)endpoint, dev->GetName().c_str(), (int)attributeMetadata->attributeId, (int)clusterId);
      ret = dev->HandleWriteAttribute(clusterId, attributeMetadata->attributeId, buffer);
      if (ret!=EMBER_ZCL_STATUS_SUCCESS) {
        ChipLogError(DeviceLayer, "p44 Endpoint %d: Attribute write not handled!", (int)endpoint);
      }
      else {
        dev->logStatus("processed attribute write");
      }
    }
  }
  return ret;
}


void MatterBridgedDeviceBasicClusterServerAttributeChangedCallback(const chip::app::ConcreteAttributePath & attributePath)
{
  ChipLogProgress(DeviceLayer, "p44 Endpoint %d, attributeId 0x%04x in BridgedDeviceBasicCluster has changed", (int)attributePath.mEndpointId, (int)attributePath.mAttributeId);
}

// MARK: - Main program

void ApplicationInit() {}


#define POLL_INTERVAL_MS (100)
uint8_t poll_prescale = 0;

bool kbhit()
{
    int byteswaiting;
    ioctl(0, FIONREAD, &byteswaiting);
    return byteswaiting > 0;
}


int chipmain(int argc, char * argv[]);
int gArgc;
char ** gArgv;


void answerreceived(JsonObjectPtr aJsonMsg, ErrorPtr aError)
{
  LOG(LOG_NOTICE, "method call status=%s, answer=%s", Error::text(aError), JsonObject::text(aJsonMsg));
  JsonObjectPtr o;
  JsonObjectPtr result;
  if (aJsonMsg && aJsonMsg->get("result", result)) {
    // global infos
    if (result->get("dSUID", o)) {
      gP44dbrDeviceInstanceInfoProvider.mDSUID = o->stringValue();
    }
    if (result->get("name", o)) {
      gP44dbrDeviceInstanceInfoProvider.mLabel = o->stringValue();
    }
    if (result->get("model", o)) {
      gP44dbrDeviceInstanceInfoProvider.mProductName = o->stringValue();
    }
    if (result->get("x-p44-deviceHardwareId", o)) {
      gP44dbrDeviceInstanceInfoProvider.mSerial = o->stringValue();
    }
    // process device list
    JsonObjectPtr vdcs;
    // devices
    if (result->get("x-p44-vdcs", vdcs)) {
      vdcs->resetKeyIteration();
      string vn;
      JsonObjectPtr vdc;
      while(vdcs->nextKeyValue(vn, vdc)) {
        JsonObjectPtr devices;
        if (vdc->get("x-p44-devices", devices)) {
          devices->resetKeyIteration();
          string dn;
          JsonObjectPtr device;
          while(devices->nextKeyValue(dn, device)) {
            // examine device
            if (device->get("x-p44-bridgeable", o)) {
              if (o->boolValue()) {
                // bridgeable device
                if (device->get("dSUID", o)) {
                  string dsuid = o->stringValue();
                  string name;
                  if (device->get("name", o)) name = o->stringValue(); // optional
                  // determine device type
                  JsonObjectPtr outputdesc;
                  if (device->get("outputDescription", outputdesc)) {
                    // output device
                    if (outputdesc->get("x-p44-behaviourType", o)) {
                      if (o->stringValue()=="light") {
                        // this is a light device
                        if (outputdesc->get("function", o)) {
                          int outputfunction = (int)o->int32Value();
                          LOG(LOG_NOTICE, "found light device '%s': %s, outputfunction=%d", name.c_str(), dsuid.c_str(), outputfunction);
                          // enable it for bridging
                          JsonObjectPtr params = JsonObject::newObj();
                          params->add("dSUID", JsonObject::newString(dsuid));
                          JsonObjectPtr props = JsonObject::newObj();
                          props->add("x-p44-bridged", JsonObject::newBool(true));
                          params->add("properties", props);
                          // no callback, but will wait when bridgeapi is in standalone mode
                          BridgeApi::sharedBridgeApi().call("setProperty", params, NULL);
                          // create to-be-bridged device
                          DevicePtr dev = nullptr;
                          // outputFunction_switch = 0, ///< switch output - single channel 0..100
                          // outputFunction_dimmer = 1, ///< effective value dimmer - single channel 0..100
                          // outputFunction_ctdimmer = 3, ///< dimmer with color temperature - channels 1 and 4
                          // outputFunction_colordimmer = 4, ///< full color dimmer - channels 1..6

                          // From: docs/guides/darwin.md
                          // -   Supported device types are (not exhaustive):
                          //
                          // | Type               | Decimal | HEX  |
                          // | ------------------ | ------- | ---- |
                          // | Lightbulb          | 256     | 0100 |
                          // | Lightbulb + Dimmer | 257     | 0101 |
                          // | Switch             | 259     | 0103 |
                          // | Contact Sensor     | 21      | 0015 |
                          // | Door Lock          | 10      | 000A |
                          // | Light Sensor       | 262     | 0106 |
                          // | Occupancy Sensor   | 263     | 0107 |
                          // | Outlet             | 266     | 010A |
                          // | Color Bulb         | 268     | 010C |
                          // | Window Covering    | 514     | 0202 |
                          // | Thermostat         | 769     | 0301 |
                          // | Temperature Sensor | 770     | 0302 |
                          // | Flow Sensor        | 774     | 0306 |

                          switch(outputfunction) {
                            default:
                            case 0: // switch output - single channel 0..100
                              dev = new DeviceOnOff();
                              break;
                            case 1: // effective value dimmer - single channel 0..100
                              dev = new DeviceLevelControl();
                              break;
                            case 3: // dimmer with color temperature - channels 1 and 4
                            case 4: // full color dimmer - channels 1..6
                              dev = new DeviceColorControl(outputfunction==3 /* ctOnly */);
                              break;
                          }
                          if (dev) {
                            dev->initBridgedInfo(device);
                            gDeviceDSUIDMap[dsuid] = dev;
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  // devices collected
  // start chip only now
  LOG(LOG_NOTICE, "End of bridge API setup, starting CHIP now");
  #error Restructure such that API setup runs first in p44 mainloop, and chip starts running only after
}


void apinotification(JsonObjectPtr aJsonMsg, ErrorPtr aError)
{
  // handle push notifications
  JsonObjectPtr o;
  string targetDSUID;
  if (aJsonMsg && aJsonMsg->get("dSUID", o, true)) {
    targetDSUID = o->stringValue();
    // search for device
    DeviceDSUIDMap::iterator devpos = gDeviceDSUIDMap.find(targetDSUID);
    if (devpos!=gDeviceDSUIDMap.end()) {
      // device exists, dispatch
      if (aJsonMsg->get("notification", o, true)) {
        string notification = o->stringValue();
        ChipLogProgress(DeviceLayer, "p44 Bridge sent notification '%s' for device %s", notification.c_str(), targetDSUID.c_str());
        bool handled = devpos->second->handleBridgeNotification(notification, aJsonMsg);
        if (handled) {
          devpos->second->logStatus("processed notification");
        }
        else {
          ChipLogError(DeviceLayer, "p44 Could not handle bridge notification '%s' for device %s", notification.c_str(), targetDSUID.c_str());
        }
      }
      else {
        ChipLogError(DeviceLayer, "p44 Bridge sent unknown message type for device %s", targetDSUID.c_str());
      }
    }
    else {
      ChipLogError(DeviceLayer, "p44 Bridge sent message for unknown device %s", targetDSUID.c_str());
    }
  }
  // continue waiting
  BridgeApi::sharedBridgeApi().handleSocketEvents();
}


void apiconnected(JsonObjectPtr aJsonMsg, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    // successful connection
    // - get list of devices
    JsonObjectPtr params = JsonObject::objFromText(
      "{ \"method\":\"getProperty\", \"dSUID\":\"root\", \"query\":{ "
      "\"dSUID\":null, \"model\":null, \"x-p44-deviceHardwareId\":null, "
      "\"x-p44-vdcs\": { \"*\":{ \"x-p44-devices\": { \"*\": "
      "{\"dSUID\":null, \"name\":null, \"outputDescription\":null, \"function\": null, \"x-p44-zonename\": null, "
      "\"vendorName\":null, \"model\":null, \"configURL\":null, "
      "\"channelStates\":null, "
      "\"active\":null, \"x-p44-bridgeable\":null, \"x-p44-bridged\":null }} }} }}"
    );
    BridgeApi::sharedBridgeApi().call("getProperty", params, answerreceived);
  }
  else {
    LOG(LOG_ERR, "error connecting bridge API: %s", aError->text());
  }
}


// MARK: - chipmain

#define kP44mbrNamespace "/ch.plan44.p44mbrd/"

#include "application.hpp"

#include <platform/TestOnlyCommissionableDataProvider.h>
#include <app/server/OnboardingCodesUtil.h>
#include <system/SystemLayerImpl.h>
#include <DeviceInfoProviderImpl.h>

#if CHIP_DEVICE_CONFIG_ENABLE_BOTH_COMMISSIONER_AND_COMMISSIONEE
#include "CommissionerMain.h"
#include <ControllerShellCommands.h>
#include <controller/CHIPDeviceControllerFactory.h>
#include <controller/ExampleOperationalCredentialsIssuer.h>
#include <lib/core/CHIPPersistentStorageDelegate.h>
#include <platform/KeyValueStoreManager.h>
#endif // CHIP_DEVICE_CONFIG_ENABLE_BOTH_COMMISSIONER_AND_COMMISSIONEE

#if CHIP_CONFIG_TRANSPORT_TRACE_ENABLED
#include "TraceDecoder.h"
#include "TraceHandlers.h"
#endif // CHIP_CONFIG_TRANSPORT_TRACE_ENABLED

using namespace p44;

/// Main program for plan44.ch P44-DSB-DEH in form of the "vdcd" daemon)
class P44mbrd : public CmdLineApp
{
  typedef CmdLineApp inherited;

  LinuxCommissionableDataProvider mCommissionableDataProvider;
  chip::DeviceLayer::DeviceInfoProviderImpl mExampleDeviceInfoProvider;

  // device instance info provider
  BridgeInfoProvider gP44dbrDeviceInstanceInfoProvider;

public:

  P44mbrd()
  {
  }


  virtual int main(int argc, char **argv)
  {
    const char *usageText =
      "Usage: %1$s [options]\n";

    /*
    const CmdLineOptionDescriptor options[] = {
      { 0  , "bridgeapi",        true,  "connection_spec; host:port connection to bridge API" },
      { 0  , "kvspath",          true,  "path; where to store the kvs data" },
      { 0  , "setuppincode",     true,  "setuppincode; setup pin code for commissioning" },
      { 0  , "spake2pVerifier",  true,  "spake2pVerifier; for commissioning" },
      { 0  , "spake2pIterationCount",  true,  "spake2pIterationCount; for commissioning" },
      { 0  , "discriminator",    true,  "discriminator; for commissioning" },
      { 0  , "productid",        true,  "productid; for commissioning" },
      { 0  , "vendorid",         true,  "vendorid; for commissioning" },
      DAEMON_APPLICATION_LOGOPTIONS,
      CMDLINE_APPLICATION_STDOPTIONS,
      CMDLINE_APPLICATION_PATHOPTIONS,
      { 0, NULL } // list terminator
    };
    */

    // parse the command line, exits when syntax errors occur
//    setCommandDescriptors(usageText, options);
    if (true /* parseCommandLine(argc, argv) */) {

//      if ((numOptions()<1) || (numArguments()>0)) {
//        // show usage
//        showUsage();
//        terminateApp(EXIT_SUCCESS);
//      }
//      else
      {
        // @note basically reduced ChipLinuxAppInit()

        CHIP_ERROR err = CHIP_NO_ERROR;
        RendezvousInformationFlag rendezvousFlags = RendezvousInformationFlag::kOnNetwork;

        err = Platform::MemoryInit();
        SuccessOrExit(err);

        // FIXME: for now, just use the linux command line options parser from CHIP
        err = ParseArguments(argc, argv, nullptr);
        SuccessOrExit(err);

        #ifdef CHIP_CONFIG_KVS_PATH
        if (LinuxDeviceOptions::GetInstance().KVS == nullptr) {
          err = DeviceLayer::PersistedStorage::KeyValueStoreMgrImpl().Init(tempPath("chip_kvs").c_str());
        }
        else {
          err = DeviceLayer::PersistedStorage::KeyValueStoreMgrImpl().Init(LinuxDeviceOptions::GetInstance().KVS);
        }
        SuccessOrExit(err);
        #endif

        err = DeviceLayer::PlatformMgr().InitChipStack();
        SuccessOrExit(err);
        // IMPORTANT: pass the p44utils mainloop to the system layer!
        static_cast<System::LayerSocketsLoop &>(DeviceLayer::SystemLayer()).SetLibEvLoop(MainLoop::currentMainLoop().libevLoop());

        // Init the commissionable data provider based on command line options
        // to handle custom verifiers, discriminators, etc.
        err = chip::examples::InitCommissionableDataProvider(mCommissionableDataProvider, LinuxDeviceOptions::GetInstance());
        SuccessOrExit(err);
        DeviceLayer::SetCommissionableDataProvider(&mCommissionableDataProvider);

        err = chip::examples::InitConfigurationManager(reinterpret_cast<ConfigurationManagerImpl &>(ConfigurationMgr()),
                                                       LinuxDeviceOptions::GetInstance());
        SuccessOrExit(err);

        if (LinuxDeviceOptions::GetInstance().payload.rendezvousInformation.HasValue()) {
            rendezvousFlags = LinuxDeviceOptions::GetInstance().payload.rendezvousInformation.Value();
        }

        err = GetPayloadContents(LinuxDeviceOptions::GetInstance().payload, rendezvousFlags);
        SuccessOrExit(err);

        ConfigurationMgr().LogDeviceConfig();

        {
          ChipLogProgress(NotSpecified, "==== Onboarding payload for Standard Commissioning Flow ====");
          PrintOnboardingCodes(LinuxDeviceOptions::GetInstance().payload);
        }
        {
          // For testing of manual pairing code with custom commissioning flow
          ChipLogProgress(NotSpecified, "==== Onboarding payload for Custom Commissioning Flows ====");
          err = GetPayloadContents(LinuxDeviceOptions::GetInstance().payload, rendezvousFlags);
          SuccessOrExit(err);

          LinuxDeviceOptions::GetInstance().payload.commissioningFlow = chip::CommissioningFlow::kCustom;

          PrintOnboardingCodes(LinuxDeviceOptions::GetInstance().payload);
        }

        #if CHIP_CONFIG_TRANSPORT_TRACE_ENABLED
        if (LinuxDeviceOptions::GetInstance().traceStreamFilename.HasValue()) {
          const char * traceFilename = LinuxDeviceOptions::GetInstance().traceStreamFilename.Value().c_str();
          auto traceStream           = new chip::trace::TraceStreamFile(traceFilename);
          chip::trace::AddTraceStream(traceStream);
        }
        else if (LinuxDeviceOptions::GetInstance().traceStreamToLogEnabled) {
          auto traceStream = new chip::trace::TraceStreamLog();
          chip::trace::AddTraceStream(traceStream);
        }
        if (LinuxDeviceOptions::GetInstance().traceStreamDecodeEnabled) {
          chip::trace::TraceDecoderOptions options;
          options.mEnableProtocolInteractionModelResponse = false;
          chip::trace::TraceDecoder * decoder = new chip::trace::TraceDecoder();
          decoder->SetOptions(options);
          chip::trace::AddTraceStream(decoder);
        }
        chip::trace::InitTrace();
        #endif // CHIP_CONFIG_TRANSPORT_TRACE_ENABLED

    exit:
        if (err != CHIP_NO_ERROR) {
          ChipLogProgress(NotSpecified, "Failed to init p44utils App: %s ", ErrorStr(err));
          terminateApp(EXIT_FAILURE);
        }

      } // command line ok
    } // option processing did not terminate app

    if (!isTerminated()) {
      // @note basically reduced ChipLinuxAppMainLoop()
      // prepare for mainloop
      static chip::CommonCaseDeviceServerInitParams initParams;
      VerifyOrDie(initParams.InitializeStaticResourcesBeforeServerInit() == CHIP_NO_ERROR);

      initParams.operationalServicePort        = CHIP_PORT;
      initParams.userDirectedCommissioningPort = CHIP_UDC_PORT;

      #if CHIP_DEVICE_ENABLE_PORT_PARAMS
      // use a different service port to make testing possible with other sample devices running on same host
      initParams.operationalServicePort        = LinuxDeviceOptions::GetInstance().securedDevicePort;
      initParams.userDirectedCommissioningPort = LinuxDeviceOptions::GetInstance().unsecuredCommissionerPort;
      #endif

      initParams.interfaceId = LinuxDeviceOptions::GetInstance().interfaceId;

      if (LinuxDeviceOptions::GetInstance().mCSRResponseOptions.csrExistingKeyPair) {
        LinuxDeviceOptions::GetInstance().mCSRResponseOptions.badCsrOperationalKeyStoreForTest.Init(initParams.persistentStorageDelegate);
        initParams.operationalKeystore = &LinuxDeviceOptions::GetInstance().mCSRResponseOptions.badCsrOperationalKeyStoreForTest;
      }

      // Init ZCL Data Model and CHIP App Server
      Server::GetInstance().Init(initParams);

      mExampleDeviceInfoProvider.SetStorageDelegate(&chip::Server::GetInstance().GetPersistentStorage());
      DeviceLayer::SetDeviceInfoProvider(&mExampleDeviceInfoProvider);

      // Now that the server has started and we are done with our startup logging,
      // log our discovery/onboarding information again so it's not lost in the
      // noise.
      ConfigurationMgr().LogDeviceConfig();

      PrintOnboardingCodes(LinuxDeviceOptions::GetInstance().payload);

// FIXME: which one is correct?
      // From ChipLinuxAppMainLoop Initialize device attestation config
      SetDeviceAttestationCredentialsProvider(LinuxDeviceOptions::GetInstance().dacProvider);
//      // From Bridge Sample: Initialize device attestation config
//      SetDeviceAttestationCredentialsProvider(Examples::GetExampleDACProvider());

      // Set our own device info provider
      SetDeviceInstanceInfoProvider(&gP44dbrDeviceInstanceInfoProvider);

      ApplicationInit();

      // MARK: - p44mbrd specific setup before entering mainloop

      // Set starting endpoint id where dynamic endpoints will be assigned, which
      // will be the next consecutive endpoint id after the last fixed endpoint.
      gFirstDynamicEndpointId = static_cast<chip::EndpointId>(
        static_cast<int>(emberAfEndpointFromIndex(static_cast<uint16_t>(emberAfFixedEndpointCount() - 1))) + 1);

      // Disable last fixed endpoint, which is used as a placeholder for all of the
      // supported clusters so that ZAP will generated the requisite code.
      emberAfEndpointEnableDisable(emberAfEndpointFromIndex(static_cast<uint16_t>(emberAfFixedEndpointCount() - 1)), false);

      // Clear out the array of dynamic endpoints
      memset(gDevices, 0, sizeof(gDevices));
      CHIP_ERROR cerr;
      chip::DeviceLayer::PersistedStorage::KeyValueStoreManager &kvs = chip::DeviceLayer::PersistedStorage::KeyValueStoreMgr();
      // get list of endpoints known in use
      char dynamicEndPointMap[CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT+1];
      size_t numDynamicEndPoints = 0;
      cerr = kvs.Get(kP44mbrNamespace "endPointMap", dynamicEndPointMap, CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT, &numDynamicEndPoints);
      if (cerr==CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND) {
        // no endpoint map yet
      }
      else {
        LogErrorOnFailure(cerr);
      }
      dynamicEndPointMap[numDynamicEndPoints]=0; // null terminate for easy debug printing as string
      LOG(LOG_INFO,"DynamicEndpointMap: %s", dynamicEndPointMap);
      // revert all endpoint map markers to "unconfimed"
      for (size_t i=0; i<numDynamicEndPoints; i++) {
        if (dynamicEndPointMap[i]=='D') dynamicEndPointMap[i]='d'; // unconfirmed device
      }
      // process list of to-be-bridged devices
      for (DeviceDSUIDMap::iterator pos = gDeviceDSUIDMap.begin(); pos!=gDeviceDSUIDMap.end(); ++pos) {
        DevicePtr dev = pos->second;
        // look up previous endpoint mapping
        string key = kP44mbrNamespace "devices/"; key += dev->bridgedDSUID();
        EndpointId dynamicEndpointIdx = kInvalidEndpointId;
        cerr = kvs.Get(key.c_str(), &dynamicEndpointIdx);
        if (cerr==CHIP_NO_ERROR) {
          // try to re-use the endpoint ID for this dSUID
          // - sanity check
          if (dynamicEndpointIdx>=numDynamicEndPoints || dynamicEndPointMap[dynamicEndpointIdx]!='d') {
            ChipLogError(DeviceLayer, "p44 inconsistent mapping info: dynamic endpoint #%d should be mapped as it is in use by device %s", dynamicEndpointIdx, dev->bridgedDSUID().c_str());
          }
          if (dynamicEndpointIdx>=CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT) {
            ChipLogError(DeviceLayer, "dynamic endpoint #%d for device %s exceeds max dynamic endpoint count -> try to add with new endpointId", dynamicEndpointIdx, dev->bridgedDSUID().c_str());
            dynamicEndpointIdx = kInvalidEndpointId; // reset to not-yet-assigned
          }
          else {
            // add to map
            if (dynamicEndpointIdx<numDynamicEndPoints) {
              dynamicEndPointMap[dynamicEndpointIdx]='D'; // confirm this device
            }
            else {
              for (size_t i = numDynamicEndPoints; i<dynamicEndpointIdx; i++) dynamicEndPointMap[i]=' ';
              dynamicEndPointMap[dynamicEndpointIdx] = 'D'; // insert as confirmed device
              dynamicEndPointMap[dynamicEndpointIdx+1] = 0;
            }
          }
        }
        else if (cerr==CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND) {
          // we haven't seen that dSUID yet -> need to assign it a new endpoint ID
          ChipLogProgress(DeviceLayer, "p44 new device %s, adding to bridge", dev->bridgedDSUID().c_str());
        }
        else {
          LogErrorOnFailure(cerr);
        }
        // update and possibly extend endpoint map
        if (dynamicEndpointIdx==kInvalidEndpointId) {
          // this device needs a new endpoint
          for (size_t i=0; i<numDynamicEndPoints; i++) {
            if (dynamicEndPointMap[i]==' ') {
              // use the gap
              dynamicEndpointIdx = i;
              dynamicEndPointMap[dynamicEndpointIdx] = 'D'; // add as confirmed device
            }
          }
          if (dynamicEndpointIdx==kInvalidEndpointId) {
            if (numDynamicEndPoints>=CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT) {
              ChipLogError(DeviceLayer, "p44 max number of dynamic endpoints exhausted -> cannot add new device %s", dev->bridgedDSUID().c_str());
            }
            else {
              dynamicEndpointIdx = numDynamicEndPoints++;
              dynamicEndPointMap[dynamicEndpointIdx] = 'D'; // add as confirmed device
              // save in KVS
              cerr = kvs.Put(key.c_str(), dynamicEndpointIdx);
              LogErrorOnFailure(cerr);
            }
          }
        }
        // assign to dynamic endpoint array
        if (dynamicEndpointIdx!=kInvalidEndpointId) {
          dev->SetDynamicEndpointIdx(dynamicEndpointIdx);
          gDevices[dynamicEndpointIdx] = dev.get();
        }
      }
      // save updated endpoint map
      cerr = kvs.Put(kP44mbrNamespace "endPointMap", dynamicEndPointMap, numDynamicEndPoints);
      LogErrorOnFailure(cerr);

      // Add the devices as dynamic endpoints
      for (size_t i=0; i<CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT; i++) {
        if (gDevices[i]) {
          // there is a device at this dynamic endpoint
          if (gDevices[i]->AddAsDeviceEndpoint(gFirstDynamicEndpointId, MATTER_BRIDGE_ENDPOINT)) {
            // give device chance to do things before mainloop starts
            gDevices[i]->beforeChipMainloopPrep();
          }
        }
      }

      // Run CHIP
      // FIXME: remove this comment, but it shows for now that we would have started the select mainloop here
      //chip::DeviceLayer::PlatformMgr().RunEventLoop();
    }
    // app now ready to run (or cleanup when already terminated)
    return run();
  }


  virtual void cleanup(int aExitCode) override
  {
    Server::GetInstance().Shutdown();
    DeviceLayer::PlatformMgr().Shutdown();
    #if CHIP_CONFIG_TRANSPORT_TRACE_ENABLED
    chip::trace::DeInitTrace();
    #endif // CHIP_CONFIG_TRANSPORT_TRACE_ENABLED
  }


  virtual void initialize() override
  {
    ChipLogProgress(DeviceLayer, "p44: p44utils mainloop started");

    // enable API to run it in parallel with chip main loop
    BridgeApi::sharedBridgeApi().endStandalone();

    // try bridge API
    BridgeApi::sharedBridgeApi().setConnectionParams("127.0.0.1", 4444, 10*Second);
    BridgeApi::sharedBridgeApi().setIncomingMessageCallback(apinotification);
    BridgeApi::sharedBridgeApi().connect(apiconnected);

    for (size_t i=0; i<CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT; i++) {
      if (gDevices[i]) {
        // give device chance to do things just after mainloop has started
        gDevices[i]->inChipMainloopInit();
        // dump status
        gDevices[i]->logStatus();
      }
    }

  }

};




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
