//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "p44bridge.h"

#if P44_ADAPTERS

#include "p44devices.h"

using namespace p44;


// MARK: - P44_BridgeImpl

P44_BridgeImpl* gSharedP44BridgeP = nullptr;

P44_BridgeImpl& P44_BridgeImpl::adapter()
{
  if (gSharedP44BridgeP==nullptr) {
    gSharedP44BridgeP = new P44_BridgeImpl;
    assert(gSharedP44BridgeP);
    gSharedP44BridgeP->isMemberVariable();
  }
  return *gSharedP44BridgeP;
}

// MARK: BridgeAdapter API implementation

void P44_BridgeImpl::startup()
{
  api().connectBridgeApi(boost::bind(&P44_BridgeImpl::bridgeApiConnectedHandler, this, _1));
}


void P44_BridgeImpl::reportCommissionable(bool aIsCommissionable)
{
  api().setProperty("root", "x-p44-bridge.commissionable", JsonObject::newBool(aIsCommissionable));
}


void P44_BridgeImpl::updateCommissioningInfo(const string aQRCodeData, const string aManualPairingCode)
{
  api().setProperty("root", "x-p44-bridge.qrcodedata", JsonObject::newString(aQRCodeData));
  api().setProperty("root", "x-p44-bridge.manualpairingcode", JsonObject::newString(aManualPairingCode));
}


void P44_BridgeImpl::setBridgeRunning(bool aRunning)
{
  api().setProperty("root", "x-p44-bridge.started", JsonObject::newBool(aRunning));
}


void P44_BridgeImpl::initialDevicesInstalled()
{
  // now that all devices are there, we can create zones
  updateAllZoneDependencies(UpdateMode(UpdateFlags::forced));
}




void P44_BridgeImpl::identifyBridge(int aDurationS)
{
  // (re)start or stop identify in bridge itself
  JsonObjectPtr params = JsonObject::newObj();
  params->add("dSUID", JsonObject::newString("root"));
  // <0 = stop, >0 = duration (duration==0 would mean default duration, not used here)
  params->add("duration", JsonObject::newDouble(aDurationS<=0 ? -1 : aDurationS));
  api().notify("identify", params);
}


void P44_BridgeImpl::cleanup()
{
  api().closeConnection();
  inherited::cleanup();
}


// MARK: helpers

bool P44_BridgeImpl::hasModelFeature(JsonObjectPtr aDeviceInfo, const char* aModelFeature)
{
  JsonObjectPtr o, o2;
  if (aDeviceInfo->get("modelFeatures", o)) {
    if (o->get(aModelFeature, o2)) {
      if (o2->boolValue()) {
        return true;
      }
    }
  }
  return false;
}


// MARK: zone handling

void P44_BridgeImpl::addOrUpdateZone(DsZoneID aZoneID, const string aZoneName, bool aOverwriteName, UpdateMode aUpdateMode)
{
  ZoneMap::iterator z = mZoneMap.find(aZoneID);
  if (z==mZoneMap.end() || (aOverwriteName && aZoneName!=z->second)) {
    // new entry or explicit zone name change
    mZoneMap[aZoneID] = aZoneName;
    updateZoneDependencies(aZoneID, aUpdateMode);
  }
}


// MARK: P44_BridgeImpl internals

P44_BridgeImpl::P44_BridgeImpl() :
  mConnectedOnce(false)
{
  mBridgeApi.isMemberVariable();
}


void P44_BridgeImpl::setAPIParams(const string aApiHost, const string aApiService)
{
  api().setConnectionParams(aApiHost.c_str(), aApiService.c_str(), SOCK_STREAM);
  api().setNotificationHandler(boost::bind(&P44_BridgeImpl::bridgeApiNotificationHandler, this, _1, _2));
}



void P44_BridgeImpl::bridgeApiConnectedHandler(ErrorPtr aStatus)
{
  if (Error::notOK(aStatus)) {
    OLOG(LOG_WARNING, "bridge API connection error: %s", aStatus->text());
    // TODO: better handling
    // - re-enable all known devices for "bridged", disable those not found
    return;
  }
  else {
    if (mConnectedOnce) {
      OLOG(LOG_WARNING, "(re)connected bridge API");
      reconnectBridgedDevices();
    }
    else {
      // connection established for the first time
      mConnectedOnce = true;
      queryBridge();
    }
  }
}

