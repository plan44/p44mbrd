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

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 1
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 6

#include "device_impl.h" // include as first file!

using namespace Clusters;

// MARK: - bridged device common declarations

// REVISION DEFINITIONS:
// TODO: move these to a better place, probably into the devices that actually handle them, or
//   try to extract them from ZAP-generated defs
// =================================================================================

#define ZCL_DESCRIPTOR_CLUSTER_REVISION (1u)
#define ZCL_BRIDGED_DEVICE_BASIC_CLUSTER_REVISION (1u)
#define ZCL_BRIDGED_DEVICE_BASIC_FEATURE_MAP (0u)
#define ZCL_FIXED_LABEL_CLUSTER_REVISION (1u)

const int kDefaultTextSize = 64;

// Declare Descriptor cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(descriptorAttrs)
  DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::DeviceTypeList::Id, ARRAY, kDescriptorAttributeArraySize, 0), /* device list */
  DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::ServerList::Id, ARRAY, kDescriptorAttributeArraySize, 0), /* server list */
  DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::ClientList::Id, ARRAY, kDescriptorAttributeArraySize, 0), /* client list */
  DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::PartsList::Id, ARRAY, kDescriptorAttributeArraySize, 0),  /* parts list */
DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// Declare Bridged Device Basic information cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(bridgedDeviceBasicAttrs)
  DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::NodeLabel::Id, CHAR_STRING, kDefaultTextSize, ZAP_ATTRIBUTE_MASK(WRITABLE)), /* Optional NodeLabel */
  DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::VendorName::Id, CHAR_STRING, kDefaultTextSize, 0), /* Optional Vendor Name */
  DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::ProductName::Id, CHAR_STRING, kDefaultTextSize, 0), /* Optional NodeLabel */
  DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::ProductURL::Id, CHAR_STRING, kDefaultTextSize, 0), /* Optional Product URL */
  DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::SerialNumber::Id, CHAR_STRING, kDefaultTextSize, 0), /* Optional Serial Number (dSUID) */
  DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::Reachable::Id, BOOLEAN, 1, 0),               /* Mandatory Reachable */
  DECLARE_DYNAMIC_ATTRIBUTE(Globals::Attributes::FeatureMap::Id, BITMAP32, 4, 0),     /* feature map */
DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(bridgedDeviceCommonClusters)
  DECLARE_DYNAMIC_CLUSTER(Descriptor::Id, descriptorAttrs, nullptr, nullptr),
  DECLARE_DYNAMIC_CLUSTER(BridgedDeviceBasicInformation::Id, bridgedDeviceBasicAttrs, nullptr, nullptr)
DECLARE_DYNAMIC_CLUSTER_LIST_END;


// MARK: - Device

Device::Device(DeviceInfoDelegate& aDeviceInfoDelegate) :
  mDeviceInfoDelegate(aDeviceInfoDelegate),
  mPartOfComposedDevice(false),
  mReachable(false)
{
  // matter side init
  mDynamicEndpointIdx = kInvalidEndpointId;
  mDynamicEndpointBase = 0;
  // - endpoint declaration info
  mEndpointDefinition.clusterCount = 0;
  mEndpointDefinition.cluster = nullptr;
  mEndpointDefinition.endpointSize = 0; // dynamic endpoints do not have any non-external attributes
  mClusterDataVersionsP = nullptr; // we'll need
  mParentEndpointId = kInvalidEndpointId;
  // - declare common bridged device clusters
  addClusterDeclarations(Span<EmberAfCluster>(bridgedDeviceCommonClusters));
}

