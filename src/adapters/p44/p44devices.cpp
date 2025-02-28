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
using namespace chip;
using namespace app;
using namespace Clusters;


// MARK: - P44_DeviceImpl

P44_DeviceImpl::P44_DeviceImpl() :
  mBridgeable(true), // assume bridgeable, otherwise device wouldn't be instantiated
  mActive(false), // not yet active
  mZoneId(zoneId_global) // no zoneID known yet
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
  // update configuration that could not be set before device was installed (especially: attributes) from bridged info
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


/* TODO: remove later
string P44_DeviceImpl::zoneName() const
{
  P44_BridgeImpl::ZoneMap::iterator z = P44_BridgeImpl::adapter().mZoneMap.find(mZoneId);
  if (z==P44_BridgeImpl::adapter().mZoneMap.end()) return "<unknown>";
  return z->second;
}
 */


// MARK: P44 specific methods

void P44_DeviceImpl::initBridgedInfo(JsonObjectPtr aDeviceInfo, const char* aInputType, const char* aInputId)
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
  if (!device().isPartOfComposedDevice()) {
    // only main devices have BridgedDeviceBasicInformation
    device().updateNodeLabel(mName, UpdateMode());
    device().updateReachable(isReachable(), UpdateMode());
    updateZoneInfo(aDeviceInfo, UpdateMode());
    // get some more info, store in Attributes
    if (aDeviceInfo->get("displayId", o)) {
      SET_ATTR_STRING(BridgedDeviceBasicInformation, SerialNumber, endpointId(), o->stringValue());
    }
    else {
      // use UID as serial number, MUST NOT BE >32 chars
      SET_ATTR_STRING_M(BridgedDeviceBasicInformation, SerialNumber, endpointId(), mBridgedDSUID); // abbreviate in the middle
    }
    if (aDeviceInfo->get("vendorName", o)) {
      SET_ATTR_STRING(BridgedDeviceBasicInformation, VendorName, endpointId(), o->stringValue());
    }
    if (aDeviceInfo->get("model", o)) {
      SET_ATTR_STRING(BridgedDeviceBasicInformation, ProductName, endpointId(), o->stringValue());
    }
    if (aDeviceInfo->get("configURL", o)) {
      SET_ATTR_STRING(BridgedDeviceBasicInformation, ProductURL, endpointId(), o->stringValue());
    }
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
    // device got removed
    mBridgeable = false;
    mActive = false;
    P44_BridgeImpl::adapter().removeDevice(&device());
    return true;
  }
  return false; // not handled
}


void P44_DeviceImpl::updateZoneInfo(JsonObjectPtr aDeviceInfo, UpdateMode aUpdateMode)
{
  JsonObjectPtr o;
  if (aDeviceInfo->get("zoneID", o)) {
    mZoneId = static_cast<DsZoneID>(o->int32Value());
    if (mZoneId!=zoneId_global) {
      // - assign or update zonename
      string zonename;
      bool explicitName = false;
      if (aDeviceInfo->get("x-p44-zonename", o)) {
        zonename = o->stringValue();
        explicitName = true;
      }
      else {
        zonename = string_format("Zone_%d", mZoneId);
      }
      P44_BridgeImpl::adapter().addOrUpdateZone(mZoneId, zonename, explicitName, aUpdateMode);
    }
  }
}