// MARK: query and setup bridgeable devices

void P44_BridgeImpl::updateBridgeStatus(bool aStarted)
{
  api().setProperty("root", "x-p44-bridge.bridgetype", JsonObject::newString("matter"));
  api().setProperty("root", "x-p44-bridge.qrcodedata", JsonObject::newString(""));
  api().setProperty("root", "x-p44-bridge.manualpairingcode", JsonObject::newString(""));
  api().setProperty("root", "x-p44-bridge.started", JsonObject::newBool(aStarted));
  api().setProperty("root", "x-p44-bridge.commissionable", JsonObject::newBool(false));
}


#define NEEDED_DEVICE_PROPERTIES \
  "{\"dSUID\":null, \"name\":null, \"function\": null, " \
  "\"zoneID\": null, \"x-p44-zonename\": null, " \
  "\"outputDescription\":null, \"outputSettings\": null, \"modelFeatures\":null, " \
  "\"scenes\": { \"0\":null, \"5\":null }, " \
  "\"vendorName\":null, \"model\":null, \"configURL\":null, \"displayId\":null, " \
  "\"channelStates\":null, \"channelDescriptions\":null, " \
  "\"sensorDescriptions\":null, \"sensorStates\":null, " \
  "\"binaryInputDescriptions\":null, \"binaryInputStates\":null, \"binaryInputSettings\":null, " \
  "\"buttonInputDescriptions\":null, \"buttonInputStates\":null, " \
  "\"active\":null, " \
  "\"x-p44-bridgeable\":null, \"x-p44-bridged\":null, \"x-p44-bridgeAs\":null }"

void P44_BridgeImpl::queryBridge()
{
  // first update (reset) bridge status
  updateBridgeStatus(false);
  // query devices
  JsonObjectPtr params = JsonObject::objFromText(
    "{ \"method\":\"getProperty\", \"dSUID\":\"root\", \"query\":{ "
    "\"dSUID\":null, \"model\":null, \"name\":null, \"x-p44-deviceHardwareId\":null, "
    "\"x-p44-vdcs\": { \"*\":{ \"x-p44-devices\": { \"*\": "
    NEEDED_DEVICE_PROPERTIES
    "} }} }}"
  );
  api().call("getProperty", params, boost::bind(&P44_BridgeImpl::bridgeApiCollectQueryHandler, this, _1, _2));
}

#define SEMANTIC_TAG_COMMON_LOCATION_NS 0x06
#define SEMANTIC_TAG_COMMON_LOCATION_INDOOR 0x00
#define SEMANTIC_TAG_COMMON_LOCATION_OUTDOOR 0x01

using SemanticTag = Clusters::Descriptor::Structs::SemanticTagStruct::Type;

static SemanticTag gInDoorTags[] = {
  { .namespaceID = SEMANTIC_TAG_COMMON_LOCATION_NS, .tag = SEMANTIC_TAG_COMMON_LOCATION_INDOOR }
};
static SemanticTag gOutDoorTags[] = {
  { .namespaceID = SEMANTIC_TAG_COMMON_LOCATION_NS, .tag = SEMANTIC_TAG_COMMON_LOCATION_OUTDOOR }
};


