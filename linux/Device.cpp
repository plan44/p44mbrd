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
    else if ((attributeId == ZCL_NODE_LABEL_ATTRIBUTE_ID) && (maxReadLength == 32)) {
      MutableByteSpan zclNameSpan(buffer, maxReadLength);
      MakeZclCharString(zclNameSpan, GetName());
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    else if ((attributeId == ZCL_CLUSTER_REVISION_SERVER_ATTRIBUTE_ID) && (maxReadLength == 2)) {
      *buffer = (uint16_t) ZCL_BRIDGED_DEVICE_BASIC_CLUSTER_REVISION;
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    else if ((attributeId == ZCL_FEATURE_MAP_SERVER_ATTRIBUTE_ID) && (maxReadLength == 4)) {
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

DeviceOnOff::DeviceOnOff(const char * szDeviceName, std::string szLocation, std::string aDSUID) : Device(szDeviceName, szLocation, aDSUID)
{
  mOn = false;
}

bool DeviceOnOff::IsOn()
{
  return mOn;
}

void DeviceOnOff::SetOnOff(bool aOn)
{
  bool changed;

  changed = aOn ^ mOn;
  mOn     = aOn;
  ChipLogProgress(DeviceLayer, "Device[%s]: %s", mName, aOn ? "ON" : "OFF");

  if (changed)
  {
    // call preset1 or off on the bridged device
    JsonObjectPtr params = JsonObject::newObj();
    params->add("dSUID", JsonObject::newString(mBridgedDSUID));
    params->add("scene", JsonObject::newInt32(mOn ? 5 : 0));
    params->add("force", JsonObject::newBool(true));
    BridgeApi::sharedBridgeApi().notify("callScene", params);

    // report back to matter
    MatterReportingAttributeChangeCallback(GetEndpointId(), ZCL_BRIDGED_DEVICE_BASIC_CLUSTER_ID, ZCL_NODE_LABEL_ATTRIBUTE_ID);
  }
}

void DeviceOnOff::Toggle()
{
    bool aOn = !IsOn();
    SetOnOff(aOn);
}


EmberAfStatus DeviceOnOff::HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength)
{
  if (clusterId==ZCL_ON_OFF_CLUSTER_ID) {
    if ((attributeId == ZCL_ON_OFF_ATTRIBUTE_ID) && (maxReadLength == 1)) {
      *buffer = IsOn() ? 1 : 0;
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    else if ((attributeId == ZCL_CLUSTER_REVISION_SERVER_ATTRIBUTE_ID) && (maxReadLength == 2)) {
      *buffer = (uint16_t) ZCL_ON_OFF_CLUSTER_REVISION;
      return EMBER_ZCL_STATUS_SUCCESS;
    }
  }
  // let base class try
  return inherited::HandleReadAttribute(clusterId, attributeId, buffer, maxReadLength);
}


EmberAfStatus DeviceOnOff::HandleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer)
{
  if (clusterId==ZCL_ON_OFF_CLUSTER_ID) {
    if ((attributeId == ZCL_ON_OFF_ATTRIBUTE_ID) && (IsReachable())) {
      if (*buffer) {
        SetOnOff(true);
      }
      else {
        SetOnOff(false);
      }
      return EMBER_ZCL_STATUS_SUCCESS;
    }
  }
  // let base class try
  return inherited::HandleWriteAttribute(clusterId, attributeId, buffer);
}
