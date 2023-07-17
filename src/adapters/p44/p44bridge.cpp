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

void P44_BridgeImpl::adapterStartup(AdapterStartedCB aAdapterStartedCB)
{
  mAdapterStartedCB = aAdapterStartedCB;
  api().connectBridgeApi(boost::bind(&P44_BridgeImpl::bridgeApiConnectedHandler, this, _1));
}


void P44_BridgeImpl::setCommissionable(bool aIsCommissionable)
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


void P44_BridgeImpl::cleanup()
{
  api().closeConnection();
  inherited::cleanup();
}


// MARK: P44_BridgeImpl internals

P44_BridgeImpl::P44_BridgeImpl()
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
    OLOG(LOG_WARNING, "(re)connected bridge API, device info might be stale");
    return;
  }
  else {
    // connection established
    queryBridge();
  }
}

// MARK: query and setup bridgeable devices

#define NEEDED_DEVICE_PROPERTIES \
  "{\"dSUID\":null, \"name\":null, \"function\": null, \"x-p44-zonename\": null, " \
  "\"outputDescription\":null, \"outputSettings\": null, " \
  "\"scenes\": { \"0\":null, \"5\":null }, " \
  "\"vendorName\":null, \"model\":null, \"configURL\":null, " \
  "\"channelStates\":null, \"channelDescriptions\":null, " \
  "\"sensorDescriptions\":null, \"sensorStates\":null, " \
  "\"inputDescriptions\":null, \"inputStates\":null, " \
  "\"buttonDescriptions\":null, \"buttonStates\":null, " \
  "\"active\":null, " \
  "\"x-p44-bridgeable\":null, \"x-p44-bridged\":null, \"x-p44-bridgeAs\":null }"

void P44_BridgeImpl::queryBridge()
{
  // first update (reset) bridge status
  api().setProperty("root", "x-p44-bridge.qrcodedata", JsonObject::newString(""));
  api().setProperty("root", "x-p44-bridge.manualpairingcode", JsonObject::newString(""));
  api().setProperty("root", "x-p44-bridge.started", JsonObject::newBool(false));
  api().setProperty("root", "x-p44-bridge.commissionable", JsonObject::newBool(false));
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
            if (behaviourtype=="light" && groups && groups->get("1")) dev = new P44_OnOffLightDevice();
            else dev = new P44_OnOffPluginUnitDevice();
          }
          else if (bridgeAs=="level-control") {
            if (behaviourtype=="light" && groups && groups->get("1")) dev = new P44_DimmableLightDevice();
            else dev = new P44_DimmablePluginUnitDevice();
          }
          if (dev) {
            OLOG(LOG_NOTICE, "found bridgeable device with x-p44-bridgeAs hint '%s': %s", bridgeAs.c_str(), dsuid.c_str());
            P44_DeviceImpl::impl(dev)->initBridgedInfo(aDeviceJSON, outputdesc);
            devices.push_back(dev);
          }
        }
        if (!dev) {
          // no or unknown bridging hint - derive bridged device type(s) automatically
          // First: check output
          if (outputdesc) {
            if (outputdesc->get("function", o)) {
              int outputfunction = (int)o->int32Value();
              // output device
              if (behaviourtype=="light" && groups && groups->get("1")) {
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
              else {
                // not a light, only switched or dimmed
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
              P44_DeviceImpl::impl(dev)->initBridgedInfo(aDeviceJSON, outputdesc);
              devices.push_back(dev);
              dev.reset();
            }
          }
          // Second: check inputs
          enum { sensor, input, button, numInputTypes };
          const char* inputTypeNames[numInputTypes] = { "sensor", "binaryInput", "button" };
          for (int inputType = sensor; inputType<numInputTypes; inputType++) {
            JsonObjectPtr inputdescs;
            if (aDeviceJSON->get((string(inputTypeNames[inputType])+"Descriptions").c_str(), inputdescs)) {
              // iterate through this input type's items
              string inputid;
              JsonObjectPtr inputdesc;
              inputdescs->resetKeyIteration();
              while (inputdescs->nextKeyValue(inputid, inputdesc)) {
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
                    }
                    break;
                  }
                  case input:
                    if (inputdesc->get("inputType", o)) {
                      int binInpType = o->int32Value();
                      // determine input type
                      switch(binInpType) {
                        case binInpType_presence:
                        case binInpType_presenceInDarkness:
                        case binInpType_motion:
                        case binInpType_motionInDarkness:
                          // TODO: map to Occupancy Sensing Cluster
                          // for now: not handled
                          break;
                        default:
                          // all others: create simple ContactSensors
                          dev = new P44_ContactInput();
                          break;
                      }
                    }
                    break;


                  case button: // TODO: maybe handle seperately, multiple button definitions in one device are usually coupled
                  default:
                    break;
                }
                if (dev) {
                  P44_DeviceImpl::impl(dev)->initBridgedInfo(aDeviceJSON, inputdesc, inputTypeNames[inputType], inputid.c_str());
                  devices.push_back(dev);
                  dev.reset();
                }
              } // iterating all inputs of one type
            }
          } // for all input types
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
  OLOG(LOG_INFO, "initial bridgeapi query: status=%s, answer=%s", Error::text(aError), JsonObject::text(aJsonMsg));
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
  // report started (ONCE!)
  AdapterStartedCB cb = mAdapterStartedCB;
  mAdapterStartedCB = NoOP;
  cb(ErrorPtr(), *this);
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
  if (notification=="terminate") {
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
  OLOG(LOG_INFO, "bridgeapi query for additional device: status=%s, answer=%s", Error::text(aError), JsonObject::text(aJsonMsg));
  JsonObjectPtr o;
  JsonObjectPtr result;
  if (aJsonMsg && aJsonMsg->get("result", result)) {
    DevicePtr dev = bridgedDeviceFromJSON(result);
    bridgeAdditionalDevice(dev);
  }
}

#endif // P44_ADAPTERS
