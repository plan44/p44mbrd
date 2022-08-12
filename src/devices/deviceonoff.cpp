//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2022 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "deviceonoff.h"
#include "device_impl.h"

// MARK: - OnOff Device specific declarations

// REVISION DEFINITIONS:
// TODO: move these to a better place, probably into the devices that actually handle them, or
//   try to extract them from ZAP-generated defs
// =================================================================================

#define ZCL_ON_OFF_CLUSTER_REVISION (4u)
#define ZCL_ON_OFF_CLUSTER_FEATURE_MAP (EMBER_AF_ON_OFF_FEATURE_LIGHTING)

// MARK: onOff cluster declarations

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(onOffAttrs)
  DECLARE_DYNAMIC_ATTRIBUTE(ZCL_ON_OFF_ATTRIBUTE_ID, BOOLEAN, 1, 0), /* on/off */
  DECLARE_DYNAMIC_ATTRIBUTE(ZCL_FEATURE_MAP_SERVER_ATTRIBUTE_ID, BITMAP32, 4, 0), /* feature map */
DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// Declare Cluster List for Bridged Light endpoint
// TODO: It's not clear whether it would be better to get the command lists from
// the ZAP config on our last fixed endpoint instead.
constexpr CommandId onOffIncomingCommands[] = {
  app::Clusters::OnOff::Commands::Off::Id,
  app::Clusters::OnOff::Commands::On::Id,
  app::Clusters::OnOff::Commands::Toggle::Id,
  app::Clusters::OnOff::Commands::OffWithEffect::Id,
  app::Clusters::OnOff::Commands::OnWithRecallGlobalScene::Id,
  app::Clusters::OnOff::Commands::OnWithTimedOff::Id,
  kInvalidCommandId,
};

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(onOffLightClusters)
  DECLARE_DYNAMIC_CLUSTER(ZCL_ON_OFF_CLUSTER_ID, onOffAttrs, onOffIncomingCommands, nullptr),
DECLARE_DYNAMIC_CLUSTER_LIST_END;

const EmberAfDeviceType gOnOffLightTypes[] = {
  { DEVICE_TYPE_LO_ON_OFF_LIGHT, DEVICE_VERSION_DEFAULT },
  { DEVICE_TYPE_BRIDGED_NODE, DEVICE_VERSION_DEFAULT }
};


// MARK: - DeviceOnOff

DeviceOnOff::DeviceOnOff() :
  mOn(false)
{
  // - declare onoff device specific clusters
  addClusterDeclarations(Span<EmberAfCluster>(onOffLightClusters));
}


void DeviceOnOff::finalizeDeviceDeclaration()
{
  finalizeDeviceDeclarationWithTypes(Span<const EmberAfDeviceType>(gOnOffLightTypes));
}


void DeviceOnOff::initBridgedInfo(JsonObjectPtr aDeviceInfo)
{
  inherited::initBridgedInfo(aDeviceInfo);
  // output devices should examine the channel states
  JsonObjectPtr o = aDeviceInfo->get("channelStates");
  if (o) {
    parseChannelStates(o, UpdateMode());
  }
}


void DeviceOnOff::handleBridgePushProperties(JsonObjectPtr aChangedProperties)
{
  inherited::handleBridgePushProperties(aChangedProperties);
  JsonObjectPtr channelStates;
  if (aChangedProperties->get("channelStates", channelStates, true)) {
    parseChannelStates(channelStates, UpdateMode(UpdateFlags::matter));
  }
}



void DeviceOnOff::parseChannelStates(JsonObjectPtr aChannelStates, UpdateMode aUpdateMode)
{
  JsonObjectPtr o;
  if (aChannelStates->get("brightness", o)) {
    JsonObjectPtr vo;
    if (o->get("value", vo, true)) {
      updateOnOff(vo->doubleValue()>0, aUpdateMode);
    }
  }
}


void DeviceOnOff::changeOnOff_impl(bool aOn)
{
  // call preset1 or off on the bridged device
  JsonObjectPtr params = JsonObject::newObj();
  params->add("channel", JsonObject::newInt32(0)); // default channel
  params->add("value", JsonObject::newDouble(aOn ? 100 : 0));
  params->add("transitionTime", JsonObject::newDouble(0));
  params->add("apply_now", JsonObject::newBool(true));
  notify("setOutputChannelValue", params);
}


bool DeviceOnOff::updateOnOff(bool aOn, UpdateMode aUpdateMode)
{
  if (aOn!=mOn || aUpdateMode.Has(UpdateFlags::forced)) {
    ChipLogProgress(DeviceLayer, "p44 Device[%s]: updating onOff to %s - updatemode=%d", GetName().c_str(), aOn ? "ON" : "OFF", aUpdateMode.Raw());
    mOn  = aOn;
    if (aUpdateMode.Has(UpdateFlags::bridged)) {
      changeOnOff_impl(mOn);
    }
    if (aUpdateMode.Has(UpdateFlags::matter)) {
      MatterReportingAttributeChangeCallback(GetEndpointId(), ZCL_BRIDGED_DEVICE_BASIC_CLUSTER_ID, ZCL_NODE_LABEL_ATTRIBUTE_ID);
    }
    return true; // changed
  }
  return false; // no change
}


// MARK: Attribute access

EmberAfStatus DeviceOnOff::HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength)
{
  if (clusterId==ZCL_ON_OFF_CLUSTER_ID) {
    if ((attributeId == ZCL_ON_OFF_ATTRIBUTE_ID) && (maxReadLength == 1)) {
      *buffer = isOn() ? 1 : 0;
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    // common attributes
    if ((attributeId == ZCL_CLUSTER_REVISION_SERVER_ATTRIBUTE_ID) && (maxReadLength == 2)) {
      *((uint16_t*)buffer) = (uint16_t) ZCL_ON_OFF_CLUSTER_REVISION;
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if ((attributeId == ZCL_FEATURE_MAP_SERVER_ATTRIBUTE_ID) && (maxReadLength == 4)) {
      *((uint32_t*)buffer) = (uint32_t) ZCL_ON_OFF_CLUSTER_FEATURE_MAP;
      return EMBER_ZCL_STATUS_SUCCESS;
    }
  }
  // let base class try
  return inherited::HandleReadAttribute(clusterId, attributeId, buffer, maxReadLength);
}



EmberAfStatus DeviceOnOff::HandleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer)
{
  if (clusterId==ZCL_ON_OFF_CLUSTER_ID) {
    // Non-writable from outside, but written by standard OnOff cluster implementation  
    if (attributeId == ZCL_ON_OFF_ATTRIBUTE_ID) {
      updateOnOff(*buffer, UpdateMode(UpdateFlags::bridged));
      return EMBER_ZCL_STATUS_SUCCESS;
    }
  }
  // let base class try
  return inherited::HandleWriteAttribute(clusterId, attributeId, buffer);
}


void DeviceOnOff::logStatus(const char *aReason)
{
  inherited::logStatus(aReason);
  ChipLogDetail(DeviceLayer, "- OnOff: %d", mOn);
}