void P44_DeviceImpl::handleBridgePushProperties(JsonObjectPtr aChangedProperties)
{
  JsonObjectPtr o;
  if (!device().isPartOfComposedDevice()) {
    // zone change
    updateZoneInfo(aChangedProperties, UpdateMode(UpdateFlags::matter));
    // only main devices have BridgedDeviceBasicInformation
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
  // Actually have the device identifying itself when it is supported
  if (mCanIdentifyToUser) {
    // (re)start or stop identify in the bridged device
    JsonObjectPtr params = JsonObject::newObj();
    // <0 = stop, >0 = duration (duration==0 would mean default duration, not used here)
    params->add("duration", JsonObject::newDouble(aDurationS<=0 ? -1 : aDurationS));
    notify("identify", params);
  }
  else {
    // Note: as most matter device types require identification, but many bridged devices don't have it,
    //   just have controller identify itself instead.
    P44_BridgeImpl::adapter().identifyBridge(aDurationS);
  }
}


void P44_IdentifiableImpl::updateBridgedInfo(JsonObjectPtr aDeviceInfo)
{
  // basics first
  inherited::updateBridgedInfo(aDeviceInfo);
  // specifics
  mCanIdentifyToUser = P44_BridgeImpl::hasModelFeature(aDeviceInfo, "identification");
}



// MARK: - P44_OutputImpl

// MARK: P44 internal implementation

void P44_OutputImpl::initBridgedInfo(JsonObjectPtr aDeviceInfo, const char* aInputType, const char* aInputId)
{
  inherited::initBridgedInfo(aDeviceInfo, aInputType, aInputId);
  // output devices should know which one is the default channel
  JsonObjectPtr o = aDeviceInfo->get("channelDescriptions");
  o->resetKeyIteration();
  string cid;
  JsonObjectPtr co;
  while(o->nextKeyValue(cid, co)) {
    JsonObjectPtr o2;
    if (co->get("dsIndex", o2)) {
      if (o2->int32Value()==0) {
        mDefaultChannelId = cid;
        if (co->get("min", o2)) mDefaultChannelMin = o2->doubleValue();
        if (co->get("max", o2)) mDefaultChannelMax = o2->doubleValue();
        break;
      }
    }
  }
}


void P44_OutputImpl::updateBridgedInfo(JsonObjectPtr aDeviceInfo)
{
  // basics first
  inherited::updateBridgedInfo(aDeviceInfo);
  // specifics
  JsonObjectPtr outputState = aDeviceInfo->get("outputState");
  JsonObjectPtr channelStates = aDeviceInfo->get("channelStates");
  if (outputState || channelStates) {
    parseOutputState(outputState, channelStates, UpdateMode());
  }
}


void P44_OutputImpl::handleBridgePushProperties(JsonObjectPtr aChangedProperties)
{
  // basics first
  inherited::handleBridgePushProperties(aChangedProperties);
  // specifics
  JsonObjectPtr outputState = aChangedProperties->get("outputState");
  JsonObjectPtr channelStates = aChangedProperties->get("channelStates");
  if (outputState || channelStates) {
    parseOutputState(outputState, channelStates, UpdateMode(UpdateFlags::matter));
  }
}


double P44_OutputImpl::value2percent(double aValue)
{
  return (aValue-mDefaultChannelMin)*100/(mDefaultChannelMax-mDefaultChannelMin);
}


double P44_OutputImpl::percent2value(double aPercent)
{
  return mDefaultChannelMin+(aPercent/100*(mDefaultChannelMax-mDefaultChannelMin));
}


// MARK: - P44_OnOffImpl

// MARK: OnOffDelegate implementation

void P44_OnOffImpl::setOnOffState(bool aOn)
{
  // call preset1 or off on the bridged device
  JsonObjectPtr params = JsonObject::newObj();
  params->add("channel", JsonObject::newInt32(0)); // default channel
  params->add("value", JsonObject::newDouble(aOn ? mDefaultChannelMax : mDefaultChannelMin));
  params->add("transitionTime", JsonObject::newDouble(0));
  params->add("apply_now", JsonObject::newBool(true));
  notify("setOutputChannelValue", params);
}

// MARK: P44 internal implementation

void P44_OnOffImpl::parseOutputState(JsonObjectPtr aOutputState, JsonObjectPtr aChannelStates, UpdateMode aUpdateMode)
{
  auto dev = deviceOrNullP<DeviceOnOff>();
  if (dev) {
    JsonObjectPtr o;
    // we actually have OnOff (level-control-like devices not actually using LevelControl might not have it)
    if (aChannelStates && aChannelStates->get(mDefaultChannelId.c_str(), o)) {
      JsonObjectPtr vo;
      if (o->get("value", vo, true)) {
        dev->updateOnOff(vo->doubleValue()>mDefaultChannelMin, aUpdateMode);
      }
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
  params->add("value", JsonObject::newDouble(percent2value(aNewLevel)));
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
  // matter rate is 0..0xFE units per second, p44 rate is 0..mDefaultChannelMax units per millisecond
  if (aDirection!=0 && aRate!=0xFF) params->add("dimPerMS", JsonObject::newDouble((double)aRate*mDefaultChannelMax/MATTER_DM_PLUGIN_LEVEL_CONTROL_MAXIMUM_LEVEL/1000));
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
            deviceP<LevelControlImplementationInterface>()->setDefaultOnLevel(value2percent(o2->doubleValue()));
          }
        }
      }
    }
  }
}