DevicePtr P44_BridgeImpl::bridgedDeviceFromJSON(JsonObjectPtr aDeviceJSON)
{
  JsonObjectPtr o;
  DevicePtr mainDevice;
  if (aDeviceJSON->get("x-p44-bridgeable", o)) {
    if (o->boolValue()) {
      // bridgeable device
      if (aDeviceJSON->get("dSUID", o)) {
        // with dSUID. Now find out what device structure to map towards matter
        std::list<DevicePtr> devices;
        // determine basic parameters for single or composed device
        string dsuid = o->stringValue();
        string name;
        if (aDeviceJSON->get("name", o)) name = o->stringValue(); // optional
        // extract mappable devices
        DevicePtr dev;
        JsonObjectPtr outputdesc = aDeviceJSON->get("outputDescription");
        string behaviourtype;
        JsonObjectPtr groups;
        bool preventOutput = false;
        bool preventInput = false;
        if (outputdesc && outputdesc->get("x-p44-behaviourType", o)) {
          behaviourtype = o->stringValue();
          if (aDeviceJSON->get("outputSettings", o)) {
            groups = o->get("groups");
          }
        }
        // - first check if we have a bridging hint that directly defines the mapping
        if (aDeviceJSON->get("x-p44-bridgeAs", o)) {
          // bridging hint should determine bridged device
          string bridgeAs = o->stringValue();
          if (bridgeAs=="on-off") {
            if (groups && groups->get("2")) dev = new P44_WindowCoveringDevice(); // group_grey_shadow
            else if (behaviourtype=="light" && groups && groups->get("1")) dev = new P44_OnOffLightDevice();
            else dev = new P44_OnOffPluginUnitDevice();
          }
          else if (bridgeAs=="level-control") {
            if (groups && groups->get("2")) dev = new P44_WindowCoveringDevice(); // group_grey_shadow
            else if (behaviourtype=="light" && groups && groups->get("1")) dev = new P44_DimmableLightDevice(); // group_yellow_light
            else dev = new P44_DimmablePluginUnitDevice();
          }
          else if (bridgeAs=="window-covering") {
            dev = new P44_WindowCoveringDevice();
          }
          else if (bridgeAs=="no-output") {
            preventOutput = true;
          }
          else if (bridgeAs=="no-input") {
            preventInput = true;
          }
          if (dev) {
            OLOG(LOG_NOTICE, "found bridgeable device with x-p44-bridgeAs hint '%s': %s", bridgeAs.c_str(), dsuid.c_str());
            P44_DeviceImpl::impl(dev)->initBridgedInfo(aDeviceJSON);
            devices.push_back(dev);
          }
        }
        if (!dev) {
          // no or unknown bridging hint - derive bridged device type(s) automatically
          // First: check output
          if (outputdesc && !preventOutput) {
            if (outputdesc->get("function", o)) {
              int outputfunction = (int)o->int32Value();
              // output device
              if (behaviourtype=="light" && groups && groups->get("1")) { // group_yellow_light
                // this is a light device
                OLOG(LOG_NOTICE, "found bridgeable light device '%s': %s, outputfunction=%d", name.c_str(), dsuid.c_str(), outputfunction);
                switch(outputfunction) {
                  default:
                  case outputFunction_switch: // switch output
                    dev = new P44_OnOffLightDevice();
                    break;
                  case outputFunction_dimmer: // effective value dimmer - single channel 0..100
                    dev = new P44_DimmableLightDevice();
                    break;
                  case outputFunction_ctdimmer: // dimmer with color temperature - channels 1 and 4
                  case outputFunction_colordimmer: // full color dimmer - channels 1..6
                    dev = new P44_ColorLightDevice(outputfunction==outputFunction_ctdimmer /* ctOnly */);
                    break;
                }
              }
              else if (behaviourtype=="shadow" && groups && groups->get("2")) {
                // this is a shadow device
                OLOG(LOG_NOTICE, "found bridgeable shadow device '%s': %s, outputfunction=%d", name.c_str(), dsuid.c_str(), outputfunction);
                dev = new P44_WindowCoveringDevice();
              }
              else if (behaviourtype=="ventilation") {
                // actual ventilation device
                OLOG(LOG_NOTICE, "found bridgeable ventilation behaviour device '%s': %s", name.c_str(), dsuid.c_str());
                // TODO: actually enable when P44_FullFeatureFanDevice exists
                /*
                if (hasModelFeature(aDeviceJSON, "fcu")) {
                  // ventilation device with extra features (louver, auto mode etc)
                  dev = new P44_FullFeatureFanDevice();
                }
                else
                */
                {
                  // simple ventilation device
                  dev = new P44_SimpleFanDevice();
                }
              }
              else if (groups && groups->get("10")) {
                // generic output in the ventilation group -> also model as fan control device
                OLOG(LOG_NOTICE, "found bridgeable standard output in ventilation group device '%s': %s, outputfunction=%d", name.c_str(), dsuid.c_str(), outputfunction);
                dev = new P44_SimpleFanDevice();
              }
              else {
                // not something specific, only switched or dimmed output
                OLOG(LOG_NOTICE, "found bridgeable generic device '%s': %s, outputfunction=%d", name.c_str(), dsuid.c_str(), outputfunction);
                switch(outputfunction) {
                  case outputFunction_switch: // switch output
                    dev = new P44_OnOffPluginUnitDevice();
                    break;
                  default:
                  case outputFunction_dimmer: // effective value dimmer - single channel 0..100
                    dev = new P44_DimmablePluginUnitDevice();
                    break;
                }
              }
            }
            if (dev) {
              P44_DeviceImpl::impl(dev)->initBridgedInfo(aDeviceJSON);
              devices.push_back(dev);
              dev.reset();
            }
          }
          // Second: check inputs
          if (!preventInput) {
            enum { sensor, input, button, numInputTypes };
            const char* inputTypeNames[numInputTypes] = { "sensor", "binaryInput", "buttonInput" };
            for (int inputType = sensor; inputType<numInputTypes; inputType++) {
              JsonObjectPtr inputdescs;
              if (aDeviceJSON->get((string(inputTypeNames[inputType])+"Descriptions").c_str(), inputdescs)) {
                // iterate through this input type's items
                string inputid;
                JsonObjectPtr inputdesc;
                inputdescs->resetKeyIteration();
                bool moreInputs = false; // default to one input per device
                while (inputdescs->nextKeyValue(inputid, inputdesc)) {
                  VdcUsageHint usage = usage_undefined;
                  switch (inputType) {
                    case sensor: {
                      if (inputdesc->get("sensorType", o)) {
                        int sensorType = o->int32Value();
                        // determine sensor type
                        switch(sensorType) {
                          case sensorType_temperature: dev = new P44_TemperatureSensor(); break;
                          case sensorType_humidity: dev = new P44_HumiditySensor(); break;
                          case sensorType_illumination: dev = new P44_IlluminanceSensor(); break;
                        }
                        // get usage if sensor specifies any
                        if (dev && inputdesc->get("sensorUsage", o)) {
                          usage = static_cast<VdcUsageHint>(o->int32Value());
                        }
                      }
                      break;
                    }
                    case input: {
                      // binary inputs are a special case, these also need the settings because configured sensorFunction is relevant
                      JsonObjectPtr inputsettings;
                      JsonObjectPtr inputsetting;
                      JsonObjectPtr sf;
                      if (aDeviceJSON->get((string(inputTypeNames[inputType])+"Settings").c_str(), inputsettings)) {
                        inputsetting = inputsettings->get(inputid.c_str());
                        if (!inputsetting || !inputsetting->get("sensorFunction", sf)) {
                          // no configured function, fall back to hardwareSensorFunction
                          inputdesc->get("sensorFunction", sf);
                        }
                      }
                      if (sf) {
                        int binInpType = sf->int32Value();
                        // determine input type
                        switch(binInpType) {
                          // TODO: maybe some time motion will get separated from occupancy
                          case binInpType_presence:
                          case binInpType_presenceInDarkness:
                          case binInpType_motion:
                          case binInpType_motionInDarkness:
                            // assume PIR, which is essentially motion, but commonly used for presence
                            dev = new P44_OccupancySensor();
                            break;
                          case binInpType_windowHandle:
                            // DS Semantics for Window handle:
                            //   Window closed (0), open (1) [and tilted (2) in extendedValue, value still 1 for that]
                            // Matter Semantics for contact sensor are opposite to that:
                            //   FALSE (0) = open or no contact
                            //   TRUE (1)  = closed or contact
                            // So: use INVERTED contact input
                            dev = new P44_ContactInput();
                            {
                              auto contactDevP = dynamic_cast<P44_ContactInput*>(dev.get());
                              contactDevP->setInverted(true);
                            }
                            break;
                          default:
                            // all others: create simple ContactSensors
                            dev = new P44_ContactInput();
                            break;
                        }
                        // get usage if input specifies any
                        if (dev && inputdesc->get("inputUsage", o)) {
                          usage = static_cast<VdcUsageHint>(o->int32Value());
                        }
                      }
                      break;
                    }
                    case button: {
                      if (inputdesc->get("buttonType", o)) {
                        int buttonType = o->int32Value();
                        int buttonElementID = buttonElement_center; // default to single button/center
                        if (inputdesc->get("buttonElementID", o)) {
                          buttonElementID = o->int32Value();
                        }
                        // determine input type
                        switch(buttonType) {
                          case buttonType_undefined:
                          case buttonType_single:
                            // single pushbutton
                            dev = new P44_Pushbutton();
                            {
                              auto switchDevP = dynamic_cast<SwitchDevice*>(dev.get());
                              if (switchDevP) {
                                // - matter positions (assumed from sample in switch cluster): 1=upper half, 2=lower half
                                switchDevP->setActivePosition(1, inputid);
                              }
                            }
                            break;
                          case buttonType_2way:
                            // two-way rocker
                            if (moreInputs) {
                              // we were waiting for the second half of the rocker, this is it
                              moreInputs = false;
                              // add second position to existing device
                              auto switchDevP = dynamic_cast<SwitchDevice*>(dev.get());
                              if (switchDevP) {
                                // - matter positions (assumed from sample in switch cluster): 1=upper half, 2=lower half
                                switchDevP->setActivePosition(buttonElementID==buttonElement_up ? 1 : 2, inputid);
                              }
                            }
                            else {
                              // we need to have more inputs to form the rocker device
                              moreInputs = true;
                              SwitchDevice* switchDevP = new P44_Pushbutton();
                              dev = switchDevP;
                              // add neutral and first position
                              if (switchDevP) {
                                // - matter positions (assumed from sample in switch cluster): 1=upper half, 2=lower half
                                switchDevP->setActivePosition(buttonElementID==buttonElement_down ? 2 : 1, inputid);
                              }
                            }
                            break;
                        }
                      }
                      break;
                    }
                    default:
                      break;
                  }
                  if (dev && !moreInputs) {
                    // apply usage semantic tags
                    switch (usage) {
                      case usage_undefined: // must NOT have a usage tag!
                      default:
                        break;
                      case usage_room: // indoor
                        dev->setSemanticTags(Span<SemanticTag>(gInDoorTags));
                        break;
                      case usage_outdoors: // outdoor
                        dev->setSemanticTags(Span<SemanticTag>(gOutDoorTags));
                        break;
                    }
                    OLOG(LOG_NOTICE, "found bridgeable input '%s' in device '%s': %s", name.c_str(), inputid.c_str(), dsuid.c_str());
                    P44_DeviceImpl::impl(dev)->initBridgedInfo(aDeviceJSON, inputTypeNames[inputType], inputid.c_str());
                    devices.push_back(dev);
                    dev.reset();
                  }
                } // iterating all inputs of one type
              }
            } // for all input types
          }
        }
        // Now we have a list of matter devices that are contained in this single briged device
        if (!devices.empty()) {
          // at least one
          if (devices.size()==1) {
            // single device, not a composed one
            mainDevice = devices.front();
          }
          else {
            // we need a composed device to represent the multiple devices we have in matter
            ComposedDevice *composedDevice = new P44_ComposedDevice();
            mainDevice = DevicePtr(composedDevice);
            P44_DeviceImpl::impl(mainDevice)->initBridgedInfo(aDeviceJSON); // needs to have the infos, too
            // add the subdevices
            while(!devices.empty()) {
              DevicePtr subdev = devices.front();
              devices.pop_front();
              composedDevice->addSubdevice(subdev);
            }
          }
        }
        if (mainDevice) {
          // add bridge-side representing device (singular or possibly composed) to UID map
          registerInitialDevice(mainDevice);
          // enable it for bridging on the other side
          JsonObjectPtr params = JsonObject::newObj();
          params->add("dSUID", JsonObject::newString(dsuid));
          JsonObjectPtr props = JsonObject::newObj();
          props->add("x-p44-bridged", JsonObject::newBool(true));
          params->add("properties", props);
          // no callback, but will wait when bridgeapi is in standalone mode
          api().call("setProperty", params, NoOP);
        }
      } // has dSUID
    } // if bridgeable
  }
  return mainDevice;
}


