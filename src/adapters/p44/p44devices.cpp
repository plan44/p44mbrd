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

#include "p44devices.h"

#if P44_ADAPTERS

#include "p44bridge.h"

using namespace p44;

// MARK: - P44_DeviceImpl

P44_DeviceImpl::P44_DeviceImpl() :
  mBridgeable(true), // assume bridgeable, otherwise device wouldn't be instantiated
  mActive(false) // not yet active
{
}


// MARK: DeviceInfoDelegate implementation in P44_DeviceImpl base class


const string P44_DeviceImpl::endpointUID() const
{
  if (!const_device().isPartOfComposedDevice()) return mBridgedDSUID; // is the base device for the dSUID
  // device is a subdevice, must add suffix to dSUID to uniquely identify endpoint
  return mBridgedDSUID + "_" + endpointUIDSuffix();
}


void P44_DeviceImpl::deviceDidGetInstalled()
{
  // update bridged info which
  updateBridgedInfo(mTempDeviceInfo);
  // we do not need it any more
  mTempDeviceInfo.reset();
}



bool P44_DeviceImpl::isReachable() const
{
  return mActive && mBridgeable;
}


string P44_DeviceImpl::name() const
{
  return mName;
}


bool P44_DeviceImpl::changeName(const string aNewName)
{
  if (aNewName!=mName) {
    mName = aNewName;
    JsonObjectPtr params = JsonObject::newObj();
    JsonObjectPtr props = JsonObject::newObj();
    props->add("name", JsonObject::newString(mName));
    params->add("properties", props);
    call("setProperty", params, NoOP);
  }
  return true; // new name propagated
}


string P44_DeviceImpl::zone() const
{
  return mZone;
}


// MARK: P44 specific methods

void P44_DeviceImpl::initBridgedInfo(JsonObjectPtr aDeviceInfo, JsonObjectPtr aDeviceComponentInfo, const char* aInputType, const char* aInputId)
{
  // parse common info from bridge
  JsonObjectPtr o = aDeviceInfo->get("dSUID");
  assert(o);
  // Assign infos we need from start and are NOT stored in attributes
  // - dSUID
  mBridgedDSUID = o->stringValue();
  // - default name
  if (aDeviceInfo->get("name", o)) {
    mName = o->stringValue();
    // Note: propagate only after device is installed
  }
  // - default zone name
  if (aDeviceInfo->get("x-p44-zonename", o)) {
    mZone = o->stringValue();
    // Note: propagate only after device is installed
  }
  // - initial reachability
  if (aDeviceInfo->get("active", o)) {
    mActive = o->boolValue();
    // Note: propagate only after device is installed
  }
  // - some of the information will go directly to attributes, which are NOT YET AVAILABLE here
  mTempDeviceInfo = aDeviceInfo;
}


void P44_DeviceImpl::updateBridgedInfo(JsonObjectPtr aDeviceInfo)
{
  JsonObjectPtr o;
  // propagate locally stored info to matter attributes
  device().updateNodeLabel(mName, UpdateMode());
  device().updateReachable(isReachable(), UpdateMode());
  // TODO: implement
  //device().updateZone(mZone, UpdateMode());
  // get some more info, store in Attributes
  if (aDeviceInfo->get("displayId", o)) {
    SET_ATTR_STRING(BridgedDeviceBasicInformation, SerialNumber, device().endpointId(), o->stringValue());
  }
  else {
    // use UID as serial number, MUST NOT BE >32 chars
    SET_ATTR_STRING_M(BridgedDeviceBasicInformation, SerialNumber, device().endpointId(), mBridgedDSUID); // abbreviate in the middle
  }
  if (aDeviceInfo->get("vendorName", o)) {
    SET_ATTR_STRING(BridgedDeviceBasicInformation, VendorName, device().endpointId(), o->stringValue());
  }
  if (aDeviceInfo->get("model", o)) {
    SET_ATTR_STRING(BridgedDeviceBasicInformation, ProductName, device().endpointId(), o->stringValue());
  }
  if (aDeviceInfo->get("configURL", o)) {
    SET_ATTR_STRING(BridgedDeviceBasicInformation, ProductURL, device().endpointId(), o->stringValue());
  }
}