string Device::logContextPrefix()
{
  string ep;
  if (GetEndpointId()!=kInvalidEndpointId) ep = string_format(" @endpoint %d", GetEndpointId());
  string pep;
  if (mPartOfComposedDevice && GetParentEndpointId()!=kInvalidEndpointId) pep = string_format(" (part of @endpoint %d)", GetParentEndpointId());
  return string_format(
    "%s %sdevice '%s'%s%s",
    deviceType(),
    mPartOfComposedDevice ? "sub" : "",
    mNodeLabel.c_str(),
    ep.c_str(),
    pep.c_str()
  );
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
  mDeviceTypeList = aDeviceTypeList;
  // now finally populate the endpoint definition
  // - count the clusters
  mEndpointDefinition.clusterCount = 0;
  for (std::list<Span<EmberAfCluster>>::iterator pos = mClusterListCollector.begin(); pos!=mClusterListCollector.end(); ++pos) {
    mEndpointDefinition.clusterCount += (uint8_t)pos->size();
  }
  // - generate the final clusters
  mEndpointDefinition.cluster = new EmberAfCluster[mEndpointDefinition.clusterCount];
  size_t i = 0;
  for (std::list<Span<EmberAfCluster>>::iterator pos = mClusterListCollector.begin(); pos!=mClusterListCollector.end(); ++pos) {
    for (size_t j=0; j<pos->size(); j++) {
      memcpy((void *)&mEndpointDefinition.cluster[i], &pos->data()[j], sizeof(EmberAfCluster));
      i++;
    }
  }
  mClusterListCollector.clear(); // don't need this any more
  // - allocate the cluster data versions storage
  if (mClusterDataVersionsP) delete mClusterDataVersionsP;
  mClusterDataVersionsP = new DataVersion[mEndpointDefinition.clusterCount];
}


bool Device::AddAsDeviceEndpoint()
{
  // finalize the declaration
  finalizeDeviceDeclaration();
  // add as dynamic endpoint
  EmberAfStatus ret = emberAfSetDynamicEndpoint(
    mDynamicEndpointIdx,
    GetEndpointId(),
    &mEndpointDefinition,
    Span<DataVersion>(mClusterDataVersionsP, mEndpointDefinition.clusterCount),
    mDeviceTypeList,
    mParentEndpointId
  );
  if (ret==EMBER_ZCL_STATUS_SUCCESS) {
    OLOG(LOG_INFO, "Added to CHIP as dynamic endpoint index #%d", mDynamicEndpointIdx);
  }
  else {
    OLOG(LOG_ERR, "emberAfSetDynamicEndpoint failed with EmberAfStatus=%d", ret);
    return false;
  }
  return true;
}


void Device::willBeInstalled()
{
  OLOG(LOG_DEBUG, "will be installed");
}


void Device::didBecomeOperational()
{
  OLOG(LOG_DEBUG, "did become operational");
}


// MARK: functionality

void Device::updateReachable(bool aReachable, UpdateMode aUpdateMode)
{
  if (mReachable!=aReachable || aUpdateMode.Has(UpdateFlags::forced)) {
    mReachable = aReachable;
    OLOG(LOG_INFO, "Updating reachable to %s - updatemode=%d", mReachable ? "REACHABLE" : "OFFLINE", aUpdateMode.Raw());
    if (aUpdateMode.Has(UpdateFlags::matter)) {
      MatterReportingAttributeChangeCallback(GetEndpointId(), BridgedDeviceBasicInformation::Id, BridgedDeviceBasicInformation::Attributes::Reachable::Id);
    }
  }
}

void Device::updateNodeLabel(const string aNodeLabel, UpdateMode aUpdateMode)
{
  if (mNodeLabel!=aNodeLabel || aUpdateMode.Has(UpdateFlags::forced)) {
    OLOG(LOG_INFO, "Updating node label to '%s' - updatemode=%d", aNodeLabel.c_str(), aUpdateMode.Raw());
    mNodeLabel = aNodeLabel;
    if (aUpdateMode.Has(UpdateFlags::bridged)) {
      // propagate to native device
      if (!mDeviceInfoDelegate.changeName(mNodeLabel)) {
        // cannot propagate
        OLOG(LOG_WARNING, "cannot set bridged device's name to nodeLabel");
      }
    }
    if (aUpdateMode.Has(UpdateFlags::matter)) {
      MatterReportingAttributeChangeCallback(GetEndpointId(), BridgedDeviceBasicInformation::Id, BridgedDeviceBasicInformation::Attributes::NodeLabel::Id);
    }
  }
}