void P44_BridgeImpl::bridgeApiCollectQueryHandler(ErrorPtr aError, JsonObjectPtr aJsonMsg)
{
  OLOG(LOG_DEBUG, "initial bridgeapi query: status=%s, answer:\n%s", Error::text(aError), JsonObject::text(aJsonMsg));
  JsonObjectPtr o;
  JsonObjectPtr result;
  if (aJsonMsg && aJsonMsg->get("result", result)) {
    // global infos
    if (result->get("dSUID", o)) {
      mUID = o->stringValue();
    }
    if (result->get("name", o)) {
      mLabel = o->stringValue();
    }
    if (result->get("model", o)) {
      mModel = o->stringValue();
    }
    if (result->get("x-p44-deviceHardwareId", o)) {
      mSerial = o->stringValue();
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
            DevicePtr dev = bridgedDeviceFromJSON(device);
          }
        }
      }
    }
  }
  // create a endpoint list for each known zone
  /*
  // TODO: actually derive actions from rooms and scenes
  // register one endpoint list
  EndpointListInfoPtr endpointList = new EndpointListInfo(
    0xEEEE,
    "TestZoneBridge",
    Actions::EndpointListTypeEnum::kOther
  );
  endpointList->addEndpoint(1); // FIXME: get action endpoint id from somewhere reliableb
  addOrReplaceEndpointsList(endpointList, UpdateMode());
  // register one test action
  ActionPtr testAction = new Action(
    0x4242, // actionId,
    "testAction",
    Actions::ActionTypeEnum::kScene,
    0xEEEE, // FIXME: reference real list
    0x03, // instant and instantWithTransition // FIXME: use names
    Actions::ActionStateEnum::kInactive // FIXME: real value
  );
  addOrReplaceAction(testAction, UpdateMode());
  */
  // report started (ONCE!)
  startupComplete(ErrorPtr());
}


