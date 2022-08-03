/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
 *    Copyright (c) 2019 Google LLC.
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

#include "Device.h"
#include "bridgeapi.hpp"

#include <cstdio>
#include <platform/CHIPDeviceLayer.h>

#include <app-common/zap-generated/attribute-id.h>
#include <app-common/zap-generated/cluster-id.h>
#include "ZclString.h"
#include <app/reporting/reporting.h>

// REVISION DEFINITIONS:
// TODO: move these to a better place, probably into the devices that actually handle them, or
//   try to extract them from ZAP-generated defs
// =================================================================================

#define ZCL_DESCRIPTOR_CLUSTER_REVISION (1u)
#define ZCL_BRIDGED_DEVICE_BASIC_CLUSTER_REVISION (1u)
#define ZCL_BRIDGED_DEVICE_BASIC_FEATURE_MAP (0u)
#define ZCL_FIXED_LABEL_CLUSTER_REVISION (1u)
#define ZCL_ON_OFF_CLUSTER_REVISION (4u)
#define ZCL_LEVEL_CONTROL_CLUSTER_REVISION (5u)
#define ZCL_COLOR_CONTROL_CLUSTER_REVISION (5u)



using namespace chip::app::Clusters::BridgedActions;

// MARK: - Device

Device::Device(const char * szDeviceName, std::string szLocation, std::string aDSUID)
{
  strncpy(mName, szDeviceName, sizeof(mName));
  mLocation   = szLocation;
  mReachable  = false;
  mDynamicEndpointIdx = kInvalidEndpointId;
  mDynamicEndpointBase = 0;
  // p44
  mBridgedDSUID = aDSUID;
  // - instantiation info
  mNumClusterVersions = 0;
  mClusterDataVersionsP = nullptr;
  mEndpointTypeP = nullptr;
  mDeviceTypeListP = nullptr;
  mParentEndpointId = kInvalidEndpointId;
}


Device::~Device()
{
  if (mClusterDataVersionsP) {
    delete mClusterDataVersionsP;
    mClusterDataVersionsP = nullptr;
  }
}


bool Device::IsReachable()
{
  return mReachable;
}

void Device::SetReachable(bool aReachable)
{
  bool changed = (mReachable != aReachable);
  mReachable = aReachable;
  if (aReachable) {
    ChipLogProgress(DeviceLayer, "Device[%s]: ONLINE", mName);
  }
  else {
    ChipLogProgress(DeviceLayer, "Device[%s]: OFFLINE", mName);
  }
  if (changed) {
    MatterReportingAttributeChangeCallback(GetEndpointId(), ZCL_BRIDGED_DEVICE_BASIC_CLUSTER_ID, ZCL_REACHABLE_ATTRIBUTE_ID);
  }
}

void Device::SetName(const char * szName)
{
  bool changed = (strncmp(mName, szName, sizeof(mName)) != 0);

  ChipLogProgress(DeviceLayer, "Device[%s]: New Name=\"%s\"", mName, szName);
  strncpy(mName, szName, sizeof(mName));

  if (changed) {
    JsonObjectPtr params = JsonObject::newObj();
    params->add("dSUID", JsonObject::newString(mBridgedDSUID));
    JsonObjectPtr props = JsonObject::newObj();
    props->add("name", JsonObject::newString(mName));
    params->add("properties", props);
    BridgeApi::sharedBridgeApi().call("setProperty", params, NULL);

    MatterReportingAttributeChangeCallback(GetEndpointId(), ZCL_BRIDGED_DEVICE_BASIC_CLUSTER_ID, ZCL_NODE_LABEL_ATTRIBUTE_ID);
  }
}


void Device::setUpClusterInfo(
  size_t aNumClusterVersions,
  EmberAfEndpointType* aEndpointTypeP,
  const Span<const EmberAfDeviceType>& aDeviceTypeList,
  EndpointId aParentEndpointId
)
{
  // save number of clusters and create storage for cluster data versions
  mNumClusterVersions = aNumClusterVersions;
  if (mClusterDataVersionsP) delete mClusterDataVersionsP;
  mClusterDataVersionsP = new DataVersion[mNumClusterVersions];
  // save other params
  mEndpointTypeP = aEndpointTypeP;
  mDeviceTypeListP = &aDeviceTypeList;
  mParentEndpointId = aParentEndpointId;
}


