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
#include "main.h"

// plan44
#include "bridgeapi.h"
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

const int kNodeLabelSize = 32;
// Current ZCL implementation of Struct uses a max-size array of 254 bytes
const int kDescriptorAttributeArraySize = 254;

//EndpointId gCurrentEndpointId;
EndpointId gFirstDynamicEndpointId;

Device * gDevices[CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT];
typedef std::map<string, DevicePtr> DeviceDSUIDMap;
DeviceDSUIDMap gDeviceDSUIDMap;

} // namespace


// MARK: - attribute access handlers

EmberAfStatus emberAfExternalAttributeReadCallback(EndpointId endpoint, ClusterId clusterId,
                                                   const EmberAfAttributeMetadata * attributeMetadata, uint8_t * buffer,
                                                   uint16_t maxReadLength)
{
    uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(endpoint);

    EmberAfStatus ret = EMBER_ZCL_STATUS_FAILURE;

    if ((endpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT) && (gDevices[endpointIndex] != nullptr))
    {
      Device * dev = gDevices[endpointIndex];
      if (dev) {
        ChipLogProgress(DeviceLayer,
          "Endpoint %d [%s]: read external attr 0x%04x in cluster 0x%04x, expecting %d bytes",
          (int)endpoint, dev->GetName().c_str(), (int)attributeMetadata->attributeId, (int)clusterId, (int)maxReadLength
        );
        ret = dev->HandleReadAttribute(clusterId, attributeMetadata->attributeId, buffer, maxReadLength);
        if (ret!=EMBER_ZCL_STATUS_SUCCESS) ChipLogError(DeviceLayer, "- Attribute read not handled!");
      }
    }

    return ret;
}