// MARK: - Zones and Actions

void P44_BridgeImpl::updateAllZoneDependencies(UpdateMode aUpdateMode)
{
  for (ZoneMap::iterator zpos = mZoneMap.begin(); zpos!=mZoneMap.end(); ++zpos) {
    updateZoneDependencies(zpos->first, aUpdateMode);
  }
}


void P44_BridgeImpl::updateZoneDependencies(DsZoneID aZoneID, UpdateMode aUpdateMode)
{
  if (aUpdateMode.Has(UpdateFlags::matter) || aUpdateMode.Has(UpdateFlags::forced)) {
    ZoneMap::iterator zpos = mZoneMap.find(aZoneID);
    if (zpos!=mZoneMap.end()) {
      // create an endpoint list of devices in this zone
      EndpointListInfoPtr endpointList = new EndpointListInfo(
        aZoneID, // endpoint list ID is compatible with DS zone ID (both uint16)
        zpos->second, // zone name
        Actions::EndpointListTypeEnum::kRoom // kRoom means device can be in only one, whereas a device can be in multiple kZones
      );
      for (DeviceUIDMap::iterator pos = mDeviceUIDMap.begin(); pos!=mDeviceUIDMap.end(); ++pos) {
        if (P44_DeviceImpl::impl(pos->second)->zoneId()==aZoneID) {
          endpointList->addEndpoint(pos->second->endpointId());
        }
      }
      addOrReplaceEndpointsList(endpointList, aUpdateMode);
      // TODO: actually implement the action
      // FIXME: for now, just generate a deep-off action with same ID as the room
      // register one test action
      ActionPtr deepOffAction = new P44SceneAction(
        aZoneID, // the zone to act
        group_undefined, // deep off affects all
        68, // DEEP_OFF - deep off scene
        aZoneID, // use the zoneID==actionID
        "deep off", // FIXME: better name
        Actions::ActionTypeEnum::kScene,
        aZoneID, // reference the endpointlist for the zone
        0x03, // instant and instantWithTransition // FIXME: use names
        Actions::ActionStateEnum::kInactive // FIXME: real value
      );
      addOrReplaceAction(deepOffAction, aUpdateMode);
      // TODO: generate more different actions for different scenes
      // - generate actions for those groups we have devices for (light, shadow)
      // - need to find a way to generate IDs that do not collide with zone ids
    }  
  }
}