// MARK: Attribute access

EmberAfStatus Device::HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength)
{
  if (clusterId==BasicInformation::Id) {
    OLOG(LOG_WARNING, "****** tried to access basic infomation cluster *****");
  }
  else if (clusterId==BridgedDeviceBasicInformation::Id) {
    if (attributeId == BridgedDeviceBasicInformation::Attributes::Reachable::Id) {
      return getAttr(buffer, maxReadLength, mDeviceInfoDelegate.isReachable());
    }
    // Writable Node Label
    if ((attributeId == BridgedDeviceBasicInformation::Attributes::NodeLabel::Id) && (maxReadLength == kDefaultTextSize)) {
      FOCUSOLOG("reading node label: %s", mNodeLabel.c_str());
      MutableByteSpan zclNameSpan(buffer, maxReadLength);
      MakeZclCharString(zclNameSpan, mNodeLabel.substr(0,maxReadLength-1).c_str());
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    // Device Information attributes
    if ((attributeId == BridgedDeviceBasicInformation::Attributes::VendorName::Id) && (maxReadLength == kDefaultTextSize)) {
      FOCUSOLOG("reading vendor name: %s", mDeviceInfoDelegate.vendorName().c_str());
      MutableByteSpan zclNameSpan(buffer, maxReadLength);
      MakeZclCharString(zclNameSpan, mDeviceInfoDelegate.vendorName().substr(0,maxReadLength-1).c_str());
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if ((attributeId == BridgedDeviceBasicInformation::Attributes::ProductName::Id) && (maxReadLength == kDefaultTextSize)) {
      FOCUSOLOG("reading product name: %s", mDeviceInfoDelegate.modelName().c_str());
      MutableByteSpan zclNameSpan(buffer, maxReadLength);
      MakeZclCharString(zclNameSpan, mDeviceInfoDelegate.modelName().substr(0,maxReadLength-1).c_str());
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if ((attributeId == BridgedDeviceBasicInformation::Attributes::ProductURL::Id) && (maxReadLength == kDefaultTextSize)) {
      FOCUSOLOG("reading product url: %s", mDeviceInfoDelegate.configUrl().c_str());
      MutableByteSpan zclNameSpan(buffer, maxReadLength);
      MakeZclCharString(zclNameSpan, mDeviceInfoDelegate.configUrl().substr(0,maxReadLength-1).c_str());
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if ((attributeId == BridgedDeviceBasicInformation::Attributes::SerialNumber::Id) && (maxReadLength == kDefaultTextSize)) {
      FOCUSOLOG("reading serial number: %s", mDeviceInfoDelegate.serialNo().c_str());
      MutableByteSpan zclNameSpan(buffer, maxReadLength);
      MakeZclCharString(zclNameSpan, mDeviceInfoDelegate.serialNo().c_str());
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    // common attributes
    if (attributeId == Globals::Attributes::ClusterRevision::Id) {
      return getAttr<uint16_t>(buffer, maxReadLength, ZCL_BRIDGED_DEVICE_BASIC_CLUSTER_REVISION);
    }
    if (attributeId == Globals::Attributes::FeatureMap::Id) {
      return getAttr<uint32_t>(buffer, maxReadLength, ZCL_BRIDGED_DEVICE_BASIC_FEATURE_MAP);
    }
  }
  // always implement an empty feature map, seems to be mandatory (according to ZAP tool, not specs)
  if (attributeId == Globals::Attributes::FeatureMap::Id) {
    FOCUSOLOG("reading generic empty feature map (endpoint does not have a specific one)");
    return getAttr<uint32_t>(buffer, maxReadLength, 0);
  }
  return EMBER_ZCL_STATUS_FAILURE;
}


EmberAfStatus Device::HandleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer)
{
  // handle common device attributes
  if (clusterId==BridgedDeviceBasicInformation::Id) {
    // Writable Node Label
    if (attributeId == BridgedDeviceBasicInformation::Attributes::NodeLabel::Id) {
      string newName((const char*)buffer+1, (size_t)buffer[0]);
      FOCUSOLOG("writing nodel label: new label = '%s'", newName.c_str());
      updateNodeLabel(newName, UpdateMode(UpdateFlags::bridged, UpdateFlags::matter));
      return EMBER_ZCL_STATUS_SUCCESS;
    }
  }
  return EMBER_ZCL_STATUS_FAILURE;
}


string Device::description()
{
  return string_format("device status:\n- reachable: %d", mReachable);
}


// MARK: - IdentifiableDevice

using namespace Identify;

// REVISION DEFINITIONS:
// TODO: move these to a better place, probably into the devices that actually handle them, or
//   try to extract them from ZAP-generated defs
// =================================================================================

#define ZCL_IDENTIFY_CLUSTER_REVISION (4u)
#define ZCL_IDENTIFY_CLUSTER_FEATURE_MAP (0) // no QRY

// Declare cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(identifyAttrs)
  DECLARE_DYNAMIC_ATTRIBUTE(Identify::Attributes::IdentifyTime::Id, INT16U, 2, 0),
  DECLARE_DYNAMIC_ATTRIBUTE(Identify::Attributes::IdentifyType::Id, ENUM8, 1, 0),
  DECLARE_DYNAMIC_ATTRIBUTE(Globals::Attributes::FeatureMap::Id, BITMAP32, 4, 0), /* feature map */
DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// Declare cluster commands
// TODO: It's not clear whether it would be better to get the command lists from the ZAP config on our last fixed endpoint instead.
// Note: we only implement "Identify", the only mandatory command when QRY feature is not present
constexpr CommandId identifyIncomingCommands[] = {
  Identify::Commands::Identify::Id,
  kInvalidCommandId,
};

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(identifiableDeviceClusters)
  DECLARE_DYNAMIC_CLUSTER(Identify::Id, identifyAttrs, identifyIncomingCommands, nullptr),
DECLARE_DYNAMIC_CLUSTER_LIST_END;


IdentifiableDevice::IdentifiableDevice(IdentifyDelegate& aIdentifyDelegate, DeviceInfoDelegate& aDeviceInfoDelegate) :
  Device(aDeviceInfoDelegate),
  mIdentifyDelegate(aIdentifyDelegate),
  mIdentifyTime(0)
{
  // - declare identify cluster
  addClusterDeclarations(Span<EmberAfCluster>(identifiableDeviceClusters));
}


IdentifiableDevice::~IdentifiableDevice()
{
}


bool IdentifiableDevice::updateIdentifyTime(uint16_t aIdentifyTime, UpdateMode aUpdateMode)
{
  if (aIdentifyTime!=mIdentifyTime || aUpdateMode.Has(UpdateFlags::forced)) {
    OLOG(LOG_INFO, "updating identifyTime to %hu - updatemode=0x%x", aIdentifyTime, aUpdateMode.Raw());
    mIdentifyTime = aIdentifyTime;
    if (aUpdateMode.Has(UpdateFlags::bridged)) {
      mIdentifyTickTimer.cancel();
      // <0 = stop, >0 = duration (duration==0 would mean default duration, not used here)
      mIdentifyDelegate.identify(mIdentifyTime<=0 ? -1 : mIdentifyTime);
      if (mIdentifyTime>0) {
        // start ticker
        identifyTick(mIdentifyTime);
      }
    }
    if (aUpdateMode.Has(UpdateFlags::matter)) {
      FOCUSOLOG("reporting IdentifyTime attribute change to matter");
      MatterReportingAttributeChangeCallback(GetEndpointId(), Identify::Id, Identify::Attributes::IdentifyTime::Id);
    }
    return true; // changed
  }
  return false; // unchanged
}


void IdentifiableDevice::identifyTick(uint16_t aRemainingSeconds)
{
  if (aRemainingSeconds<mIdentifyTime) {
    // update
    updateIdentifyTime(aRemainingSeconds, UpdateMode(UpdateFlags::matter));
  }
  if (mIdentifyTime>0) {
    mIdentifyTickTimer.executeOnce(boost::bind(&IdentifiableDevice::identifyTick, this, (uint16_t)(mIdentifyTime-1)), 1*Second);
  }
}



EmberAfStatus IdentifiableDevice::HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength)
{
  if (clusterId==Identify::Id) {
    if (attributeId == Identify::Attributes::IdentifyTime::Id) {
      return getAttr(buffer, maxReadLength, mIdentifyTime);
    }
    if (attributeId == Identify::Attributes::IdentifyType::Id) {
      return getAttr(buffer, maxReadLength, identifyType());
    }
    // common
    if (attributeId == Globals::Attributes::ClusterRevision::Id) {
      return getAttr<uint16_t>(buffer, maxReadLength, ZCL_IDENTIFY_CLUSTER_REVISION);
    }
    if (attributeId == Globals::Attributes::FeatureMap::Id) {
      return getAttr<uint32_t>(buffer, maxReadLength, ZCL_IDENTIFY_CLUSTER_FEATURE_MAP);
    }
  }
  // let base class try
  return inherited::HandleReadAttribute(clusterId, attributeId, buffer, maxReadLength);
}


EmberAfStatus IdentifiableDevice::HandleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer)
{
  if (clusterId==Identify::Id) {
    if (attributeId == Identify::Attributes::IdentifyTime::Id) {
      updateIdentifyTime(*((uint16_t*)buffer), UpdateMode(UpdateFlags::bridged));
      return EMBER_ZCL_STATUS_SUCCESS;
    }
  }
  // let base class try
  return inherited::HandleWriteAttribute(clusterId, attributeId, buffer);
}

// MARK: callbacks

void MatterIdentifyPluginServerInitCallback()
{
  /* NOP */
}


void emberAfIdentifyClusterServerInitCallback(EndpointId aEndpoint)
{
  /* NOP */
}


void MatterIdentifyClusterServerAttributeChangedCallback(const chip::app::ConcreteAttributePath & attributePath)
{
  /* NOP */
}


bool emberAfIdentifyClusterIdentifyCallback(
  CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
  const Commands::Identify::DecodableType & commandData
)
{
  auto dev = DeviceEndpoints::getDevice<IdentifiableDevice>(commandPath.mEndpointId);
  if (!dev) return false;
  dev->updateIdentifyTime(commandData.identifyTime, Device::UpdateMode(Device::UpdateFlags::bridged, Device::UpdateFlags::matter));
  commandObj->AddStatus(commandPath, Status::Success);
  return true;
}


bool emberAfIdentifyClusterTriggerEffectCallback(chip::app::CommandHandler*, chip::app::ConcreteCommandPath const&, chip::app::Clusters::Identify::Commands::TriggerEffect::DecodableType const&)
{
  // needs to be here because it is referenced from a dispatcher outside the cluster implementation
  return false; // we do not implement this
}


// MARK: - ComposedDevice

const EmberAfDeviceType gComposedDeviceTypes[] = {
  { DEVICE_TYPE_MA_BRIDGED_DEVICE, DEVICE_VERSION_DEFAULT }
};


string ComposedDevice::description()
{
  string s = inherited::description();
  string_format_append(s, "\n- Composed of %lu subdevices", subDevices().size());
  return s;
}


void ComposedDevice::addSubdevice(DevicePtr aSubDevice)
{
  subDevices().push_back(aSubDevice);
}


void ComposedDevice::finalizeDeviceDeclaration()
{
  finalizeDeviceDeclarationWithTypes(Span<const EmberAfDeviceType>(gComposedDeviceTypes));
}