EmberAfStatus emberAfExternalAttributeWriteCallback(EndpointId endpoint, ClusterId clusterId,
                                                    const EmberAfAttributeMetadata * attributeMetadata, uint8_t * buffer)
{
    uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(endpoint);

    EmberAfStatus ret = EMBER_ZCL_STATUS_FAILURE;

    if (endpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
    {
      Device * dev = gDevices[endpointIndex];
      if (dev) {
        ChipLogProgress(DeviceLayer, "Endpoint %d [%s]: write external attr 0x%04x in cluster 0x%04x", (int)endpoint, dev->GetName().c_str(), (int)attributeMetadata->attributeId, (int)clusterId);
        ret = dev->HandleWriteAttribute(clusterId, attributeMetadata->attributeId, buffer);
        if (ret!=EMBER_ZCL_STATUS_SUCCESS) ChipLogError(DeviceLayer, "- Attribute write not handled!");
      }
    }

    return ret;
}


DevicePtr deviceForEndPointId(EndpointId aEndpointId)
{
  uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(aEndpointId);
  if (endpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT) {
    return gDevices[endpointIndex];
  }
  return DevicePtr();
}



void ApplicationInit() {}


#define POLL_INTERVAL_MS (100)
uint8_t poll_prescale = 0;

bool kbhit()
{
    int byteswaiting;
    ioctl(0, FIONREAD, &byteswaiting);
    return byteswaiting > 0;
}


// MARK: - P44 additions

// TODO: clean up once we don't need the pre-running bridge query (aka standalone mode)
const bool standalone = true;
int chipmain(int argc, char * argv[]);
int gArgc;
char ** gArgv;


void answerreceived(JsonObjectPtr aJsonMsg, ErrorPtr aError)
{
  LOG(LOG_NOTICE, "method call status=%s, answer=%s", Error::text(aError), JsonObject::text(aJsonMsg));
  JsonObjectPtr o;
  if (aJsonMsg && aJsonMsg->get("result", o)) {
    // process device list
    JsonObjectPtr vdcs;
    if (o->get("x-p44-vdcs", vdcs)) {
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
                  string zone;
                  if (device->get("name", o)) name = o->stringValue(); // optional
                  if (device->get("x-p44-zonename", o)) zone = o->stringValue(); // optional
                  // determine device type
                  JsonObjectPtr outputdesc;
                  if (device->get("outputDescription", outputdesc)) {
                    // output device
                    if (outputdesc->get("x-p44-behaviourType", o)) {
                      if (o->stringValue()=="light") {
                        // this is a light device
                        if (outputdesc->get("function", o)) {
                          int outputfunction = (int)o->int32Value();
                          LOG(LOG_NOTICE, "found light device '%s' in zone='%s': %s, outputfunction=%d", name.c_str(), zone.c_str(), dsuid.c_str(), outputfunction);
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
                              dev = new DeviceOnOff(dsuid);
                              break;
                            case 1: // effective value dimmer - single channel 0..100
                              dev = new DeviceLevelControl(dsuid);
                              break;
                            case 3: // dimmer with color temperature - channels 1 and 4
                            case 4: // full color dimmer - channels 1..6
                              dev = new DeviceColorControl(dsuid, outputfunction==3 /* ctOnly */);
                              break;
                          }
                          if (dev) {
                            gDeviceDSUIDMap[dsuid] = dev;
                            // additional setup
                            dev->initName(name);
                            dev->initZone(zone);
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
  if (standalone) {
    // start chip only now
    BridgeApi::sharedBridgeApi().endStandalone();
    LOG(LOG_NOTICE, "End of standalone mode, starting CHIP now");
    chipmain(gArgc, gArgv);
  }
}


void apinotification(JsonObjectPtr aJsonMsg, ErrorPtr aError)
{
  LOG(LOG_NOTICE, "notification status=%s, answer=%s", Error::text(aError), JsonObject::text(aJsonMsg));
  // continue waiting
  if (aJsonMsg && aJsonMsg->stringValue()=="quit") {
    return;
  }
  BridgeApi::sharedBridgeApi().handleSocketEvents();
}


void apiconnected(JsonObjectPtr aJsonMsg, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    // successful connection
    // - get list of devices
    JsonObjectPtr params = JsonObject::objFromText(
      "{ \"method\":\"getProperty\", \"dSUID\":\"root\", \"query\":{ \"x-p44-vdcs\": { \"*\":{ \"x-p44-devices\": { \"*\": "
      "{\"dSUID\":null, \"name\":null, \"outputDescription\":null, \"function\": null, \"x-p44-zonename\": null, "
      "\"x-p44-bridgeable\":null, \"x-p44-bridged\":null }} }} }}"
    );
    BridgeApi::sharedBridgeApi().call("getProperty", params, answerreceived);
  }
  else {
    LOG(LOG_ERR, "error connecting bridge API: %s", aError->text());
  }
}


void initializeP44(chip::System::Layer * aLayer, void * aAppState)
{
  if (!standalone) {
    // run it in parallel
    BridgeApi::sharedBridgeApi().endStandalone();
    LOG(LOG_NOTICE, "Running bridge API in parallel mode from beginning");
    BridgeApi::sharedBridgeApi().connect(apiconnected);
  }
}


// MARK: - entry point

int main(int argc, char * argv[])
{
  gArgc = argc;
  gArgv = argv;

  // try bridge API
  BridgeApi::sharedBridgeApi().setConnectionParams("127.0.0.1", 4444, 10*Second);
  BridgeApi::sharedBridgeApi().setIncomingMessageCallback(apinotification);
  if (standalone) {
    BridgeApi::sharedBridgeApi().connect(apiconnected);
    // we'll never return if everything goes well
    return 0;
  }
  else {
    chipmain(gArgc, gArgv);
  }
}


// MARK: - chipmain

#define kP44mbrNamespace "/ch.plan44.p44mbrd/"

int chipmain(int argc, char * argv[])
{

  if (ChipLinuxAppInit(argc, argv) != 0)
  {
      return -1;
  }

  // Init Data Model and CHIP App Server
  static chip::CommonCaseDeviceServerInitParams initParams;
  (void) initParams.InitializeStaticResourcesBeforeServerInit();

#if CHIP_DEVICE_ENABLE_PORT_PARAMS
  // use a different service port to make testing possible with other sample devices running on same host
  initParams.operationalServicePort = LinuxDeviceOptions::GetInstance().securedDevicePort;
#endif

  initParams.interfaceId = LinuxDeviceOptions::GetInstance().interfaceId;
  chip::Server::GetInstance().Init(initParams);

  // Initialize device attestation config
  SetDeviceAttestationCredentialsProvider(Examples::GetExampleDACProvider());

  // Set starting endpoint id where dynamic endpoints will be assigned, which
  // will be the next consecutive endpoint id after the last fixed endpoint.
  gFirstDynamicEndpointId = static_cast<chip::EndpointId>(
      static_cast<int>(emberAfEndpointFromIndex(static_cast<uint16_t>(emberAfFixedEndpointCount() - 1))) + 1);
  //gCurrentEndpointId = gFirstDynamicEndpointId;

  // Disable last fixed endpoint, which is used as a placeholder for all of the
  // supported clusters so that ZAP will generated the requisite code.
  emberAfEndpointEnableDisable(emberAfEndpointFromIndex(static_cast<uint16_t>(emberAfFixedEndpointCount() - 1)), false);


  // MARK: p44 code for generating dynamic endpoints

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
    string key = kP44mbrNamespace "devices/"; key += dev->mBridgedDSUID;
    EndpointId dynamicEndpointIdx = kInvalidEndpointId;
    cerr = kvs.Get(key.c_str(), &dynamicEndpointIdx);
    if (cerr==CHIP_NO_ERROR) {
      // try to re-use the endpoint ID for this dSUID
      // - sanity check
      if (dynamicEndpointIdx>=numDynamicEndPoints || dynamicEndPointMap[dynamicEndpointIdx]!='d') {
        LOG(LOG_WARNING, "inconsistent mapping info: dynamic endpoint #%d should be mapped as it is in use by device %s", dynamicEndpointIdx, dev->mBridgedDSUID.c_str());
      }
      if (dynamicEndpointIdx>=CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT) {
        LOG(LOG_ERR, "dynamic endpoint #%d for device %s exceeds max dynamic endpoint count -> try to add with new endpointId", dynamicEndpointIdx, dev->mBridgedDSUID.c_str());
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
      LOG(LOG_NOTICE, "new device %s, adding to bridge", dev->mBridgedDSUID.c_str());
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
          LOG(LOG_ERR, "max number of dynamic endpoints exhausted -> cannot add new device %s", dev->mBridgedDSUID.c_str());
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
      if (gDevices[i]->AddAsDeviceEndpoint(gFirstDynamicEndpointId)) {
        // make sure reachable and name get reported
        gDevices[i]->SetReachable(true);
      }
    }
  }

  // FIXME: crashes, probably not safe with dynamic endpoints
  //emberAfPrintAttributeTable();

  // Run CHIP

  //chip::DeviceLayer::SystemLayer().ScheduleWork(initializeP44, NULL);
  initializeP44(NULL, NULL);
  chip::DeviceLayer::PlatformMgr().RunEventLoop();

  return 0;
}