void P44SceneAction::invoke(Optional<uint16_t> aTransitionTime)
{
  // TODO: check: is aTransitionTime time really 1/10th seconds?
  // { "notification":"callScene", "zone_id":0, "group":1, "scene":5, "force":false, "transitionTime":5.5 }
  JsonObjectPtr params = JsonObject::newObj();
  params->add("zone_id", JsonObject::newInt32(mZoneId));
  params->add("group", JsonObject::newInt32(mGroup));
  params->add("scene", JsonObject::newInt32(mSceneNo));
  params->add("force", JsonObject::newBool(false));
  if (aTransitionTime.HasValue()) {
    params->add("transitionTime", params->newDouble((double)aTransitionTime.Value()/10));
  }
  P44_BridgeImpl::adapter().api().notify("callScene", params);
}



// MARK: - reconnect bridge API

#define RECONNECT_DEVICE_PROPERTIES \
  "{\"dSUID\":null, " \
  "\"active\":null, " \
  "\"x-p44-bridgeable\":null, \"x-p44-bridged\":null, }"

void P44_BridgeImpl::reconnectBridgedDevices()
{
  OLOG(LOG_NOTICE, "querying bridgeapi query after reconnect for device status");
  // query devices
  JsonObjectPtr params = JsonObject::objFromText(
    "{ \"method\":\"getProperty\", \"dSUID\":\"root\", \"query\":{ "
    "\"dSUID\":null, \"model\":null, \"name\":null, \"x-p44-deviceHardwareId\":null, "
    "\"x-p44-vdcs\": { \"*\":{ \"x-p44-devices\": { \"*\": "
    RECONNECT_DEVICE_PROPERTIES
    "} }} }}"
  );
  api().call("getProperty", params, boost::bind(&P44_BridgeImpl::bridgeApiReconnectQueryHandler, this, _1, _2));
}