bool P44_DeviceImpl::handleBridgeNotification(const string aNotification, JsonObjectPtr aParams)
{
  if (aNotification=="pushNotification") {
    JsonObjectPtr props;
    if (aParams->get("changedproperties", props, true)) {
      handleBridgePushProperties(props);
      return true;
    }
  }
  else if (aNotification=="vanish") {
    // device got removed, make unreachable
    mBridgeable = false;
    mActive = false;
    device().updateReachable(isReachable(), UpdateMode(UpdateFlags::matter));
    return true;
  }
  return false; // not handled
}


void P44_DeviceImpl::handleBridgePushProperties(JsonObjectPtr aChangedProperties)
{
  JsonObjectPtr o;
  if (aChangedProperties->get("active", o)) {
    mActive = o->boolValue();
    device().updateReachable(isReachable(), UpdateMode(UpdateFlags::matter));
  }
  if (aChangedProperties->get("name", o)) {
    device().updateNodeLabel(o->stringValue(), UpdateMode(UpdateFlags::matter));
  }
  if (aChangedProperties->get("x-p44-bridgeable", o)) {
    // note: non-bridgeable status just makes device unreachable
    mBridgeable = o->boolValue();
    device().updateReachable(isReachable(), UpdateMode(UpdateFlags::matter));
  }
}


void P44_DeviceImpl::notify(const string aNotification, JsonObjectPtr aParams)
{
  if (!aParams) aParams = JsonObject::newObj();
  DLOG(LOG_NOTICE, "mbr -> vdcd: sending notification '%s': %s", aNotification.c_str(), aParams->json_c_str());
  aParams->add("dSUID", JsonObject::newString(mBridgedDSUID));
  P44_BridgeImpl::adapter().api().notify(aNotification, aParams);
}


void P44_DeviceImpl::call(const string aMethod, JsonObjectPtr aParams, JSonMessageCB aResponseCB)
{
  if (!aParams) aParams = JsonObject::newObj();
  DLOG(LOG_NOTICE, "mbr -> vdcd: calling method '%s': %s", aMethod.c_str(), aParams->json_c_str());
  aParams->add("dSUID", JsonObject::newString(mBridgedDSUID));
  P44_BridgeImpl::adapter().api().call(aMethod, aParams, aResponseCB);
}


// MARK: - P44_ComposedDevice

void P44_ComposedImpl::handleBridgePushProperties(JsonObjectPtr aChangedProperties)
{
  for (DevicesList::iterator pos = device().subDevices().begin(); pos!=device().subDevices().end(); ++pos) {
    P44_DeviceImpl::impl(*pos)->handleBridgePushProperties(aChangedProperties);
  }
}


// MARK: - P44_IdentifiableImpl

// MARK: IdentityDelegate implementation

void P44_IdentifiableImpl::identify(int aDurationS)
{
  // (re)start or stop identify in the bridged device
  JsonObjectPtr params = JsonObject::newObj();
  // <0 = stop, >0 = duration (duration==0 would mean default duration, not used here)
  params->add("duration", JsonObject::newDouble(aDurationS<=0 ? -1 : aDurationS));
  notify("identify", params);
}


// MARK: - P44_OnOffImpl

// MARK: OnOffDelegate implementation

void P44_OnOffImpl::setOnOffState(bool aOn)
{
  // call preset1 or off on the bridged device
  JsonObjectPtr params = JsonObject::newObj();
  params->add("channel", JsonObject::newInt32(0)); // default channel
  params->add("value", JsonObject::newDouble(aOn ? 100 : 0));
  params->add("transitionTime", JsonObject::newDouble(0));
  params->add("apply_now", JsonObject::newBool(true));
  notify("setOutputChannelValue", params);
}


void P44_OnOffImpl::initBridgedInfo(JsonObjectPtr aDeviceInfo, JsonObjectPtr aDeviceComponentInfo, const char* aInputType, const char* aInputId)
{
  inherited::initBridgedInfo(aDeviceInfo, aDeviceComponentInfo, aInputType, aInputId);
  // output devices should know which one is the default channel
  JsonObjectPtr o = aDeviceInfo->get("channelDescriptions");
  o->resetKeyIteration();
  string cid;
  JsonObjectPtr co;
  while(o->nextKeyValue(cid, co)) {
    JsonObjectPtr o2;
    if (co->get("dsIndex", o2)) {
      if (o2->int32Value()==0) { mDefaultChannelId = cid; break; }
    }
  }
}


