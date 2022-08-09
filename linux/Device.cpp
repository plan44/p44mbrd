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


#include "device_impl.h"

// REVISION DEFINITIONS:
// TODO: move these to a better place, probably into the devices that actually handle them, or
//   try to extract them from ZAP-generated defs
// =================================================================================

#define ZCL_DESCRIPTOR_CLUSTER_REVISION (1u)
#define ZCL_BRIDGED_DEVICE_BASIC_CLUSTER_REVISION (1u)
#define ZCL_BRIDGED_DEVICE_BASIC_FEATURE_MAP (0u)
#define ZCL_FIXED_LABEL_CLUSTER_REVISION (1u)



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


void Device::notify(const string aNotification, JsonObjectPtr aParams)
{
  if (!aParams) aParams = JsonObject::newObj();
  aParams->add("dSUID", JsonObject::newString(mBridgedDSUID));
  BridgeApi::sharedBridgeApi().notify(aNotification, aParams);
}

void Device::call(const string aNotification, JsonObjectPtr aParams, BridgeApiCB aResponseCB)
{
  if (!aParams) aParams = JsonObject::newObj();
  aParams->add("dSUID", JsonObject::newString(mBridgedDSUID));
  BridgeApi::sharedBridgeApi().call(aNotification, aParams, aResponseCB);
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