void P44_BridgeImpl::bridgeApiReconnectQueryHandler(ErrorPtr aError, JsonObjectPtr aJsonMsg)
{
  OLOG(LOG_DEBUG, "bridgeapi query after reconnect: status=%s, answer:\n%s", Error::text(aError), JsonObject::text(aJsonMsg));
  JsonObjectPtr o;
  JsonObjectPtr result;
  if (aJsonMsg && aJsonMsg->get("result", result)) {
    // process device list
    JsonObjectPtr vdcs;
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
            if (device->get("dSUID", o, true)) {
              string dsuid = o->stringValue();
              if (device->get("x-p44-bridgeable", o) && o->boolValue()) {
                // is a bridgeable device, look it up
                DeviceUIDMap::iterator devpos = mDeviceUIDMap.find(dsuid);
                if (devpos!=mDeviceUIDMap.end()) {
                  POLOG(devpos->second, LOG_NOTICE, "Continuing operation after API server reconnect");
                  // we have that device registered, re-enable for bridging
                  JsonObjectPtr params = JsonObject::newObj();
                  params->add("dSUID", JsonObject::newString(dsuid));
                  JsonObjectPtr props = JsonObject::newObj();
                  props->add("x-p44-bridged", JsonObject::newBool(true));
                  params->add("properties", props);
                  api().call("setProperty", params, NoOP);
                }
                else {
                  // we don't know this yet, add separately
                  OLOG(LOG_NOTICE, "New device '%s' encountered after API server reconnect", dsuid.c_str());
                  newDeviceGotBridgeable(dsuid);
                }
              }
            }
          }
        }
      }
    }
    // update status
    updateBridgeStatus(hasBridgeableDevices()); // bridge is running when it has any bridgeable devices now
  }
  // TODO: maybe find and disable those that are no longer visible in the brige API
  OLOG(LOG_WARNING, "Reconnected devices after API server reconnect");
}




void P44_BridgeImpl::bridgeApiNotificationHandler(ErrorPtr aError, JsonObjectPtr aJsonMsg)
{
  if (Error::isOK(aError)) {
    OLOG(LOG_DEBUG, "bridge API message received: %s", JsonObject::text(aJsonMsg));
    // handle push notifications
    JsonObjectPtr o;
    string targetDSUID;
    if (aJsonMsg && aJsonMsg->get("dSUID", o, true)) {
      // request targets a device
      targetDSUID = o->stringValue();
      DeviceUIDMap::iterator devpos = mDeviceUIDMap.find(targetDSUID);
      if (devpos!=mDeviceUIDMap.end()) {
        // device exists, dispatch
        if (aJsonMsg->get("notification", o, true)) {
          string notification = o->stringValue();
          POLOG(devpos->second, LOG_INFO, "Notification '%s' received: %s", notification.c_str(), JsonObject::text(aJsonMsg));
          bool handled = P44_DeviceImpl::impl(devpos->second)->handleBridgeNotification(notification, aJsonMsg);
          if (handled) {
            POLOG(devpos->second, LOG_INFO, "processed notification");
          }
          else {
            POLOG(devpos->second, LOG_ERR, "could not handle notification '%s'", notification.c_str());
          }
        }
        else {
          POLOG(devpos->second, LOG_ERR, "unknown request for device");
        }
      }
      else {
        // unknown DSUID - check if it is a change in bridgeability
        if (aJsonMsg->get("notification", o, true)) {
          if (o->stringValue()=="pushNotification") {
            JsonObjectPtr props;
            if (aJsonMsg->get("changedproperties", props, true)) {
              if (props->get("x-p44-bridgeable", o) && o->boolValue()) {
                // a new device got bridgeable
                newDeviceGotBridgeable(targetDSUID);
              }
              return;
            }
          }
        }
        OLOG(LOG_ERR, "request targeting unknown device %s", targetDSUID.c_str());
      }
    }
    else {
      // global request
      if (aJsonMsg->get("notification", o, true)) {
        string notification = o->stringValue();
        OLOG(LOG_NOTICE, "Global notification '%s' received: %s", notification.c_str(), JsonObject::text(aJsonMsg));
        handleGlobalNotification(notification, aJsonMsg);
      }
      else {
        OLOG(LOG_ERR, "unknown global request: %s", JsonObject::text(aJsonMsg));
      }
    }
  }
  else {
    OLOG(LOG_ERR, "bridge API Error %s", aError->text());
  }
}


