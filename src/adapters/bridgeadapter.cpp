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

#include "bridgeadapter.h"


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


string P44_DeviceImpl::vendorName() const
{
  return mVendorName;
}


string P44_DeviceImpl::modelName() const
{
  return mModelName;
}


string P44_DeviceImpl::configUrl() const
{
  return mConfigUrl;
}


string P44_DeviceImpl::serialNo() const
{
  return mSerialNo;
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
    params->add("dSUID", JsonObject::newString(mBridgedDSUID));
    JsonObjectPtr props = JsonObject::newObj();
    props->add("name", JsonObject::newString(mName));
    params->add("properties", props);
    BridgeApi::api().call("setProperty", params, NoOP);
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
  mBridgedDSUID = o->stringValue();
  mSerialNo = mBridgedDSUID; // default serial number
  // - optionals
  if (aDeviceInfo->get("displayId", o)) {
    mSerialNo = o->stringValue();
  }
  if (aDeviceInfo->get("vendorName", o)) {
    mVendorName = o->stringValue();
  }
  if (aDeviceInfo->get("model", o)) {
    mModelName = o->stringValue();
  }
  if (aDeviceInfo->get("configURL", o)) {
    mConfigUrl = o->stringValue();
  }
  // - default name
  if (aDeviceInfo->get("name", o)) {
    mName = o->stringValue();
    device().updateNodeLabel(mName, UpdateMode());
  }
  // - default zone name
  if (aDeviceInfo->get("x-p44-zonename", o)) {
    mZone = o->stringValue();
  }
  // - initial reachability
  if (aDeviceInfo->get("active", o)) {
    mActive = o->boolValue();
    device().updateReachable(isReachable(), UpdateMode());
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
  BridgeApi::api().notify(aNotification, aParams);
}


void P44_DeviceImpl::call(const string aMethod, JsonObjectPtr aParams, JSonMessageCB aResponseCB)
{
  if (!aParams) aParams = JsonObject::newObj();
  DLOG(LOG_NOTICE, "mbr -> vdcd: calling method '%s': %s", aMethod.c_str(), aParams->json_c_str());
  aParams->add("dSUID", JsonObject::newString(mBridgedDSUID));
  BridgeApi::api().call(aMethod, aParams, aResponseCB);
}


// MARK: - P44_ComposedDevice

void P44_ComposedImpl::handleBridgePushProperties(JsonObjectPtr aChangedProperties)
{
  for (DevicesList::iterator pos = device().subDevices().begin(); pos!=device().subDevices().end(); ++pos) {
    P44_DeviceImpl::impl(*pos)->handleBridgePushProperties(aChangedProperties);
  }
}


// MARK: - P44_InputDevice

void P44_InputImpl::initBridgedInfo(JsonObjectPtr aDeviceInfo, JsonObjectPtr aDeviceComponentInfo, const char* aInputType, const char* aInputId)
{
  mInputType = aInputType;
  mInputId = aInputId;
  inherited::initBridgedInfo(aDeviceInfo, aDeviceComponentInfo, aInputType, aInputId);
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


// MARK: - P44_OnOffDevice

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
//    if (co->get("channelType", o2)) {
//      if (o2->int32Value()==0) { mDefaultChannelId = cid; break; }
//    }
  }
  // output devices should examine the channel states
  o = aDeviceInfo->get("channelStates");
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