void P44_OnOffImpl::updateBridgedInfo(JsonObjectPtr aDeviceInfo)
{
  // basics first
  inherited::updateBridgedInfo(aDeviceInfo);
  // specifics
  // - output devices should examine the channel states
  JsonObjectPtr o = aDeviceInfo->get("channelStates");
  if (o) {
    parseChannelStates(o, UpdateMode());
  }
}


void P44_OnOffImpl::handleBridgePushProperties(JsonObjectPtr aChangedProperties)
{
  inherited::handleBridgePushProperties(aChangedProperties);
  JsonObjectPtr channelStates;
  if (aChangedProperties->get("channelStates", channelStates, true)) {
    parseChannelStates(channelStates, UpdateMode(UpdateFlags::matter));
  }
}



void P44_OnOffImpl::parseChannelStates(JsonObjectPtr aChannelStates, UpdateMode aUpdateMode)
{
  JsonObjectPtr o;
  if (aChannelStates->get(mDefaultChannelId.c_str(), o)) {
    JsonObjectPtr vo;
    if (o->get("value", vo, true)) {
      deviceP<DeviceOnOff>()->updateOnOff(vo->doubleValue()>0, aUpdateMode);
    }
  }
}


// MARK: - P44_LevelControlImpl

// MARK: LevelControlDelegate implementation

void P44_LevelControlImpl::setLevel(double aNewLevel, uint16_t aTransitionTimeDS)
{
  if (aTransitionTimeDS==0xFFFF) {
    // means using default of the device, so we take the recommended transition time
    aTransitionTimeDS = mRecommendedTransitionTimeDS;
  }
  // adjust default channel
  // Note: transmit relative changes as such, although we already calculate the outcome above,
  //   but non-standard channels might arrive at another value (e.g. wrap around)
  JsonObjectPtr params = JsonObject::newObj();
  params->add("channel", JsonObject::newInt32(0)); // default channel
  params->add("value", JsonObject::newDouble(aNewLevel));
  params->add("transitionTime", JsonObject::newDouble((double)aTransitionTimeDS/10.0));
  params->add("apply_now", JsonObject::newBool(true));
  notify("setOutputChannelValue", params);
  // calculate time when transition will be done
  mEndOfLatestTransition = MainLoop::now()+aTransitionTimeDS*(Second/10);
}


void P44_LevelControlImpl::dim(int8_t aDirection, uint8_t aRate)
{
  JsonObjectPtr params = JsonObject::newObj();
  params->add("channel", JsonObject::newInt32(0)); // default channel
  params->add("mode", JsonObject::newInt32(aDirection));
  params->add("autostop", JsonObject::newBool(false));
  // matter rate is 0..0xFE units per second, p44 rate is 0..100 units per millisecond
  if (aDirection!=0 && aRate!=0xFF) params->add("dimPerMS", JsonObject::newDouble((double)aRate*100/EMBER_AF_PLUGIN_LEVEL_CONTROL_MAXIMUM_LEVEL/1000));
  notify("dimChannel", params);
}



// MARK: P44 internal implementation

P44_LevelControlImpl::P44_LevelControlImpl() :
  mRecommendedTransitionTimeDS(5), // FIXME: for now: just dS default of full range in 7 seconds
  mEndOfLatestTransition(Never)
{
}


void P44_LevelControlImpl::updateBridgedInfo(JsonObjectPtr aDeviceInfo)
{
  // basics first
  inherited::updateBridgedInfo(aDeviceInfo);
  // specifics
  JsonObjectPtr o;
  JsonObjectPtr o2;
  // - level control devices should know the recommended transition time for the output
  if (aDeviceInfo->get("outputDescription", o)) {
    if (o->get("x-p44-recommendedTransitionTime", o2)) {
      // adjust current
      mRecommendedTransitionTimeDS = static_cast<uint16_t>(o2->doubleValue()*10);
    }
  }
  // - get default on level (level of preset1 scene)
  if (aDeviceInfo->get("scenes", o)) {
    if (o->get("5", o2)) {
      if (o2->get("channels", o2)) {
        if (o2->get(mDefaultChannelId.c_str(), o2)) {
          if (o2->get("value", o2)) {
            deviceP<DeviceLevelControl>()->setDefaultOnLevel(o2->doubleValue());
          }
        }
      }
    }
  }
}