void P44_BridgeImpl::handleGlobalNotification(const string notification, JsonObjectPtr aJsonMsg)
{
  JsonObjectPtr o;
  if (notification=="commissioning") {
    if ((o = aJsonMsg->get("enable"))) {
      requestCommissioning(o->boolValue());
    }
  }
  else if (notification=="terminate") {
    int exitcode = EXIT_SUCCESS;
    if ((o = aJsonMsg->get("exitcode"))) {
      // custom exit code
      exitcode = o->int32Value();
    }
    OLOG(LOG_NOTICE, "Terminating application with exitcode=%d", exitcode);
    Application::sharedApplication()->terminateApp(exitcode);
  }
  else if (notification=="loglevel") {
    if ((o = aJsonMsg->get("app"))) {
      int newAppLogLevel = o->int32Value();
      if (newAppLogLevel==8) {
        // trigger statistics
        LOG(LOG_NOTICE, "\n========== requested showing statistics");
        LOG(LOG_NOTICE, "\n%s", MainLoop::currentMainLoop().description().c_str());
        MainLoop::currentMainLoop().statistics_reset();
        LOG(LOG_NOTICE, "========== statistics shown\n");
      }
      else if (newAppLogLevel>=0 && newAppLogLevel<=7) {
        int oldLevel = LOGLEVEL;
        SETLOGLEVEL(newAppLogLevel);
        LOG(newAppLogLevel, "\n\n========== changed log level from %d to %d ===============", oldLevel, newAppLogLevel);
      }
      else {
        LOG(LOG_ERR, "invalid log level %d", newAppLogLevel);
      }
    }
    if ((o = aJsonMsg->get("chip"))) {
      int newChipLogLevel = o->int32Value();
      LOG(LOG_NOTICE, "\n\n========== changing CHIP log level from %d to %d ===============", (int)chip::Logging::GetLogFilter(), newChipLogLevel);
      chip::Logging::SetLogFilter((uint8_t)newChipLogLevel);
    }
    if ((o = aJsonMsg->get("deltas"))) SETDELTATIME(o->boolValue());
    #if ENABLE_LOG_COLORS
    if ((o = aJsonMsg->get("symbols"))) SETLOGSYMBOLS(o->boolValue());
    if ((o = aJsonMsg->get("colors"))) SETLOGCOLORING(o->boolValue());
    #endif // ENABLE_LOG_COLORS
  }
}



void P44_BridgeImpl::newDeviceGotBridgeable(string aNewDeviceDSUID)
{
  JsonObjectPtr params = JsonObject::objFromText(
    "{ \"query\": "
    NEEDED_DEVICE_PROPERTIES
    "}"
  );
  params->add("dSUID", JsonObject::newString(aNewDeviceDSUID));
  api().call("getProperty", params, boost::bind(&P44_BridgeImpl::newDeviceInfoQueryHandler, this, _1, _2));
}


void P44_BridgeImpl::newDeviceInfoQueryHandler(ErrorPtr aError, JsonObjectPtr aJsonMsg)
{
  OLOG(LOG_INFO, "bridgeapi query for additional device: status=%s, answer:\n%s", Error::text(aError), JsonObject::text(aJsonMsg));
  JsonObjectPtr o;
  JsonObjectPtr result;
  if (aJsonMsg && aJsonMsg->get("result", result)) {
    DevicePtr dev = bridgedDeviceFromJSON(result);
    if (dev) {
      bridgeAdditionalDevice(dev);
    }
  }
}

#endif // P44_ADAPTERS