void P44_LevelControlImpl::parseOutputState(JsonObjectPtr aOutputState, JsonObjectPtr aChannelStates, UpdateMode aUpdateMode)
{
  // OnOff just sets on/off state when level>0
  // Note: only device actually based on levelcontrol will do anything here
  inherited::parseOutputState(aOutputState, aChannelStates, aUpdateMode);
  // init level
  JsonObjectPtr o;
  if (aChannelStates && aChannelStates->get(mDefaultChannelId.c_str(), o)) {
    JsonObjectPtr vo;
    if (o->get("value", vo, true)) {
      // bridge side is mDefaultChannelMin..mDefaultChannelMax, mapped to levelcontrol minLevel()..maxLevel()
      // Note: updating on/off attribute is handled automatically when needed (and OnOff is present at all)
      deviceP<LevelControlImplementationInterface>()->updateLevel(value2percent(vo->doubleValue()), aUpdateMode);
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


void P44_ColorControlImpl::initBridgedInfo(JsonObjectPtr aDeviceInfo, const char* aInputType, const char* aInputId)
{
  inherited::initBridgedInfo(aDeviceInfo, aInputType, aInputId);
  // no extra info at this level so far -> NOP
}


void P44_ColorControlImpl::parseOutputState(JsonObjectPtr aOutputState, JsonObjectPtr aChannelStates, UpdateMode aUpdateMode)
{
  inherited::parseOutputState(aOutputState, aChannelStates, aUpdateMode);
  if (!aChannelStates) return; // no channel info at all
  JsonObjectPtr o;
  JsonObjectPtr vo;
  bool relevant;
  using InternalColorMode = DeviceColorControl::InternalColorMode;
  // need to determine color mode
  DeviceColorControl::InternalColorMode colorMode = InternalColorMode::unknown_mode;
  if (aChannelStates->get("colortemp", o)) {
    if ((relevant = o->get("age", vo, true))) colorMode = InternalColorMode::ct; // age is non-null -> component detemines colormode
    if (o->get("value", vo, true)) {
      // scaling: ct is directly in mired
      deviceP<DeviceColorControl>()->updateCurrentColortemp(static_cast<uint16_t>(vo->doubleValue()), relevant && colorMode==InternalColorMode::ct ? aUpdateMode : UpdateMode(UpdateFlags::noderive), 0);
    }
  }
  if (!deviceP<DeviceColorControl>()->ctOnly()) {
    if (aChannelStates->get("hue", o)) {
      if ((relevant = o->get("age", vo, true))) colorMode = InternalColorMode::hs; // age is non-null -> component detemines colormode
      if (o->get("value", vo, true)) {
        // update only cache if not actually in hs mode
        // scaling: hue is 0..360 degrees mapped to 0..0xFE
        deviceP<DeviceColorControl>()->updateCurrentHue(static_cast<uint8_t>(vo->doubleValue()/360*0xFE), relevant && colorMode==InternalColorMode::hs ? aUpdateMode : UpdateMode(UpdateFlags::noderive), 0);
      }
    }
    if (aChannelStates->get("saturation", o)) {
      if ((relevant = o->get("age", vo, true))) colorMode = InternalColorMode::hs; // age is non-null -> component detemines colormode
      if (o->get("value", vo, true)) {
        // update only cache if not actually in hs mode
        // scaling: saturation is 0..100% mapped to 0..0xFE
        deviceP<DeviceColorControl>()->updateCurrentSaturation(static_cast<uint8_t>(vo->doubleValue()/100*0xFE), relevant && colorMode==InternalColorMode::hs ? aUpdateMode : UpdateMode(UpdateFlags::noderive), 0);
      }
    }
    if (aChannelStates->get("x", o)) {
      if ((relevant = o->get("age", vo, true))) colorMode = InternalColorMode::xy; // age is non-null -> component detemines colormode
      if (o->get("value", vo, true)) {
        // update only cache if not actually in hs mode
        // scaling: X is 0..1 mapped to 0..0x10000, with effective range 0..0xFEFF (0..0.9961)
        deviceP<DeviceColorControl>()->updateCurrentX(static_cast<uint16_t>(vo->doubleValue()*0xFFFF), relevant && colorMode==InternalColorMode::xy ? aUpdateMode : UpdateMode(UpdateFlags::noderive), 0);
      }
    }
    if (aChannelStates->get("y", o)) {
      if ((relevant = o->get("age", vo, true))) colorMode = InternalColorMode::xy; // age is non-null -> component detemines colormode
      if (o->get("value", vo, true)) {
        // update only cache if not actually in hs mode
        // scaling: Y is 0..1 mapped to 0..0x10000, with effective range 0..0xFEFF (0..0.9961)
        deviceP<DeviceColorControl>()->updateCurrentY(static_cast<uint16_t>(vo->doubleValue()*0xFFFF), relevant && colorMode==InternalColorMode::xy ? aUpdateMode : UpdateMode(UpdateFlags::noderive), 0);
      }
    }
  }
  // now actually update resulting color mode
  deviceP<DeviceColorControl>()->updateCurrentColorMode(colorMode, aUpdateMode, 0);
}


// MARK: - P44_WindowCoveringImpl

// MARK: WindowCoveringDelegate implementation

// TODO: clarify
// Note: unclear who sets the WindowCovering::Mode::kMotorDirectionReversed bit, Apple does not and
//   sends GoToLiftPercentage(100%) to close the blind, so we assume that's the non-reversed meaning
//   (would semantically fit "WindowCovering": 100% = window fully covered).
//   As dS has it the other way,  bridge2matter and matter2bridge invert the range IN NORMAL MODE
//   (and don't when reversed is set)


double P44_WindowCoveringImpl::matter2bridge(const Percent100ths aPercent100th, bool aMotorDirectionReversed, bool aDefaultChannel)
{
  double p = (double)aPercent100th/100;
  if (!aMotorDirectionReversed) p = 100-p; // reversed is dS standard (100% = fully lifted/open) so !aMotorDirectionReversed
  return aDefaultChannel ? percent2value(p) : p;
}


bool P44_WindowCoveringImpl::matter2bridge(const DataModel::Nullable<Percent100ths>& aPercent100th, JsonObjectPtr &aBridgeValue, bool aMotorDirectionReversed, bool aDefaultChannel)
{
  if (aPercent100th.IsNull()) {
    aBridgeValue = JsonObject::newNull();
    return false;
  }
  aBridgeValue = JsonObject::newDouble(matter2bridge(aPercent100th.Value(), aMotorDirectionReversed, aDefaultChannel));
  return true;
}


Percent100ths P44_WindowCoveringImpl::bridge2matter(double aBridgeValue, bool aMotorDirectionReversed, bool aDefaultChannel)
{
  double p = aDefaultChannel ? value2percent(aBridgeValue) : aBridgeValue;
  if (!aMotorDirectionReversed) p = 100 - p; // reversed is dS standard (100% = fully lifted/open) so !aMotorDirectionReversed
  return static_cast<Percent100ths>(p*100);
}


bool P44_WindowCoveringImpl::bridge2matter(JsonObjectPtr aBridgeValue, DataModel::Nullable<Percent100ths>& aPercent100th, bool aMotorDirectionReversed, bool aDefaultChannel)
{
  if (aBridgeValue && !aBridgeValue->isType(json_type_null)) {
    aPercent100th.SetNonNull(bridge2matter(aBridgeValue->doubleValue(), aMotorDirectionReversed, aDefaultChannel));
    return true;
  }
  aPercent100th.SetNull();
  return false;
}


void P44_WindowCoveringImpl::startMovement(WindowCovering::WindowCoveringType aMovementType)
{
  JsonObjectPtr params, val;
  BitMask<WindowCovering::Mode> mode;
  WindowCovering::Attributes::Mode::Get(endpointId(), &mode);
  // set output values
  DataModel::Nullable<Percent100ths> lift;
  WindowCovering::Attributes::TargetPositionLiftPercent100ths::Get(endpointId(), lift);
  if (mHasTilt) {
    DataModel::Nullable<Percent100ths> tilt;
    WindowCovering::Attributes::TargetPositionTiltPercent100ths::Get(endpointId(), tilt);
    if (matter2bridge(tilt, val, mode.Has(WindowCovering::Mode::kMotorDirectionReversed), false)) {
      params = JsonObject::newObj();
      params->add("channelId", JsonObject::newString("shadeOpeningAngleOutside"));
      params->add("value", val);
      params->add("apply_now", JsonObject::newBool(lift.IsNull())); // wait for lift value, unless it is not provided
      notify("setOutputChannelValue", params);
    }
  }
  if (matter2bridge(lift, val, mode.Has(WindowCovering::Mode::kMotorDirectionReversed), true)) {
    params = JsonObject::newObj();
    params->add("channelId", JsonObject::newString(mDefaultChannelId));
    params->add("value", val);
    params->add("apply_now", JsonObject::newBool(true)); // Apply now, together with tilt
    notify("setOutputChannelValue", params);
  }
}


void P44_WindowCoveringImpl::simpleStartMovement(WindowCovering::WindowCoveringType aMovementType, bool aUpOrOpen)
{
  BitMask<WindowCovering::Mode> mode;
  WindowCovering::Attributes::Mode::Get(endpointId(), &mode);
  JsonObjectPtr params = JsonObject::newObj();
  bool isLift = aMovementType==WindowCovering::WindowCoveringType::Lift;
  params->add("channelId", JsonObject::newString(isLift ? mDefaultChannelId : "shadeOpeningAngleOutside"));
  double v = matter2bridge(aUpOrOpen ? 0 : 100*100, mode.Has(WindowCovering::Mode::kMotorDirectionReversed), isLift);
  params->add("value", JsonObject::newDouble(v)); // dS standard: 100% = fully lifted/open
  params->add("apply_now", JsonObject::newBool(true)); // Apply now, together with tilt
  notify("setOutputChannelValue", params);
}


void P44_WindowCoveringImpl::stopMovement()
{
  // call stop scene
  JsonObjectPtr params = JsonObject::newObj();
  params->add("scene", JsonObject::newInt32(15)); // S_STOP
  params->add("force", JsonObject::newBool(true));
  notify("callScene", params);
}



// MARK: P44 internal implementation

P44_WindowCoveringImpl::P44_WindowCoveringImpl() :
  mHasTilt(false)
{
}


void P44_WindowCoveringImpl::updateBridgedInfo(JsonObjectPtr aDeviceInfo)
{
  // basics first
  inherited::updateBridgedInfo(aDeviceInfo);
  // init attributes
  // - rollershade or tiltblind?
  JsonObjectPtr o, o2;
  mHasTilt = P44_BridgeImpl::hasModelFeature(aDeviceInfo, "shadebladeang");
  underlying_type_t<WindowCovering::Feature> featuremap = 0;
  featuremap |= to_underlying(WindowCovering::Feature::kLift) | to_underlying(WindowCovering::Feature::kPositionAwareLift);
  if (mHasTilt) {
    featuremap |= to_underlying(WindowCovering::Feature::kTilt) | to_underlying(WindowCovering::Feature::kPositionAwareTilt);
  }
  WindowCovering::Attributes::FeatureMap::Set(endpointId(), featuremap);
  WindowCovering::ConfigStatusUpdateFeatures(endpointId()); // update dependent attributes
  WindowCovering::TypeSet(endpointId(), mHasTilt ? WindowCovering::Type::kTiltBlindLiftAndTilt : WindowCovering::Type::kRollerShade);
  // - end product type
  WindowCovering::EndProductType endproducttype = WindowCovering::EndProductType::kRollerShade;
  if (mHasTilt) endproducttype = WindowCovering::EndProductType::kExteriorVenetianBlind; // TODO: maybe differentiate later
  WindowCovering::EndProductTypeSet(endpointId(), endproducttype);
}


void P44_WindowCoveringImpl::parseOutputState(JsonObjectPtr aOutputState, JsonObjectPtr aChannelStates, UpdateMode aUpdateMode)
{
  inherited::parseOutputState(aOutputState, aChannelStates, aUpdateMode);
  JsonObjectPtr o, vo;
  BitMask<WindowCovering::Mode> mode = WindowCovering::ModeGet(endpointId());
  int moving = 0;
  if (aOutputState) {
    // get moving state
    if (aOutputState->get("movingState", o)) {
      moving = o->int32Value();
    }
    // check for errors
    if (aOutputState->get("error", o)) {
      int status = 0;
      int err = o->int32Value();
      switch (err) {
        case hardwareError_openCircuit:
        case hardwareError_shortCircuit:
        case hardwareError_deviceError:
          // PCB, fuse and other electrics problems.
          status |= to_underlying(WindowCovering::SafetyStatus::kHardwareFailure);
          break;
        case hardwareError_overload:
          // An obstacle is preventing actuator movement.
          status |= to_underlying(WindowCovering::SafetyStatus::kObstacleDetected);
          break;
        case hardwareError_busConnection:
          // Communication failure to sensors or other safety equip­ment.
          status |= to_underlying(WindowCovering::SafetyStatus::kFailedCommunication);
          break;
        case hardwareError_lowBattery:
          // power might not be fully available at the moment.
          status |= to_underlying(WindowCovering::SafetyStatus::kPower);
          break;
      }
      // FIXME: it seems SafetyStatusSet() should be used, however it is not implemented (due to declaration/impl mismatch in the SDK...)
      // (but as the intended implementation is just setting the attribute, we leave it at that)
      //WindowCovering::SafetyStatusSet(endpointId(), static_cast<underlying_type_t<WindowCovering::SafetyStatus>>(status));
      WindowCovering::Attributes::SafetyStatus::Set(endpointId(), static_cast<underlying_type_t<WindowCovering::SafetyStatus>>(status));
    }
  }
  // - current positions
  if (aChannelStates->get(mDefaultChannelId.c_str(), o)) {
    // Lift channel
    if (o->get("value", vo, true)) {
      // non-null channel value
      Percent100ths targetvalue = bridge2matter(vo->doubleValue(), mode.Has(WindowCovering::Mode::kMotorDirectionReversed), true);
      // - always report target value, WindowCovering cluster relies on that
      WindowCovering::Attributes::TargetPositionLiftPercent100ths::Set(endpointId(), targetvalue);
      if (moving && o->get("x-p44-transitional", vo, true)) {
        // we know the actual transitional current position value
        Percent100ths currentvalue = bridge2matter(vo->doubleValue(), mode.Has(WindowCovering::Mode::kMotorDirectionReversed), true);
        WindowCovering::LiftPositionSet(endpointId(), WindowCovering::NPercent100ths(currentvalue));
      }
      else {
        // we do not know a transitional value
        if (!moving) {
          // not moving, assume target and current equal
          WindowCovering::LiftPositionSet(endpointId(), WindowCovering::NPercent100ths(targetvalue));
        }
      }
    }
  }
  if (aChannelStates->get("shadeOpeningAngleOutside", o)) {
    // Tilt channel
    if (o->get("value", vo, true)) {
      // non-null channel value
      Percent100ths targetvalue = bridge2matter(vo->doubleValue(), mode.Has(WindowCovering::Mode::kMotorDirectionReversed), false);
      // - always report target value, WindowCovering cluster relies on that
      WindowCovering::Attributes::TargetPositionTiltPercent100ths::Set(endpointId(), targetvalue);
      if (moving && o->get("x-p44-transitional", vo, true)) {
        // we know the actual transitional current position value
        Percent100ths currentvalue = bridge2matter(vo->doubleValue(), mode.Has(WindowCovering::Mode::kMotorDirectionReversed), false);
        WindowCovering::TiltPositionSet(endpointId(), WindowCovering::NPercent100ths(currentvalue));
      }
      else {
        // we do not know a transitional value
        if (!moving) {
          // not moving, assume target and current equal
          WindowCovering::TiltPositionSet(endpointId(), WindowCovering::NPercent100ths(targetvalue));
        }
      }
    }
  }
}


// MARK: - P44_InputDevice


const string P44_InputImpl::endpointUIDSuffix() const
{
  return mInputType + "_" + mInputId;
}


void P44_InputImpl::initBridgedInfo(JsonObjectPtr aDeviceInfo, const char* aInputType, const char* aInputId)
{
  mInputType = aInputType;
  mInputId = aInputId;
  inherited::initBridgedInfo(aDeviceInfo, aInputType, aInputId);
}


// MARK: - P44_SensorImpl

void P44_SensorImpl::updateBridgedInfo(JsonObjectPtr aDeviceInfo)
{
  // basics first
  inherited::updateBridgedInfo(aDeviceInfo);

  JsonObjectPtr o;
  double min = 0;
  double max = 0;
  double tolerance = 0; // unknown
  bool hasMin = false;
  bool hasMax = false;

  SensorDevice* dev = deviceP<SensorDevice>();
  JsonObjectPtr descriptions;
  if (aDeviceInfo->get((mInputType+"Descriptions").c_str(), descriptions)) {
    if (descriptions->get("resolution", o)) {
      // tolerance is half of the resolution (when resolution is 1, true value might be max +/- 0.5 resolution away)
      tolerance = o->doubleValue()/2;
    }
    if (descriptions->get("min", o)) {
      hasMin = true;
      min = o->doubleValue();
    }
    if (descriptions->get("max", o)) {
      hasMax = true;
      max = o->doubleValue();
    }
    dev->setupSensorParams(hasMin, min, hasMax, max, tolerance);
  }
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


void P44_BinaryInputImpl::updateBridgedInfo(JsonObjectPtr aDeviceInfo)
{
  // basics first
  inherited::updateBridgedInfo(aDeviceInfo);
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
      BinaryInputDevice* dev = deviceP<BinaryInputDevice>();
      if (state->get("value", o, true)) {
        // non-NULL value, forward value, possibly inverted
        dev->updateCurrentState(o->boolValue()!=mInverted, true, aUpdateMode);
      }
      else {
        // NULL value or no value contained in state at all
        dev->updateCurrentState(false, false, aUpdateMode);
      }
    }
  }
}


// MARK: - P44_ButtonImpl

#include <app/clusters/switch-server/switch-server.h>

void P44_ButtonImpl::updateBridgedInfo(JsonObjectPtr aDeviceInfo)
{
  // basics first
  inherited::updateBridgedInfo(aDeviceInfo);
  // now button specifics
  mClicks = 0;
  mPosition = 0;
  // configure switch
  SwitchDevice* dev = deviceP<SwitchDevice>();
  // - number of positions
  Switch::Attributes::NumberOfPositions::Set(endpointId(), (uint8_t)dev->mActivePositions.size());
  // - fixed features for P44 buttons
  Switch::Attributes::FeatureMap::Set(endpointId(),
    to_underlying(Switch::Feature::kMomentarySwitch) |
    to_underlying(Switch::Feature::kMomentarySwitchRelease) |
    to_underlying(Switch::Feature::kMomentarySwitchLongPress) |
    to_underlying(Switch::Feature::kMomentarySwitchMultiPress)
  );
  Switch::Attributes::MultiPressMax::Set(endpointId(), 4);
  // get current value from xxxStates
  parseButtonState(aDeviceInfo, UpdateMode());
}


void P44_ButtonImpl::handleBridgePushProperties(JsonObjectPtr aChangedProperties)
{
  inherited::handleBridgePushProperties(aChangedProperties);
  parseButtonState(aChangedProperties, UpdateMode(UpdateFlags::matter));
}


void P44_ButtonImpl::parseButtonState(JsonObjectPtr aProperties, UpdateMode aUpdateMode)
{
  SwitchDevice* dev = deviceP<SwitchDevice>();
  JsonObjectPtr states;
  if (aProperties->get((mInputType+"States").c_str(), states)) {
    JsonObjectPtr state;
    // scan all active positions
    for (DevicePushbutton::PositionsMap::iterator pos = dev->mActivePositions.begin(); pos!=dev->mActivePositions.end(); ++pos) {
      if (states->get(pos->second.c_str(), state)) {
        JsonObjectPtr o;
        if (state->get("value", o, true)) {
          uint8_t position = static_cast<uint8_t>(o->boolValue() ? pos->first : 0); // active position or idle
          DsClickType clicktype = ct_none;
          if (state->get("clickType", o, true)) {
            clicktype = (DsClickType)(o->int32Value());
          }
          if (position!=mPosition || clicktype==ct_complete) {
            // actual change of position
            switch(clicktype) {
              case ct_tip_1x:
              case ct_tip_2x:
              case ct_tip_3x:
              case ct_tip_4x:
                // update tips (count as clicks)
                mClicks = (uint8_t)(clicktype-ct_tip_1x+1);
                goto multi;
              case ct_click_1x:
              case ct_click_2x:
              case ct_click_3x:
                // update clicks
                mClicks = (uint8_t)(clicktype-ct_click_1x+1);
              multi:
                if (position==0) {
                  // any tip or click detection also implies short release
                  SwitchServer::Instance().OnShortRelease(endpointId(), mPosition); // report previous position
                }
                break;
              case ct_progress:
                if (position==0) {
                  // released
                  SwitchServer::Instance().OnShortRelease(endpointId(), mPosition); // report previous position
                }
                else {
                  // pressed
                  SwitchServer::Instance().OnInitialPress(endpointId(), position); // report new position
                  mClicks++; // preliminary counting, will be overridden by (but should be equal to) regular click/tip count
                  if (mClicks>1) {
                    // actual progress beyond single click
                    SwitchServer::Instance().OnMultiPressOngoing(endpointId(), position, mClicks); // report new position
                  }
                }
                break;
              case ct_complete:
                if (position==0) {
                  // Note: when we have multipress support, even single clicks need the OnMultiPressComplete event!
                  SwitchServer::Instance().OnMultiPressComplete(endpointId(), mPosition, mClicks); // report previous position
                }
                mClicks = 0;
                break;
              case ct_hold_start:
                SwitchServer::Instance().OnLongPress(endpointId(), position); // report new position
                break;
              case ct_hold_end:
                SwitchServer::Instance().OnLongRelease(endpointId(), mPosition); // report previous position
                break;
              default:
                break;
            }
            mPosition = position;
          }
        }
        // only evaluate ONE button state (should be only one)
        break;
      }
    }
  }
}


#endif // P44_ADAPTERS
