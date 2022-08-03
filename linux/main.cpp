/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

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
#include "Device.h"
#include "main.h"

// plan44
#include "bridgeapi.hpp"

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

// ENDPOINT DEFINITIONS:
// =================================================================================
//
// Endpoint definitions will be reused across multiple endpoints for every instance of the
// endpoint type.
// There will be no intrinsic storage for the endpoint attributes declared here.
// Instead, all attributes will be treated as EXTERNAL, and therefore all reads
// or writes to the attributes must be handled within the emberAfExternalAttributeWriteCallback
// and emberAfExternalAttributeReadCallback functions declared herein. This fits
// the typical model of a bridge, since a bridge typically maintains its own
// state database representing the devices connected to it.

// Device types for dynamic endpoints: TODO Need a generated file from ZAP to define these!
// (taken from chip-devices.xml)
#define DEVICE_TYPE_BRIDGED_NODE 0x0013
// (taken from lo-devices.xml)
#define DEVICE_TYPE_LO_ON_OFF_LIGHT 0x0100
// (taken from matter-devices.xml)
#define DEVICE_TYPE_MA_DIMMABLE_LIGHT 0x0101
// (taken from matter-devices.xml) FIXME: is it only "color temperature"? or just not extended (0x010D) color?
#define DEVICE_TYPE_MA_COLOR_LIGHT 0x010C

// Device Version for dynamic endpoints:
#define DEVICE_VERSION_DEFAULT 1



// MARK: bridged device common declarations

// Declare Descriptor cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(descriptorAttrs)
    DECLARE_DYNAMIC_ATTRIBUTE(ZCL_DEVICE_LIST_ATTRIBUTE_ID, ARRAY, kDescriptorAttributeArraySize, 0), /* device list */
    DECLARE_DYNAMIC_ATTRIBUTE(ZCL_SERVER_LIST_ATTRIBUTE_ID, ARRAY, kDescriptorAttributeArraySize, 0), /* server list */
    DECLARE_DYNAMIC_ATTRIBUTE(ZCL_CLIENT_LIST_ATTRIBUTE_ID, ARRAY, kDescriptorAttributeArraySize, 0), /* client list */
    DECLARE_DYNAMIC_ATTRIBUTE(ZCL_PARTS_LIST_ATTRIBUTE_ID, ARRAY, kDescriptorAttributeArraySize, 0),  /* parts list */
DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// Declare Bridged Device Basic information cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(bridgedDeviceBasicAttrs)
    DECLARE_DYNAMIC_ATTRIBUTE(ZCL_NODE_LABEL_ATTRIBUTE_ID, CHAR_STRING, kNodeLabelSize, 0), /* NodeLabel */
    DECLARE_DYNAMIC_ATTRIBUTE(ZCL_REACHABLE_ATTRIBUTE_ID, BOOLEAN, 1, 0),               /* Reachable */
    DECLARE_DYNAMIC_ATTRIBUTE(ZCL_FEATURE_MAP_SERVER_ATTRIBUTE_ID, BITMAP32, 4, 0),     /* feature map */
DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();


// MARK: onOff cluster declarations

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(onOffAttrs)
    DECLARE_DYNAMIC_ATTRIBUTE(ZCL_ON_OFF_ATTRIBUTE_ID, BOOLEAN, 1, 0), /* on/off */
DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// Declare Cluster List for Bridged Light endpoint
// TODO: It's not clear whether it would be better to get the command lists from
// the ZAP config on our last fixed endpoint instead.
constexpr CommandId onOffIncomingCommands[] = {
    app::Clusters::OnOff::Commands::Off::Id,
    app::Clusters::OnOff::Commands::On::Id,
    app::Clusters::OnOff::Commands::Toggle::Id,
    app::Clusters::OnOff::Commands::OffWithEffect::Id,
    app::Clusters::OnOff::Commands::OnWithRecallGlobalScene::Id,
    app::Clusters::OnOff::Commands::OnWithTimedOff::Id,
    kInvalidCommandId,
};


// MARK: level control cluster declarations

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(levelControlAttrs)
    DECLARE_DYNAMIC_ATTRIBUTE(ZCL_CURRENT_LEVEL_ATTRIBUTE_ID, INT8U, 1, 0), /* current level */
DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

