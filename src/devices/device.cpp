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
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 7

#include "device_impl.h" // include as first file!


using namespace Clusters;

// MARK: - bridged device common declarations

static EmberAfClusterSpec gBridgedDeviceCommonClusters[] = {
  { Descriptor::Id, CLUSTER_MASK_SERVER },
  { BridgedDeviceBasicInformation::Id, CLUSTER_MASK_SERVER },
};

// MARK: - Device

Device::Device(DeviceInfoDelegate& aDeviceInfoDelegate) :
  mDeviceInfoDelegate(aDeviceInfoDelegate),
  mPartOfComposedDevice(false),
  mReachable(false)
{
  // matter side init
  mEndpointId = kInvalidEndpointId;
  // - endpoint declaration info: must be reset to call emberAfSetupDynamicEndpointDeclaration on
  mEndpointDefinition.clusterCount = 0;
  mEndpointDefinition.cluster = nullptr;
  mEndpointDefinition.endpointSize = 0; // dynamic endpoints do not have any non-external attributes
  // - internal
  mClusterDataVersionsP = nullptr; // we'll need
  mParentEndpointId = kInvalidEndpointId;
  // - declare common bridged device clusters
  useClusterTemplates(Span<EmberAfClusterSpec>(gBridgedDeviceCommonClusters));
}


string Device::logContextPrefix()
{
  string ep;
  if (endpointId()!=kInvalidEndpointId) ep = string_format(" @endpoint %d", endpointId());
  string pep;
  if (mPartOfComposedDevice && GetParentEndpointId()!=kInvalidEndpointId) pep = string_format(" (part of @endpoint %d)", GetParentEndpointId());
  return string_format(
    "%s %sdevice '%s'%s%s",
    deviceType(),
    mPartOfComposedDevice ? "sub" : "",
    mDeviceInfoDelegate.name().c_str(), // bridge side name is available from start, but not node label (although the two might be in sync later)
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

void Device::useClusterTemplates(const Span<EmberAfClusterSpec>& aTemplateClusterSpecList)
{
  mTemplateClusterSpecSpanList.push_back(aTemplateClusterSpecList);
}


void Device::finalizeDeviceDeclarationWithTypes(const Span<const EmberAfDeviceType>& aDeviceTypeList)
{
  // save the device type list
  mDeviceTypeList = aDeviceTypeList;
  // now finally populate the endpoint definition
  // - generate the template clusterId list
  size_t i = 0;
  for (std::list<Span<EmberAfClusterSpec>>::iterator pos = mTemplateClusterSpecSpanList.begin(); pos!=mTemplateClusterSpecSpanList.end(); ++pos) {
    i += pos->size();
  }
  EmberAfClusterSpec *tl = new EmberAfClusterSpec[i];
  i = 0;
  for (std::list<Span<EmberAfClusterSpec>>::iterator pos = mTemplateClusterSpecSpanList.begin(); pos!=mTemplateClusterSpecSpanList.end(); ++pos) {
    for (size_t j=0; j<pos->size(); j++) {
      tl[i] = *(pos->data()+j);
      i++;
    }
  }
  // set up the endpoint declaration
  emberAfSetupDynamicEndpointDeclaration(
    mEndpointDefinition,
    static_cast<chip::EndpointId>(emberAfFixedEndpointCount()-1), // last fixed endpoint is the template endpoint
    Span<EmberAfClusterSpec>(tl, i)
  );
  mTemplateClusterSpecSpanList.clear(); // don't need this any more
  // - allocate the cluster data versions storage
  if (mClusterDataVersionsP) delete mClusterDataVersionsP;
  mClusterDataVersionsP = new DataVersion[mEndpointDefinition.clusterCount];
}


bool Device::addAsDeviceEndpoint()
{
  // finalize the declaration
  finalizeDeviceDeclaration();
  // allocate storage
  auto endpointStorage = Span<uint8_t>(new uint8_t[mEndpointDefinition.endpointSize], mEndpointDefinition.endpointSize);
  // add as dynamic endpoint
  CHIP_ERROR ret = emberAfSetDynamicEndpoint(
    mDynamicEndpointIdx,
    endpointId(),
    &mEndpointDefinition,
    Span<DataVersion>(mClusterDataVersionsP, mEndpointDefinition.clusterCount),
    mDeviceTypeList,
    mParentEndpointId,
    endpointStorage
  );
  if (ret==CHIP_NO_ERROR) {
    OLOG(LOG_INFO, "added at dynamic endpoint index #%d", mDynamicEndpointIdx);
  }
  else {
    OLOG(LOG_ERR, "emberAfSetDynamicEndpoint failed with CHIP_ERROR=%" CHIP_ERROR_FORMAT, ret.Format());
    return false;
  }
  return true;
}



void Device::willBeInstalled()
{
  OLOG(LOG_DEBUG, "will be installed");
}


void Device::didGetInstalled()
{
  mDeviceInfoDelegate.deviceDidGetInstalled();
  OLOG(LOG_DEBUG, "did get installed");
}



void Device::didBecomeOperational()
{
  OLOG(LOG_INFO,
    "did become operational:"
    "\n- (internal) UID: %s"
    "\n- NodeLabel: %s"
    "\n- VendorName: %s"
    "\n- ProductName: %s"
    "\n- SerialNumber: %s"
    "\n- ProductURL: %s",
    mDeviceInfoDelegate.endpointUID().c_str(),
    ATTR_STRING(BridgedDeviceBasicInformation, NodeLabel, endpointId()).c_str(),
    ATTR_STRING(BridgedDeviceBasicInformation, VendorName, endpointId()).c_str(),
    ATTR_STRING(BridgedDeviceBasicInformation, ProductName, endpointId()).c_str(),
    ATTR_STRING(BridgedDeviceBasicInformation, SerialNumber, endpointId()).c_str(),
    ATTR_STRING(BridgedDeviceBasicInformation, ProductURL, endpointId()).c_str()
  );
}


void Device::willBeDisabled()
{
}


// MARK: functionality

void Device::updateReachable(bool aReachable, UpdateMode aUpdateMode)
{
  if (mReachable!=aReachable || aUpdateMode.Has(UpdateFlags::forced)) {
    mReachable = aReachable;
    OLOG(LOG_INFO, "Updating reachable to %s - updatemode=%d", mReachable ? "REACHABLE" : "OFFLINE", aUpdateMode.Raw());
    if (aUpdateMode.Has(UpdateFlags::matter)) {
      reportAttributeChange(BridgedDeviceBasicInformation::Id, BridgedDeviceBasicInformation::Attributes::Reachable::Id);
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
      reportAttributeChange(BridgedDeviceBasicInformation::Id, BridgedDeviceBasicInformation::Attributes::NodeLabel::Id);
    }
  }
}

// MARK: Attribute access

Status Device::handleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength)
{
  if (clusterId==BasicInformation::Id) {
    OLOG(LOG_WARNING, "****** tried to access basic infomation cluster *****");
  }
  else if (clusterId==BridgedDeviceBasicInformation::Id) {
    // Reachable flag
    if (attributeId == BridgedDeviceBasicInformation::Attributes::Reachable::Id) {
      return getAttr(buffer, maxReadLength, mDeviceInfoDelegate.isReachable());
    }
    // Writable Node Label
    if (attributeId == BridgedDeviceBasicInformation::Attributes::NodeLabel::Id) {
      FOCUSOLOG("reading node label: %s", mNodeLabel.c_str());
      MutableByteSpan zclNameSpan(buffer, maxReadLength);
      MakeZclCharString(zclNameSpan, mNodeLabel.substr(0,maxReadLength-1).c_str());
      return Status::Success;
    }
  }
  return Status::Failure;
}


Status Device::handleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer)
{
  // handle common device attributes
  if (clusterId==BridgedDeviceBasicInformation::Id) {
    // Writable Node Label
    if (attributeId == BridgedDeviceBasicInformation::Attributes::NodeLabel::Id) {
      string newName((const char*)buffer+1, (size_t)buffer[0]);
      FOCUSOLOG("writing nodel label: new label = '%s'", newName.c_str());
      updateNodeLabel(newName, UpdateMode(UpdateFlags::bridged, UpdateFlags::matter));
      return Status::Success;
    }
  }
  return Status::Failure;
}