void P44_LevelControlImpl::parseChannelStates(JsonObjectPtr aChannelStates, UpdateMode aUpdateMode)
{
  // OnOff just sets on/off state when brightness>0
  inherited::parseChannelStates(aChannelStates, aUpdateMode);
  // init level
  JsonObjectPtr o;
  if (aChannelStates->get(mDefaultChannelId.c_str(), o)) {
    JsonObjectPtr vo;
    if (o->get("value", vo, true)) {
      // bridge side is always 0..100%, mapped to minLevel()..maxLevel()
      // Note: updating on/off attribute is handled separately (in deviceonoff), don't do it here
      deviceP<DeviceLevelControl>()->updateLevel(vo->doubleValue(), aUpdateMode);
    }
  }
}



// MARK: - P44_ColorControlImpl

// MARK: ColorControlDelegate implementation

void P44_ColorControlImpl::setHue(uint8_t aHue, uint16_t aTransitionTimeDS, bool aApply)
{
  JsonObjectPtr params = JsonObject::newObj();
  params->add("channelId", JsonObject::newString("hue"));
  params->add("value", JsonObject::newDouble((double)aHue*360/0xFE));
  params->add("transitionTime", JsonObject::newDouble((double)aTransitionTimeDS/10));
  params->add("apply_now", JsonObject::newBool(aApply));
  notify("setOutputChannelValue", params);

}


void P44_ColorControlImpl::setSaturation(uint8_t aSaturation, uint16_t aTransitionTimeDS, bool aApply)
{
  JsonObjectPtr params = JsonObject::newObj();
  params->add("channelId", JsonObject::newString("saturation"));
  params->add("value", JsonObject::newDouble((double)aSaturation*100/0xFE));
  params->add("transitionTime", JsonObject::newDouble((double)aTransitionTimeDS/10));
  params->add("apply_now", JsonObject::newBool(aApply));
  notify("setOutputChannelValue", params);

}


void P44_ColorControlImpl::setCieX(uint16_t aX, uint16_t aTransitionTimeDS, bool aApply)
{
  JsonObjectPtr params = JsonObject::newObj();
  params->add("channelId", JsonObject::newString("x"));
  params->add("value", JsonObject::newDouble((double)aX/0xFFFE));
  params->add("transitionTime", JsonObject::newDouble((double)aTransitionTimeDS/10));
  params->add("apply_now", JsonObject::newBool(aApply));
  notify("setOutputChannelValue", params);
}


void P44_ColorControlImpl::setCieY(uint16_t aY, uint16_t aTransitionTimeDS, bool aApply)
{
  JsonObjectPtr params = JsonObject::newObj();
  params->add("channelId", JsonObject::newString("y"));
  params->add("value", JsonObject::newDouble((double)aY/0xFFFE));
  params->add("transitionTime", JsonObject::newDouble((double)aTransitionTimeDS/10));
  params->add("apply_now", JsonObject::newBool(aApply));
  notify("setOutputChannelValue", params);
}


void P44_ColorControlImpl::setColortemp(uint16_t aColortemp, uint16_t aTransitionTimeDS, bool aApply)
{
  JsonObjectPtr params = JsonObject::newObj();
  params->add("channelId", JsonObject::newString("colortemp"));
  params->add("value", JsonObject::newDouble(aColortemp)); // is in mireds
  params->add("transitionTime", JsonObject::newDouble((double)aTransitionTimeDS/10));
  params->add("apply_now", JsonObject::newBool(aApply));
  notify("setOutputChannelValue", params);
}




// MARK: P44 internal implementation

P44_ColorControlImpl::P44_ColorControlImpl()
{
}


