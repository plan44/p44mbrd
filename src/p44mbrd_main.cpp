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

//#include <AppMain.h>
#include <platform/CHIPDeviceLayer.h>
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
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CommissionableDataProvider.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#include <setup_payload/SetupPayload.h>

#include <pthread.h>
#include <sys/ioctl.h>

#include <CommissionableInit.h>

// plan44
#include "bridgeapi.h"
#include "p44mbrd_main.h"
#include "chip_glue/chip_error.h"
#include "chip_glue/deviceinfoprovider.h"

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

#define DEFAULT_BRIDGE_SERVICE "4444"
#define DEFAULT_BRIDGE_HOST "127.0.0.1"

/// Main program for plan44.ch P44-DSB-DEH in form of the "vdcd" daemon)
class P44mbrd : public CmdLineApp
{
  typedef CmdLineApp inherited;

  // CHIP "globals"
  bool mChipAppInitialized;
  LinuxCommissionableDataProvider mCommissionableDataProvider;
  chip::DeviceLayer::DeviceInfoProviderImpl mExampleDeviceInfoProvider; // TODO: example? do we need our own?
  P44DeviceInfoProvider mP44dbrDeviceInstanceInfoProvider; ///< our own device **instance** info provider

  // Bridged devices info
  EndpointId mFirstDynamicEndpointId;
  Device * mDevices[CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT];
  typedef std::map<string, DevicePtr> DeviceDSUIDMap;
  DeviceDSUIDMap mDeviceDSUIDMap;

public:

  P44mbrd() :
    mChipAppInitialized(false)
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
      { 0, "discriminator",       true, "discriminator;a12-bit unsigned integer match the value which a device advertises during commissioning" },
      { 0, "setuppin",            true, "pincode;A 27-bit unsigned integer, which serves as proof of possession during commissioning.\n"
                                        "If not provided to compute a verifier, the --spake2p-verifier must be provided." },
      { 0, "spake2p-verifier",    true, "b64_PASE_verifier;A raw concatenation of 'W0' and 'L' (67 bytes) as base64 to override the verifier\n"
                                            "Auto-computed from the passcode, if provided" },
      { 0, "spake2p-salt",        true, "b64_PASE_salt;16-32 bytes of salt to use for the PASE verifier, as base64. If omitted, will be generated randomly" },
      { 0, "spake2p-iterations",  true, "iterations;Number of PBKDF iterations to use." },
      { 0, "matter-tcp-port",     true, "port;matter TCP port (secured)" },
      { 0, "matter-udp-port",     true, "arg;matter UDP port (unsecured)" },
      { 0, "interface-id",        true, "interfaceid;A interface id to advertise on" },
      { 0, "PICS",                true, "filepath;A file containing PICS items" },
      { 0, "KVS",                 true, "filepath;A file to store Key Value Store items" },
      #if CHIP_CONFIG_TRANSPORT_TRACE_ENABLED
      { 0, "trace_file",          true, "filepath;Output trace data to the provided file." },
      { 0, "trace_log",           false, "enables traces to go to the log" },
      { 0, "trace_decode",        false, "enables traces decoding" },
      #endif // CHIP_CONFIG_TRANSPORT_TRACE_ENABLED
      // p44mbrd command line args
      { 0, "bridgeapihost",       true, "host;host of the bridge API, default is " DEFAULT_BRIDGE_HOST },
      { 0, "bridgeapiport",       true, "port;port of the bridge API, default is " DEFAULT_BRIDGE_SERVICE },
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
    if (mChipAppInitialized) {
      chipAppCleanup();
    }
  }


  virtual void initialize() override
  {
    OLOG(LOG_NOTICE, "p44: p44utils mainloop started");
    connectBridgeApi();
  }


  // MARK: bridge API

  void connectBridgeApi()
  {
    const char* bridgeapihost = DEFAULT_BRIDGE_HOST;
    const char* bridgeapiservice = DEFAULT_BRIDGE_SERVICE;
    getStringOption("bridgeapihost", bridgeapihost);
    getStringOption("bridgeapiservice", bridgeapiservice);
    BridgeApi::api().setConnectionParams(bridgeapihost, bridgeapiservice, SOCK_STREAM);
    BridgeApi::api().setNotificationHandler(boost::bind(&P44mbrd::bridgeApiNotificationHandler, this, _1, _2));
    BridgeApi::api().connectBridgeApi(boost::bind(&P44mbrd::bridgeApiConnectedHandler, this, _1));
  }


  void bridgeApiConnectedHandler(ErrorPtr aStatus)
  {
    if (Error::notOK(aStatus)) {
      OLOG(LOG_WARNING, "bridge API connection error: %s", aStatus->text());
      // TODO: better handling
      OLOG(LOG_WARNING, "(re)connected bridge API, device info might be stale");
      return;
    }
    else {
      // connection established
      queryBridge();
    }
  }

  // MARK: query and setup bridgeable devices

  void queryBridge()
  {
    JsonObjectPtr params = JsonObject::objFromText(
      "{ \"method\":\"getProperty\", \"dSUID\":\"root\", \"query\":{ "
      "\"dSUID\":null, \"model\":null, \"x-p44-deviceHardwareId\":null, "
      "\"x-p44-vdcs\": { \"*\":{ \"x-p44-devices\": { \"*\": "
      "{\"dSUID\":null, \"name\":null, \"outputDescription\":null, \"function\": null, \"x-p44-zonename\": null, "
      "\"vendorName\":null, \"model\":null, \"configURL\":null, "
      "\"channelStates\":null, "
      "\"active\":null, \"x-p44-bridgeable\":null, \"x-p44-bridged\":null }} }} }}"
    );
    BridgeApi::api().call("getProperty", params, boost::bind(&P44mbrd::bridgeApiCollectQueryHandler, this, _1, _2));
  }


  void bridgeApiCollectQueryHandler(ErrorPtr aError, JsonObjectPtr aJsonMsg)
  {
    OLOG(LOG_INFO, "bridge query: status=%s, answer=%s", Error::text(aError), JsonObject::text(aJsonMsg));
    JsonObjectPtr o;
    JsonObjectPtr result;
    if (aJsonMsg && aJsonMsg->get("result", result)) {
      // global infos
      if (result->get("dSUID", o)) {
        mP44dbrDeviceInstanceInfoProvider.mDSUID = o->stringValue();
      }
      if (result->get("name", o)) {
        mP44dbrDeviceInstanceInfoProvider.mLabel = o->stringValue();
      }
      if (result->get("model", o)) {
        mP44dbrDeviceInstanceInfoProvider.mProductName = o->stringValue();
      }
      if (result->get("x-p44-deviceHardwareId", o)) {
        mP44dbrDeviceInstanceInfoProvider.mSerial = o->stringValue();
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
                            OLOG(LOG_NOTICE, "found light device '%s': %s, outputfunction=%d", name.c_str(), dsuid.c_str(), outputfunction);
                            // enable it for bridging
                            JsonObjectPtr params = JsonObject::newObj();
                            params->add("dSUID", JsonObject::newString(dsuid));
                            JsonObjectPtr props = JsonObject::newObj();
                            props->add("x-p44-bridged", JsonObject::newBool(true));
                            params->add("properties", props);
                            // no callback, but will wait when bridgeapi is in standalone mode
                            BridgeApi::api().call("setProperty", params, NoOP);
                            // create to-be-bridged device
                            DevicePtr dev;
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
                              mDeviceDSUIDMap[dsuid] = dev;
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
    OLOG(LOG_NOTICE, "End of bridge API setup, starting CHIP now");
    // - start but unwind call stack before
    MainLoop::currentMainLoop().executeNow(boost::bind(&P44mbrd::startChip, this));
  }


  void startChip()
  {
    ErrorPtr err;
    err = chipAppInit();
    if (Error::notOK(err)) {
      OLOG(LOG_ERR, "chipAppInit failed: %s ", err->text());
      terminateApp(EXIT_FAILURE);
      return;
    }
    installBridgedDevices();
  }


  void installBridgedDevices()
  {
    // MARK: - p44mbrd specific setup before entering chip mainloop

    // Set starting endpoint id where dynamic endpoints will be assigned, which
    // will be the next consecutive endpoint id after the last fixed endpoint.
    mFirstDynamicEndpointId = static_cast<chip::EndpointId>(
      static_cast<int>(emberAfEndpointFromIndex(static_cast<uint16_t>(emberAfFixedEndpointCount() - 1))) + 1);

    // Disable last fixed endpoint, which is used as a placeholder for all of the
    // supported clusters so that ZAP will generated the requisite code.
    emberAfEndpointEnableDisable(emberAfEndpointFromIndex(static_cast<uint16_t>(emberAfFixedEndpointCount() - 1)), false);

    // Clear out the array of dynamic endpoints
    memset(mDevices, 0, sizeof(mDevices));
    CHIP_ERROR chiperr;
    chip::DeviceLayer::PersistedStorage::KeyValueStoreManager &kvs = chip::DeviceLayer::PersistedStorage::KeyValueStoreMgr();
    // get list of endpoints known in use
    char dynamicEndPointMap[CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT+1];
    EndpointId numDynamicEndPoints = 0;
    size_t nde;
    chiperr = kvs.Get(kP44mbrNamespace "endPointMap", dynamicEndPointMap, CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT, &nde);
    if (chiperr==CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND) {
      // no endpoint map yet
    }
    else if (chiperr==CHIP_NO_ERROR){
      numDynamicEndPoints = static_cast<EndpointId>(nde);
    }
    else {
      LogErrorOnFailure(chiperr);
    }
    dynamicEndPointMap[numDynamicEndPoints]=0; // null terminate for easy debug printing as string
    OLOG(LOG_INFO,"DynamicEndpointMap: %s", dynamicEndPointMap);
    // revert all endpoint map markers to "unconfimed"
    for (size_t i=0; i<numDynamicEndPoints; i++) {
      if (dynamicEndPointMap[i]=='D') dynamicEndPointMap[i]='d'; // unconfirmed device
    }
    // process list of to-be-bridged devices
    for (DeviceDSUIDMap::iterator pos = mDeviceDSUIDMap.begin(); pos!=mDeviceDSUIDMap.end(); ++pos) {
      DevicePtr dev = pos->second;
      // look up previous endpoint mapping
      string key = kP44mbrNamespace "devices/"; key += dev->bridgedDSUID();
      EndpointId dynamicEndpointIdx = kInvalidEndpointId;
      chiperr = kvs.Get(key.c_str(), &dynamicEndpointIdx);
      if (chiperr==CHIP_NO_ERROR) {
        // try to re-use the endpoint ID for this dSUID
        // - sanity check
        if (dynamicEndpointIdx>=numDynamicEndPoints || dynamicEndPointMap[dynamicEndpointIdx]!='d') {
          OLOG(LOG_ERR, "p44 inconsistent mapping info: dynamic endpoint #%d should be mapped as it is in use by device %s", dynamicEndpointIdx, dev->bridgedDSUID().c_str());
        }
        if (dynamicEndpointIdx>=CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT) {
          OLOG(LOG_ERR, "dynamic endpoint #%d for device %s exceeds max dynamic endpoint count -> try to add with new endpointId", dynamicEndpointIdx, dev->bridgedDSUID().c_str());
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
      else if (chiperr==CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND) {
        // we haven't seen that dSUID yet -> need to assign it a new endpoint ID
        OLOG(LOG_NOTICE, "p44 new device %s, adding to bridge", dev->bridgedDSUID().c_str());
      }
      else {
        LogErrorOnFailure(chiperr);
      }
      // update and possibly extend endpoint map
      if (dynamicEndpointIdx==kInvalidEndpointId) {
        // this device needs a new endpoint
        for (EndpointId i=0; i<numDynamicEndPoints; i++) {
          if (dynamicEndPointMap[i]==' ') {
            // use the gap
            dynamicEndpointIdx = i;
            dynamicEndPointMap[dynamicEndpointIdx] = 'D'; // add as confirmed device
          }
        }
        if (dynamicEndpointIdx==kInvalidEndpointId) {
          if (numDynamicEndPoints>=CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT) {
            OLOG(LOG_ERR, "p44 max number of dynamic endpoints exhausted -> cannot add new device %s", dev->bridgedDSUID().c_str());
          }
          else {
            dynamicEndpointIdx = numDynamicEndPoints++;
            dynamicEndPointMap[dynamicEndpointIdx] = 'D'; // add as confirmed device
            // save in KVS
            chiperr = kvs.Put(key.c_str(), dynamicEndpointIdx);
            LogErrorOnFailure(chiperr);
          }
        }
      }
      // assign to dynamic endpoint array
      if (dynamicEndpointIdx!=kInvalidEndpointId) {
        dev->SetDynamicEndpointIdx(dynamicEndpointIdx);
        mDevices[dynamicEndpointIdx] = dev.get();
      }
    }
    // save updated endpoint map
    chiperr = kvs.Put(kP44mbrNamespace "endPointMap", dynamicEndPointMap, numDynamicEndPoints);
    LogErrorOnFailure(chiperr);

    // Add the devices as dynamic endpoints
    for (size_t i=0; i<CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT; i++) {
      if (mDevices[i]) {
        // there is a device at this dynamic endpoint
        if (mDevices[i]->AddAsDeviceEndpoint(mFirstDynamicEndpointId, MATTER_BRIDGE_ENDPOINT)) {
          // give device chance to do things before mainloop starts
          mDevices[i]->beforeChipMainloopPrep();
        }
      }
    }
  }


  void bridgeApiNotificationHandler(ErrorPtr aError, JsonObjectPtr aJsonMsg)
  {
    if (Error::isOK(aError)) {
      // handle push notifications
      JsonObjectPtr o;
      string targetDSUID;
      if (aJsonMsg && aJsonMsg->get("dSUID", o, true)) {
        targetDSUID = o->stringValue();
        // search for device
        DeviceDSUIDMap::iterator devpos = mDeviceDSUIDMap.find(targetDSUID);
        if (devpos!=mDeviceDSUIDMap.end()) {
          // device exists, dispatch
          if (aJsonMsg->get("notification", o, true)) {
            string notification = o->stringValue();
            OLOG(LOG_NOTICE, "p44 Bridge sent notification '%s' for device %s", notification.c_str(), targetDSUID.c_str());
            bool handled = devpos->second->handleBridgeNotification(notification, aJsonMsg);
            if (handled) {
              devpos->second->logStatus("processed notification");
            }
            else {
              OLOG(LOG_ERR, "p44 Could not handle bridge notification '%s' for device %s", notification.c_str(), targetDSUID.c_str());
            }
          }
          else {
            OLOG(LOG_ERR, "p44 Bridge sent unknown message type for device %s", targetDSUID.c_str());
          }
        }
        else {
          OLOG(LOG_ERR, "p44 Bridge sent message for unknown device %s", targetDSUID.c_str());
        }
      }
    }
    else {
      OLOG(LOG_ERR, "p44 Bridge Error %s", aError->text());
    }
  }


  void whenChipMainloopStartedWork()
  {
    for (size_t i=0; i<CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT; i++) {
      if (mDevices[i]) {
        // give device chance to do things just after mainloop has started
        mDevices[i]->inChipMainloopInit();
        // dump status
        mDevices[i]->logStatus();
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

    ConfigurationMgr().LogDeviceConfig();

    OLOG(LOG_NOTICE, "==== Onboarding payload for %s Commissioning Flow ====",
      onBoardingPayload.commissioningFlow == chip::CommissioningFlow::kStandard ? "STANDARD" : "USER-ACTION or CUSTOM"
    );
    PrintOnboardingCodes(onBoardingPayload);

    // let the bridged app know
    char payloadBuffer[chip::QRCodeBasicSetupPayloadGenerator::kMaxQRCodeBase38RepresentationLength + 1];
    chip::MutableCharSpan qrCode(payloadBuffer);
    if (GetQRCode(qrCode, onBoardingPayload) == CHIP_NO_ERROR) {
      BridgeApi::api().setProperty("root", "x-p44-bridge.qrcodedata", JsonObject::newString(qrCode.data()));
      // FIXME: figure out if actually commissionable or not
      BridgeApi::api().setProperty("root", "x-p44-bridge.commissionable", JsonObject::newBool(true));
    }

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
    if (getIntOption("interface-id", i)) serverInitParams.interfaceId = Inet::InterfaceId(static_cast<chip::Inet::InterfaceId::PlatformType>(i));

    // We need to set DeviceInfoProvider before Server::Init to setup the storage of DeviceInfoProvider properly.
    DeviceLayer::SetDeviceInfoProvider(&mExampleDeviceInfoProvider);

    // Init ZCL Data Model and CHIP App Server
    Server::GetInstance().Init(serverInitParams);

    // prepare the storage delegate
    mExampleDeviceInfoProvider.SetStorageDelegate(&chip::Server::GetInstance().GetPersistentStorage());

    // done, ready to run
    mChipAppInitialized = true;
    return err;
  }


  void chipAppCleanup()
  {
    Server::GetInstance().Shutdown();
    DeviceLayer::PlatformMgr().Shutdown();
    #if CHIP_CONFIG_TRANSPORT_TRACE_ENABLED
    chip::trace::DeInitTrace();
    #endif // CHIP_CONFIG_TRANSPORT_TRACE_ENABLED
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
    LOG(LOG_NOTICE,
      "p44 Endpoint %d [%s]: read external attr 0x%04x in cluster 0x%04x, expecting %d bytes",
      (int)endpoint, dev->GetName().c_str(), (int)attributeMetadata->attributeId, (int)clusterId, (int)maxReadLength
    );
    ret = dev->HandleReadAttribute(clusterId, attributeMetadata->attributeId, buffer, maxReadLength);
    if (ret!=EMBER_ZCL_STATUS_SUCCESS) {
      LOG(LOG_ERR, "p44 Endpoint %d: Attribute read not handled!", (int)endpoint);
    }
    else {
      dev->logStatus("processed attribute read");
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
    LOG(LOG_NOTICE, "p44 Endpoint %d [%s]: write external attr 0x%04x in cluster 0x%04x", (int)endpoint, dev->GetName().c_str(), (int)attributeMetadata->attributeId, (int)clusterId);
    ret = dev->HandleWriteAttribute(clusterId, attributeMetadata->attributeId, buffer);
    if (ret!=EMBER_ZCL_STATUS_SUCCESS) {
      LOG(LOG_ERR, "p44 Endpoint %d: Attribute write not handled!", (int)endpoint);
    }
    else {
      dev->logStatus("processed attribute write");
    }
  }
  return ret;
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