constexpr CommandId levelControlIncomingCommands[] = {
    app::Clusters::LevelControl::Commands::MoveToLevel::Id,
    app::Clusters::LevelControl::Commands::Move::Id,
    app::Clusters::LevelControl::Commands::Step::Id,
    app::Clusters::LevelControl::Commands::Stop::Id,
    app::Clusters::LevelControl::Commands::MoveToLevelWithOnOff::Id,
    app::Clusters::LevelControl::Commands::MoveWithOnOff::Id,
    app::Clusters::LevelControl::Commands::StepWithOnOff::Id,
    app::Clusters::LevelControl::Commands::StopWithOnOff::Id,
//    app::Clusters::LevelControl::Commands::MoveToClosestFrequency::Id,
    kInvalidCommandId,
};


// MARK: color control cluster declarations

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(colorControlAttrs)
    DECLARE_DYNAMIC_ATTRIBUTE(ZCL_COLOR_CONTROL_CURRENT_HUE_ATTRIBUTE_ID, INT8U, 1, 0), /* current hue */
    DECLARE_DYNAMIC_ATTRIBUTE(ZCL_COLOR_CONTROL_CURRENT_SATURATION_ATTRIBUTE_ID, INT8U, 1, 0), /* current saturation */
    DECLARE_DYNAMIC_ATTRIBUTE(ZCL_COLOR_CONTROL_COLOR_TEMPERATURE_ATTRIBUTE_ID, INT16U, 1, 0), /* current color temperature */
    DECLARE_DYNAMIC_ATTRIBUTE(ZCL_COLOR_CONTROL_COLOR_MODE_ATTRIBUTE_ID, ENUM8, 1, 0), /* current color mode: 0=HS, 1=XY, 2=Colortemp */
//    DECLARE_DYNAMIC_ATTRIBUTE(ZCL_COLOR_CONTROL_ENHANCED_CURRENT_HUE_ATTRIBUTE_ID, ENUM8, 1, 0), /* current color mode: 0=HS, 1=XY, 2=Colortemp */

DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();
// TODO: other important capabilities
// ZCL_COLOR_CONTROL_COLOR_CAPABILITIES_ATTRIBUTE_ID type MAP16 (Bit0=HS, Bit1=EnhancedHS, Bit2=ColorLoop, Bit3=XY, Bit4=ColorTemp)

constexpr CommandId colorControlIncomingCommands[] = {
    app::Clusters::ColorControl::Commands::MoveToHue::Id,
    app::Clusters::ColorControl::Commands::MoveHue::Id,
    app::Clusters::ColorControl::Commands::StepHue::Id,
    app::Clusters::ColorControl::Commands::MoveToSaturation::Id,
    app::Clusters::ColorControl::Commands::MoveSaturation::Id,
    app::Clusters::ColorControl::Commands::StepSaturation::Id,
    app::Clusters::ColorControl::Commands::MoveToHueAndSaturation::Id,
    app::Clusters::ColorControl::Commands::MoveToColorTemperature::Id,
    app::Clusters::ColorControl::Commands::MoveColorTemperature::Id,
    app::Clusters::ColorControl::Commands::StepColorTemperature::Id,
    kInvalidCommandId,
};


// MARK: - actual endpoints