void P44_ColorControlImpl::initBridgedInfo(JsonObjectPtr aDeviceInfo, JsonObjectPtr aDeviceComponentInfo, const char* aInputType, const char* aInputId)
{
  inherited::initBridgedInfo(aDeviceInfo, aDeviceComponentInfo, aInputType, aInputId);
  // no extra info at this level so far -> NOP
}


void P44_ColorControlImpl::parseChannelStates(JsonObjectPtr aChannelStates, UpdateMode aUpdateMode)
{
  inherited::parseChannelStates(aChannelStates, aUpdateMode);
  JsonObjectPtr o;
  JsonObjectPtr vo;
  bool relevant;
  // need to determine color mode
  DeviceColorControl::ColorMode colorMode = DeviceColorControl::colormode_unknown;
  if (aChannelStates->get("colortemp", o)) {
    if ((relevant = o->get("age", vo, true))) colorMode = DeviceColorControl::colormode_ct; // age is non-null -> component detemines colormode
    if (o->get("value", vo, true)) {
      // scaling: ct is directly in mired
      deviceP<DeviceColorControl>()->updateCurrentColortemp(static_cast<uint16_t>(vo->doubleValue()), relevant && colorMode==DeviceColorControl::colormode_ct ? aUpdateMode : UpdateMode(UpdateFlags::noderive), 0);
    }
  }
  if (!deviceP<DeviceColorControl>()->ctOnly()) {
    if (aChannelStates->get("hue", o)) {
      if ((relevant = o->get("age", vo, true))) colorMode = DeviceColorControl::colormode_hs; // age is non-null -> component detemines colormode
      if (o->get("value", vo, true)) {
        // update only cache if not actually in hs mode
        // scaling: hue is 0..360 degrees mapped to 0..0xFE
        deviceP<DeviceColorControl>()->updateCurrentHue(static_cast<uint8_t>(vo->doubleValue()/360*0xFE), relevant && colorMode==DeviceColorControl::colormode_hs ? aUpdateMode : UpdateMode(UpdateFlags::noderive), 0);
      }
    }
    if (aChannelStates->get("saturation", o)) {
      if ((relevant = o->get("age", vo, true))) colorMode = DeviceColorControl::colormode_hs; // age is non-null -> component detemines colormode
      if (o->get("value", vo, true)) {
        // update only cache if not actually in hs mode
        // scaling: saturation is 0..100% mapped to 0..0xFE
        deviceP<DeviceColorControl>()->updateCurrentSaturation(static_cast<uint8_t>(vo->doubleValue()/100*0xFE), relevant && colorMode==DeviceColorControl::colormode_hs ? aUpdateMode : UpdateMode(UpdateFlags::noderive), 0);
      }
    }
    if (aChannelStates->get("x", o)) {
      if ((relevant = o->get("age", vo, true))) colorMode = DeviceColorControl::colormode_xy; // age is non-null -> component detemines colormode
      if (o->get("value", vo, true)) {
        // update only cache if not actually in hs mode
        // scaling: X is 0..1 mapped to 0..0x10000, with effective range 0..0xFEFF (0..0.9961)
        deviceP<DeviceColorControl>()->updateCurrentX(static_cast<uint16_t>(vo->doubleValue()*0xFFFF), relevant && colorMode==DeviceColorControl::colormode_xy ? aUpdateMode : UpdateMode(UpdateFlags::noderive), 0);
      }
    }
    if (aChannelStates->get("y", o)) {
      if ((relevant = o->get("age", vo, true))) colorMode = DeviceColorControl::colormode_xy; // age is non-null -> component detemines colormode
      if (o->get("value", vo, true)) {
        // update only cache if not actually in hs mode
        // scaling: Y is 0..1 mapped to 0..0x10000, with effective range 0..0xFEFF (0..0.9961)
        deviceP<DeviceColorControl>()->updateCurrentY(static_cast<uint16_t>(vo->doubleValue()*0xFFFF), relevant && colorMode==DeviceColorControl::colormode_xy ? aUpdateMode : UpdateMode(UpdateFlags::noderive), 0);
      }
    }
  }
  // now actually update resulting color mode
  deviceP<DeviceColorControl>()->updateCurrentColorMode(colorMode, aUpdateMode, 0);
}


// MARK: - P44_InputDevice


