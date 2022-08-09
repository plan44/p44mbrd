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

// MARK: - bridged device common declarations

// REVISION DEFINITIONS:
// TODO: move these to a better place, probably into the devices that actually handle them, or
//   try to extract them from ZAP-generated defs
// =================================================================================

#define ZCL_DESCRIPTOR_CLUSTER_REVISION (1u)
#define ZCL_BRIDGED_DEVICE_BASIC_CLUSTER_REVISION (1u)
#define ZCL_BRIDGED_DEVICE_BASIC_FEATURE_MAP (0u)
#define ZCL_FIXED_LABEL_CLUSTER_REVISION (1u)

const int kNodeLabelSize = 32;

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

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(bridgedDeviceCommonClusters)
    DECLARE_DYNAMIC_CLUSTER(ZCL_DESCRIPTOR_CLUSTER_ID, descriptorAttrs, nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(ZCL_BRIDGED_DEVICE_BASIC_CLUSTER_ID, bridgedDeviceBasicAttrs, nullptr, nullptr)
DECLARE_DYNAMIC_CLUSTER_LIST_END;


using namespace chip::app::Clusters::BridgedActions;


// MARK: - Device

Device::Device(std::string aDSUID) :
  mBridgedDSUID(aDSUID)
{
  // init
  mName = string_format("unnamed@%p", this);
  mReachable  = false;
  mDynamicEndpointIdx = kInvalidEndpointId;
  mDynamicEndpointBase = 0;

  // - endpoint declaration info
  mEndpointDefinition.clusterCount = 0;
  mEndpointDefinition.cluster = nullptr;
  mEndpointDefinition.endpointSize = 0; // dynamic endpoints do not have any non-external attributes
  mClusterDataVersionsP = nullptr; // we'll need
  mDeviceTypeListP = nullptr;
  mParentEndpointId = kInvalidEndpointId;

  // - declare common bridged device clusters
  addClusterDeclarations(Span<EmberAfCluster>(bridgedDeviceCommonClusters));
}


Device::~Device()
{
  if (mClusterDataVersionsP) {
    delete mClusterDataVersionsP;
    mClusterDataVersionsP = nullptr;
  }
}


// MARK: cluster declaration

void Device::addClusterDeclarations(const Span<EmberAfCluster>& aClusterDeclarationList)
{
  mClusterListCollector.push_back(aClusterDeclarationList);
}

void Device::finalizeDeviceDeclarationWithTypes(const Span<const EmberAfDeviceType>& aDeviceTypeList)
{
  // save the device type list
  mDeviceTypeListP = &aDeviceTypeList;
  // now finally populate the endpoint definition
  // - count the clusters
  mEndpointDefinition.clusterCount = 0;
  for (std::list<Span<EmberAfCluster>>::iterator pos = mClusterListCollector.begin(); pos!=mClusterListCollector.end(); ++pos) {
    mEndpointDefinition.clusterCount += pos->size();
  }
  // - generate the final clusters
  mEndpointDefinition.cluster = new EmberAfCluster[mEndpointDefinition.clusterCount];
  int i = 0;
  for (std::list<Span<EmberAfCluster>>::iterator pos = mClusterListCollector.begin(); pos!=mClusterListCollector.end(); ++pos) {
    for (int j=0; j<pos->size(); j++) {
      memcpy((void *)&mEndpointDefinition.cluster[i], &pos->data()[j], sizeof(EmberAfCluster));
      i++;
    }
  }
  mClusterListCollector.clear(); // don't need this any more
  // - allocate the cluster data versions storage
  if (mClusterDataVersionsP) delete mClusterDataVersionsP;
  mClusterDataVersionsP = new DataVersion[mEndpointDefinition.clusterCount];
}


bool Device::AddAsDeviceEndpoint(EndpointId aDynamicEndpointBase)
{
  // finalize the declaration
  finalizeDeviceDeclaration();
  // add as dynamic endpoint
  mDynamicEndpointBase = aDynamicEndpointBase;
  EmberAfStatus ret = emberAfSetDynamicEndpoint(
    mDynamicEndpointIdx,
    GetEndpointId(),
    &mEndpointDefinition,
    Span<DataVersion>(mClusterDataVersionsP, mEndpointDefinition.clusterCount),
    *mDeviceTypeListP,
    mParentEndpointId
  );
  if (ret==EMBER_ZCL_STATUS_SUCCESS) {
    ChipLogProgress(
      DeviceLayer, "Added device '%s' to dynamic endpoint %d (index=%d)",
      GetName().c_str(), GetEndpointId(), mDynamicEndpointIdx
    );
  }
  else {
    ChipLogError(DeviceLayer, "emberAfSetDynamicEndpoint failed with EmberAfStatus=%d", ret);
    return false;
  }
  return true;
}


// MARK: functionality

bool Device::IsReachable()
{
  return mReachable;
}

void Device::SetReachable(bool aReachable)
{
  bool changed = (mReachable != aReachable);
  mReachable = aReachable;
  if (aReachable) {
    ChipLogProgress(DeviceLayer, "Device[%s]: ONLINE", mName.c_str());
  }
  else {
    ChipLogProgress(DeviceLayer, "Device[%s]: OFFLINE", mName.c_str());
  }
  if (changed) {
    MatterReportingAttributeChangeCallback(GetEndpointId(), ZCL_BRIDGED_DEVICE_BASIC_CLUSTER_ID, ZCL_REACHABLE_ATTRIBUTE_ID);
  }
}

void Device::SetName(const std::string aDeviceName)
{
  ChipLogProgress(DeviceLayer, "Device[%s]: New Name=\"%s\"", mName.c_str(), aDeviceName.c_str());
  if (mName!=aDeviceName) {
    mName = aDeviceName;
    // propagate to native device
    JsonObjectPtr params = JsonObject::newObj();
    params->add("dSUID", JsonObject::newString(mBridgedDSUID));
    JsonObjectPtr props = JsonObject::newObj();
    props->add("name", JsonObject::newString(mName));
    params->add("properties", props);
    BridgeApi::sharedBridgeApi().call("setProperty", params, NULL);
    // report to matter
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


EmberAfStatus Device::HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength)
{
  if (clusterId==ZCL_BRIDGED_DEVICE_BASIC_CLUSTER_ID) {
    if ((attributeId == ZCL_REACHABLE_ATTRIBUTE_ID) && (maxReadLength == 1)) {
      *buffer = IsReachable() ? 1 : 0;
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if ((attributeId == ZCL_NODE_LABEL_ATTRIBUTE_ID) && (maxReadLength == 32)) {
      MutableByteSpan zclNameSpan(buffer, maxReadLength);
      MakeZclCharString(zclNameSpan, GetName().c_str());
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