void Device::handleAttributeChange(ClusterId clusterId, chip::AttributeId attributeId)
{
  /* NOP in base class */
}


void Device::reportAttributeChange(ClusterId aClusterId, chip::AttributeId aAttributeId)
{
  MatterReportingAttributeChangeCallback(endpointId(), aClusterId, aAttributeId);
}



string Device::description()
{
  return string_format("device status:\n- reachable: %d", mReachable);
}


// MARK: - IdentifiableDevice

using namespace Identify;

static EmberAfClusterSpec gIdentifiableDeviceClusters[] = { { Identify::Id, CLUSTER_MASK_SERVER } };

IdentifiableDevice::IdentifiableDevice(IdentifyDelegate& aIdentifyDelegate, DeviceInfoDelegate& aDeviceInfoDelegate) :
  Device(aDeviceInfoDelegate),
  mIdentifyDelegate(aIdentifyDelegate),
  mIdentifyTime(0)
{
  // - declare identify cluster
  useClusterTemplates(Span<EmberAfClusterSpec>(gIdentifiableDeviceClusters));
}


IdentifiableDevice::~IdentifiableDevice()
{
}


void IdentifiableDevice::didGetInstalled()
{
  // override static attribute defaults
  Identify::Attributes::IdentifyType::Set(endpointId(), mIdentifyDelegate.identifyType());
  // call base class last
  inherited::didGetInstalled();
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
      reportAttributeChange(Identify::Id, Identify::Attributes::IdentifyTime::Id);
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



Status IdentifiableDevice::handleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength)
{
  if (clusterId==Identify::Id) {
    if (attributeId == Identify::Attributes::IdentifyTime::Id) {
      return getAttr(buffer, maxReadLength, mIdentifyTime);
    }
  }
  // let base class try
  return inherited::handleReadAttribute(clusterId, attributeId, buffer, maxReadLength);
}


Status IdentifiableDevice::handleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer)
{
  if (clusterId==Identify::Id) {
    if (attributeId == Identify::Attributes::IdentifyTime::Id) {
      updateIdentifyTime(*((uint16_t*)buffer), UpdateMode(UpdateFlags::bridged));
      return Status::Success;
    }
  }
  // let base class try
  return inherited::handleWriteAttribute(clusterId, attributeId, buffer);
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

static const EmberAfDeviceType gComposedDeviceTypes[] = {
  { DEVICE_TYPE_MA_BRIDGED_DEVICE, DEVICE_VERSION_DEFAULT },
  { DEVICE_TYPE_MA_AGGREGATOR, DEVICE_VERSION_DEFAULT }
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