const string P44_InputImpl::endpointUIDSuffix() const
{
  return mInputType + "_" + mInputId;
}


void P44_InputImpl::initBridgedInfo(JsonObjectPtr aDeviceInfo, JsonObjectPtr aDeviceComponentInfo, const char* aInputType, const char* aInputId)
{
  mInputType = aInputType;
  mInputId = aInputId;
  inherited::initBridgedInfo(aDeviceInfo, aDeviceComponentInfo, aInputType, aInputId);
}


// MARK: - P44_SensorImpl

void P44_SensorImpl::initBridgedInfo(JsonObjectPtr aDeviceInfo, JsonObjectPtr aDeviceComponentInfo, const char* aInputType, const char* aInputId)
{
  inherited::initBridgedInfo(aDeviceInfo, aDeviceComponentInfo, aInputType, aInputId);
  JsonObjectPtr o;
  double min = 0;
  double max = 0;
  double tolerance = 0; // unknown
  bool hasMin = false;
  bool hasMax = false;

  SensorDevice* dev = deviceP<SensorDevice>();
  if (aDeviceComponentInfo->get("resolution", o)) {
    // tolerance is half of the resolution (when resolution is 1, true value might be max +/- 0.5 resolution away)
    tolerance = o->doubleValue()/2;
  }
  if (aDeviceComponentInfo->get("min", o)) {
    hasMin = true;
    min = o->doubleValue();
  }
  if (aDeviceComponentInfo->get("max", o)) {
    hasMax = true;
    max = o->doubleValue();
  }
  dev->setupSensorParams(hasMin, min, hasMax, max, tolerance);
  // also get current value from xxxStates
  parseSensorValue(aDeviceInfo, UpdateMode());
}


void P44_SensorImpl::handleBridgePushProperties(JsonObjectPtr aChangedProperties)
{
  inherited::handleBridgePushProperties(aChangedProperties);
  // parse sensor value
  parseSensorValue(aChangedProperties, UpdateMode(UpdateFlags::matter));
}


void P44_SensorImpl::parseSensorValue(JsonObjectPtr aProperties, UpdateMode aUpdateMode)
{
  JsonObjectPtr states;
  if (aProperties->get((mInputType+"States").c_str(), states)) {
    JsonObjectPtr state;
    if (states->get(mInputId.c_str(), state)) {
      JsonObjectPtr o;
      SensorDevice* dev = deviceP<SensorDevice>();
      if (state->get("value", o, true)) {
        // non-NULL value
        dev->updateMeasuredValue(o->doubleValue(), true, aUpdateMode);
      }
      else {
        // NULL value or no value contained in state at all
        dev->updateMeasuredValue(0, false, aUpdateMode);
      }
    }
  }
}


// MARK: - P44_BinaryInputImpl


void P44_BinaryInputImpl::initBridgedInfo(JsonObjectPtr aDeviceInfo, JsonObjectPtr aDeviceComponentInfo, const char* aInputType, const char* aInputId)
{
  inherited::initBridgedInfo(aDeviceInfo, aDeviceComponentInfo, aInputType, aInputId);
  // get current value from xxxStates
  parseInputValue(aDeviceInfo, UpdateMode());
}


void P44_BinaryInputImpl::handleBridgePushProperties(JsonObjectPtr aChangedProperties)
{
  inherited::handleBridgePushProperties(aChangedProperties);
  parseInputValue(aChangedProperties, UpdateMode(UpdateFlags::matter));
}


void P44_BinaryInputImpl::parseInputValue(JsonObjectPtr aProperties, UpdateMode aUpdateMode)
{
  JsonObjectPtr states;
  if (aProperties->get((mInputType+"States").c_str(), states)) {
    JsonObjectPtr state;
    if (states->get(mInputId.c_str(), state)) {
      JsonObjectPtr o;
      BooleanInputDevice* dev = deviceP<BooleanInputDevice>();
      if (state->get("value", o, true)) {
        // non-NULL value
        dev->updateCurrentState(o->boolValue(), true, aUpdateMode);
      }
      else {
        // NULL value or no value contained in state at all
        dev->updateCurrentState(false, false, aUpdateMode);
      }
    }
  }
}


#endif // P44_ADAPTERS