bool Device::AddAsDeviceEndpoint(EndpointId aDynamicEndpointBase)
{
  // allocate data versions
  mDynamicEndpointBase = aDynamicEndpointBase;
  EmberAfStatus ret = emberAfSetDynamicEndpoint(
    mDynamicEndpointIdx,
    GetEndpointId(),
    mEndpointTypeP,
    Span<DataVersion>(mClusterDataVersionsP, mNumClusterVersions),
    *mDeviceTypeListP,
    mParentEndpointId
  );
  if (ret==EMBER_ZCL_STATUS_SUCCESS) {
    ChipLogProgress(
      DeviceLayer, "Added device %s to dynamic endpoint %d (index=%d)",
      GetName(), GetEndpointId(), mDynamicEndpointIdx
    );
  }
  else {
    ChipLogError(DeviceLayer, "emberAfSetDynamicEndpoint failed with EmberAfStatus=%d", ret);
    return false;
  }
  return true;
}


EmberAfStatus Device::HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength)
{
  if (clusterId==ZCL_BRIDGED_DEVICE_BASIC_CLUSTER_ID) {
    if ((attributeId == ZCL_REACHABLE_ATTRIBUTE_ID) && (maxReadLength == 1)) {
      *buffer = IsReachable() ? 1 : 0;
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if ((attributeId == ZCL_NODE_LABEL_ATTRIBUTE_ID) && (maxReadLength == 32)) {
      MutableByteSpan zclNameSpan(buffer, maxReadLength);
      MakeZclCharString(zclNameSpan, GetName());
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if ((attributeId == ZCL_CLUSTER_REVISION_SERVER_ATTRIBUTE_ID) && (maxReadLength == 2)) {
      *buffer = (uint16_t) ZCL_BRIDGED_DEVICE_BASIC_CLUSTER_REVISION;
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if ((attributeId == ZCL_FEATURE_MAP_SERVER_ATTRIBUTE_ID) && (maxReadLength == 4)) {
      *buffer = (uint32_t) ZCL_BRIDGED_DEVICE_BASIC_FEATURE_MAP;
      return EMBER_ZCL_STATUS_SUCCESS;
    }
  }
  return EMBER_ZCL_STATUS_FAILURE;
}


EmberAfStatus Device::HandleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer)
{
  // handle common device attributes
  // FIXME: No writeable common attributes at this time
  return EMBER_ZCL_STATUS_FAILURE;
}




// MARK: - DeviceOnOff

DeviceOnOff::DeviceOnOff(const char * szDeviceName, std::string szLocation, std::string aDSUID) :
  inherited(szDeviceName, szLocation, aDSUID)
{
  mOn = false;
}


EmberAfStatus DeviceOnOff::HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength)
{
  if (clusterId==ZCL_ON_OFF_CLUSTER_ID) {
    if ((attributeId == ZCL_CLUSTER_REVISION_SERVER_ATTRIBUTE_ID) && (maxReadLength == 2)) {
      *((uint16_t *)buffer) = ZCL_ON_OFF_CLUSTER_REVISION;
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if ((attributeId == ZCL_ON_OFF_ATTRIBUTE_ID) && (maxReadLength == 1)) {
      *buffer = IsOn() ? 1 : 0;
      return EMBER_ZCL_STATUS_SUCCESS;
    }
  }
  // let base class try
  return inherited::HandleReadAttribute(clusterId, attributeId, buffer, maxReadLength);
}


bool DeviceOnOff::SetOnOff(bool aOn)
{
  ChipLogProgress(DeviceLayer, "Device[%s]: %s", mName, aOn ? "ON" : "OFF");
  if (aOn!=mOn) {
    mOn  = aOn;
    // call preset1 or off on the bridged device
    JsonObjectPtr params = JsonObject::newObj();
    params->add("dSUID", JsonObject::newString(mBridgedDSUID));
    params->add("scene", JsonObject::newInt32(mOn ? 5 : 0));
    params->add("force", JsonObject::newBool(true));
    BridgeApi::sharedBridgeApi().notify("callScene", params);
    // report back to matter
    MatterReportingAttributeChangeCallback(GetEndpointId(), ZCL_BRIDGED_DEVICE_BASIC_CLUSTER_ID, ZCL_NODE_LABEL_ATTRIBUTE_ID);
    return true; // changed
  }
  return false; // no change
}


EmberAfStatus DeviceOnOff::HandleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer)
{
  if (clusterId==ZCL_ON_OFF_CLUSTER_ID) {
    if ((attributeId == ZCL_ON_OFF_ATTRIBUTE_ID) && IsReachable()) {
      SetOnOff(*buffer);
      return EMBER_ZCL_STATUS_SUCCESS;
    }
  }
  // let base class try
  return inherited::HandleWriteAttribute(clusterId, attributeId, buffer);
}


// MARK: - DeviceDimmable

DeviceDimmable::DeviceDimmable(const char * szDeviceName, std::string szLocation, std::string aDSUID) :
  inherited(szDeviceName, szLocation, aDSUID),
  mLevel(0)
{
}


EmberAfStatus DeviceDimmable::HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength)
{
  if (clusterId==ZCL_LEVEL_CONTROL_CLUSTER_ID) {
    if ((attributeId == ZCL_CLUSTER_REVISION_SERVER_ATTRIBUTE_ID) && (maxReadLength == 2)) {
      *((uint16_t *)buffer) = ZCL_LEVEL_CONTROL_CLUSTER_REVISION;
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if ((attributeId == ZCL_CURRENT_LEVEL_ATTRIBUTE_ID) && (maxReadLength == 1)) {
      *buffer = currentLevel();
      return EMBER_ZCL_STATUS_SUCCESS;
    }
  }
  // let base class try
  return inherited::HandleReadAttribute(clusterId, attributeId, buffer, maxReadLength);
}


bool DeviceDimmable::setCurrentLevel(uint8_t aNewLevel)
{
  ChipLogProgress(DeviceLayer, "Device[%s]: set level to %d", mName, aNewLevel);
  if (aNewLevel!=mLevel) {
    mLevel = aNewLevel;
    // adjust default channel
    JsonObjectPtr params = JsonObject::newObj();
    params->add("dSUID", JsonObject::newString(mBridgedDSUID));
    params->add("channel", JsonObject::newInt32(0)); // default channel
    params->add("value", JsonObject::newDouble((double)mLevel*100/0xFE));
    params->add("apply_now", JsonObject::newBool(true));
    BridgeApi::sharedBridgeApi().notify("setOutputChannelValue", params);
    // report back to matter
    MatterReportingAttributeChangeCallback(GetEndpointId(), ZCL_LEVEL_CONTROL_CLUSTER_ID, ZCL_CURRENT_LEVEL_ATTRIBUTE_ID);
    return true; // changed
  }
  return false; // no change
}


EmberAfStatus DeviceDimmable::HandleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer)
{
  if (clusterId==ZCL_LEVEL_CONTROL_CLUSTER_ID) {
    if ((attributeId == ZCL_CURRENT_LEVEL_ATTRIBUTE_ID) && IsReachable()) {
      setCurrentLevel(*buffer);
      return EMBER_ZCL_STATUS_SUCCESS;
    }
  }
  // let base class try
  return inherited::HandleWriteAttribute(clusterId, attributeId, buffer);
}