// MARK: on/off light
DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(onOffLightClusters)
    DECLARE_DYNAMIC_CLUSTER(ZCL_ON_OFF_CLUSTER_ID, onOffAttrs, onOffIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(ZCL_DESCRIPTOR_CLUSTER_ID, descriptorAttrs, nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(ZCL_BRIDGED_DEVICE_BASIC_CLUSTER_ID, bridgedDeviceBasicAttrs, nullptr, nullptr)
DECLARE_DYNAMIC_CLUSTER_LIST_END;

DECLARE_DYNAMIC_ENDPOINT(onOffLightEndpoint, onOffLightClusters);

const EmberAfDeviceType gOnOffLightTypes[] = { { DEVICE_TYPE_LO_ON_OFF_LIGHT, DEVICE_VERSION_DEFAULT },
                                               { DEVICE_TYPE_BRIDGED_NODE, DEVICE_VERSION_DEFAULT } };



// MARK: dimmable light
DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(dimmableLightClusters)
    DECLARE_DYNAMIC_CLUSTER(ZCL_ON_OFF_CLUSTER_ID, onOffAttrs, onOffIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(ZCL_LEVEL_CONTROL_CLUSTER_ID, levelControlAttrs, levelControlIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(ZCL_DESCRIPTOR_CLUSTER_ID, descriptorAttrs, nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(ZCL_BRIDGED_DEVICE_BASIC_CLUSTER_ID, bridgedDeviceBasicAttrs, nullptr, nullptr)
DECLARE_DYNAMIC_CLUSTER_LIST_END;

DECLARE_DYNAMIC_ENDPOINT(dimmableLightEndpoint, dimmableLightClusters);

const EmberAfDeviceType gDimmableLightTypes[] = { { DEVICE_TYPE_MA_DIMMABLE_LIGHT, DEVICE_VERSION_DEFAULT },
                                                  { DEVICE_TYPE_BRIDGED_NODE, DEVICE_VERSION_DEFAULT } };

// MARK: ct/color light
DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(colorLightClusters)
    DECLARE_DYNAMIC_CLUSTER(ZCL_ON_OFF_CLUSTER_ID, onOffAttrs, onOffIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(ZCL_LEVEL_CONTROL_CLUSTER_ID, levelControlAttrs, levelControlIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(ZCL_COLOR_CONTROL_CLUSTER_ID, colorControlAttrs, colorControlIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(ZCL_DESCRIPTOR_CLUSTER_ID, descriptorAttrs, nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(ZCL_BRIDGED_DEVICE_BASIC_CLUSTER_ID, bridgedDeviceBasicAttrs, nullptr, nullptr)
DECLARE_DYNAMIC_CLUSTER_LIST_END;

DECLARE_DYNAMIC_ENDPOINT(colorLightEndpoint, colorLightClusters);

const EmberAfDeviceType gColorLightTypes[] = { { DEVICE_TYPE_MA_COLOR_LIGHT, DEVICE_VERSION_DEFAULT },
                                               { DEVICE_TYPE_BRIDGED_NODE, DEVICE_VERSION_DEFAULT } };


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
          (int)endpoint, dev->GetName(), (int)attributeMetadata->attributeId, (int)clusterId, (int)maxReadLength
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
        ChipLogProgress(DeviceLayer, "Endpoint %d [%s]: write external attr 0x%04x in cluster 0x%04x", (int)endpoint, dev->GetName(), (int)attributeMetadata->attributeId, (int)clusterId);
        ret = dev->HandleWriteAttribute(clusterId, attributeMetadata->attributeId, buffer);
        if (ret!=EMBER_ZCL_STATUS_SUCCESS) ChipLogError(DeviceLayer, "- Attribute write not handled!");
      }
    }

    return ret;
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
                              dev = new DeviceOnOff(name.c_str(), zone, dsuid);
                              dev->setUpClusterInfo(
                                ArraySize(onOffLightClusters),
                                &onOffLightEndpoint,
                                Span<const EmberAfDeviceType>(gOnOffLightTypes),
                                1
                              );
                              break;
                            case 1: // effective value dimmer - single channel 0..100
                              dev = new DeviceDimmable(name.c_str(), zone, dsuid);
                              dev->setUpClusterInfo(
                                ArraySize(dimmableLightClusters),
                                &dimmableLightEndpoint,
                                Span<const EmberAfDeviceType>(gDimmableLightTypes),
                                1
                              );
                              break;
                            case 3: // dimmer with color temperature - channels 1 and 4
                            case 4: // full color dimmer - channels 1..6
                              dev = new DeviceColor(name.c_str(), zone, dsuid, outputfunction==3 /* ctOnly */);
                              dev->setUpClusterInfo(
                                ArraySize(colorLightClusters),
                                &colorLightEndpoint,
                                Span<const EmberAfDeviceType>(gColorLightTypes),
                                1
                              );
                              break;
                          }
                          if (dev) {
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
    // FIXME: probably wrong time to do that, creates error
    dev->SetReachable(true);
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
        // FIXME: do we need this??
//        HandleDeviceStatusChanged(gDevices[i], Device::kChanged_Reachable);
//        HandleDeviceStatusChanged(gDevices[i], Device::kChanged_Name);
      }
    }
  }

  // Run CHIP

  //chip::DeviceLayer::SystemLayer().ScheduleWork(initializeP44, NULL);
  initializeP44(NULL, NULL);
  chip::DeviceLayer::PlatformMgr().RunEventLoop();

  return 0;
}