// MARK: - DeviceColor

DeviceColor::DeviceColor(const char * szDeviceName, std::string szLocation, std::string aDSUID, bool aCTOnly) :
  inherited(szDeviceName, szLocation, aDSUID),
  mCtOnly(aCTOnly),
  mColorMode(aCTOnly ? 2 : 0),
  mHue(0),
  mSaturation(0),
  mColorTemp(0)
{
}


EmberAfStatus DeviceColor::HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength)
{
  if (clusterId==ZCL_COLOR_CONTROL_CLUSTER_ID) {
    if ((attributeId == ZCL_CLUSTER_REVISION_SERVER_ATTRIBUTE_ID) && (maxReadLength == 2)) {
      *((uint16_t *)buffer) = ZCL_COLOR_CONTROL_CLUSTER_REVISION;
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if ((attributeId == ZCL_COLOR_CONTROL_COLOR_CAPABILITIES_ATTRIBUTE_ID) && (maxReadLength == 1)) {
      // color capabilities: Bit0=HS, Bit1=EnhancedHS, Bit2=ColorLoop, Bit3=XY, Bit4=ColorTemp
      *buffer = (1<<4) | (mCtOnly ? 0 : (1<<0));
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if ((attributeId == ZCL_COLOR_CONTROL_COLOR_MODE_ATTRIBUTE_ID) && (maxReadLength == 1)) {
      // color mode: 0=HS, 1=XY, 2=Colortemp
      *buffer = mColorMode;
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if ((attributeId == ZCL_COLOR_CONTROL_CURRENT_HUE_ATTRIBUTE_ID) && (maxReadLength == 1)) {
      *buffer = currentHue();
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if ((attributeId == ZCL_COLOR_CONTROL_CURRENT_SATURATION_ATTRIBUTE_ID) && (maxReadLength == 1)) {
      *buffer = currentSaturation();
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if ((attributeId == ZCL_COLOR_CONTROL_COLOR_TEMPERATURE_ATTRIBUTE_ID)) {
      if (maxReadLength == 2) {
        *((uint16_t *)buffer) = currentColortemp();
      }
      else if (maxReadLength == 1) {
        *((uint8_t *)buffer) = (uint8_t)currentColortemp();
      }
      return EMBER_ZCL_STATUS_SUCCESS;
    }
  }
  // let base class try
  return inherited::HandleReadAttribute(clusterId, attributeId, buffer, maxReadLength);
}


bool DeviceColor::setCurrentHue(uint8_t aHue)
{
  ChipLogProgress(DeviceLayer, "Device[%s]: set hue to %d", mName, aHue);
  if (aHue!=mHue) {
    mHue = aHue;
    JsonObjectPtr params = JsonObject::newObj();
    params->add("dSUID", JsonObject::newString(mBridgedDSUID));
    params->add("channelId", JsonObject::newString("hue"));
    params->add("value", JsonObject::newDouble((double)mHue*360/0xFE));
    params->add("apply_now", JsonObject::newBool(true));
    BridgeApi::sharedBridgeApi().notify("setOutputChannelValue", params);
    if (mColorMode!=0) {
      mColorMode = 0; // switch to HS
      MatterReportingAttributeChangeCallback(GetEndpointId(), ZCL_COLOR_CONTROL_CLUSTER_ID, ZCL_COLOR_CONTROL_COLOR_MODE_ATTRIBUTE_ID);
    }
    // report back to matter
    MatterReportingAttributeChangeCallback(GetEndpointId(), ZCL_COLOR_CONTROL_CLUSTER_ID, ZCL_COLOR_CONTROL_CURRENT_HUE_ATTRIBUTE_ID);
    return true; // changed
  }
  return false; // no change
}


bool DeviceColor::setCurrentSaturation(uint8_t aSaturation)
{
  ChipLogProgress(DeviceLayer, "Device[%s]: set saturation to %d", mName, aSaturation);
  if (aSaturation!=mSaturation) {
    mSaturation = aSaturation;
    JsonObjectPtr params = JsonObject::newObj();
    params->add("dSUID", JsonObject::newString(mBridgedDSUID));
    params->add("channelId", JsonObject::newString("saturation"));
    params->add("value", JsonObject::newDouble((double)mSaturation*100/0xFE));
    params->add("apply_now", JsonObject::newBool(true));
    BridgeApi::sharedBridgeApi().notify("setOutputChannelValue", params);
    // report back to matter
    if (mColorMode!=0) {
      mColorMode = 0; // switch to HS
      MatterReportingAttributeChangeCallback(GetEndpointId(), ZCL_COLOR_CONTROL_CLUSTER_ID, ZCL_COLOR_CONTROL_COLOR_MODE_ATTRIBUTE_ID);
    }
    MatterReportingAttributeChangeCallback(GetEndpointId(), ZCL_COLOR_CONTROL_CLUSTER_ID, ZCL_COLOR_CONTROL_CURRENT_SATURATION_ATTRIBUTE_ID);
    return true; // changed
  }
  return false; // no change
}


bool DeviceColor::setCurrentColortemp(uint8_t aColortemp)
{
  ChipLogProgress(DeviceLayer, "Device[%s]: set colortemp to %d", mName, aColortemp);
  if (aColortemp!=mColorTemp) {
    mColorTemp = aColortemp;
    JsonObjectPtr params = JsonObject::newObj();
    params->add("dSUID", JsonObject::newString(mBridgedDSUID));
    params->add("channelId", JsonObject::newString("colortemp"));
    params->add("value", JsonObject::newDouble(mColorTemp)); // is in mireds
    params->add("apply_now", JsonObject::newBool(true));
    BridgeApi::sharedBridgeApi().notify("setOutputChannelValue", params);
    // report back to matter
    if (mColorMode!=2) {
      mColorMode = 2; // switch to CT
      MatterReportingAttributeChangeCallback(GetEndpointId(), ZCL_COLOR_CONTROL_CLUSTER_ID, ZCL_COLOR_CONTROL_COLOR_MODE_ATTRIBUTE_ID);
    }
    MatterReportingAttributeChangeCallback(GetEndpointId(), ZCL_COLOR_CONTROL_CLUSTER_ID, ZCL_COLOR_CONTROL_COLOR_TEMPERATURE_ATTRIBUTE_ID);
    return true; // changed
  }
  return false; // no change
}


EmberAfStatus DeviceColor::HandleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer)
{
  if (clusterId==ZCL_COLOR_CONTROL_CLUSTER_ID) {
    if ((attributeId == ZCL_CURRENT_LEVEL_ATTRIBUTE_ID) && IsReachable()) {
      setCurrentLevel(*buffer);
      return EMBER_ZCL_STATUS_SUCCESS;
    }
  }
  // let base class try
  return inherited::HandleWriteAttribute(clusterId, attributeId, buffer);
}


