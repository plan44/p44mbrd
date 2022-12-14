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

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 1
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 5

#include "device_impl.h" // include as first file!
#include "deviceonoff.h"

// MARK: - OnOff Device specific declarations

// REVISION DEFINITIONS:
// TODO: move these to a better place, probably into the devices that actually handle them, or
//   try to extract them from ZAP-generated defs
// =================================================================================

#define ZCL_ON_OFF_CLUSTER_REVISION (4u)

// MARK: onOff cluster declarations

// Declare cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(onOffAttrs)
  DECLARE_DYNAMIC_ATTRIBUTE(ZCL_ON_OFF_ATTRIBUTE_ID, BOOLEAN, 1, 0), /* on/off */
  DECLARE_DYNAMIC_ATTRIBUTE(ZCL_FEATURE_MAP_SERVER_ATTRIBUTE_ID, BITMAP32, 4, 0), /* feature map */
DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// Declare cluster commands
// TODO: It's not clear whether it would be better to get the command lists from the ZAP config on our last fixed endpoint instead.
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


// MARK: - DeviceOnOff

DeviceOnOff::DeviceOnOff(bool aLighting) :
  mLighting(aLighting),
  mOn(false)
{
  // - declare onoff device specific clusters
  addClusterDeclarations(Span<EmberAfCluster>(onOffLightClusters));
}


uint8_t DeviceOnOff::identifyType()
{
  // Lights identify via light, others somehow operate the actuator (blinds, clicking relay etc.)
  return mLighting ? EMBER_ZCL_IDENTIFY_IDENTIFY_TYPE_VISIBLE_LIGHT : EMBER_ZCL_IDENTIFY_IDENTIFY_TYPE_ACTUATOR;
}



void DeviceOnOff::initBridgedInfo(JsonObjectPtr aDeviceInfo, JsonObjectPtr aDeviceComponentInfo, const char* aInputType, const char* aInputId)
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
  if (aChannelStates->get(mDefaultChannelId.c_str(), o)) {
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
    OLOG(LOG_INFO, "updating onOff to %s - updatemode=0x%x", aOn ? "ON" : "OFF", aUpdateMode.Raw());
    mOn = aOn;
    if (aUpdateMode.Has(UpdateFlags::bridged)) {
      changeOnOff_impl(mOn);
    }
    if (aUpdateMode.Has(UpdateFlags::matter)) {
      MatterReportingAttributeChangeCallback(GetEndpointId(), ZCL_ON_OFF_CLUSTER_ID, ZCL_ON_OFF_ATTRIBUTE_ID);
    }
    return true; // changed
  }
  return false; // no change
}


// MARK: Utilities for derived classes

bool DeviceOnOff::shouldExecuteWithFlag(bool aWithOnOff, uint8_t aOptionMask, uint8_t aOptionOverride, uint8_t aOptionsAttribute, uint8_t aExecuteIfOffFlag)
{
  // From 3.10.2.2.8.1 of ZCL7 document 14-0127-20j-zcl-ch-3-general.docx:
  //   "Command execution SHALL NOT continue beyond the Options processing if
  //    all of these criteria are true:
  //      - The command is one of the ???without On/Off??? commands: Move, Move to
  //        Level, Stop, or Step.
  //      - The On/Off cluster exists on the same endpoint as this cluster.
  //      - The OnOff attribute of the On/Off cluster, on this endpoint, is 0x00
  //        (FALSE).
  //      - The value of the ExecuteIfOff bit is 0."
  if (aWithOnOff) {
    // command includes On/Off -> always execute
    return true;
  }
  if (isOn()) {
    // is already on -> execute anyway
    return true;
  }
  // now the options bit decides about executing or not
  return (aOptionsAttribute & (uint8_t)(~aOptionMask)) | (aOptionOverride & aOptionMask);
}


// MARK: Attribute access

EmberAfStatus DeviceOnOff::HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength)
{
  if (clusterId==ZCL_ON_OFF_CLUSTER_ID) {
    if (attributeId == ZCL_ON_OFF_ATTRIBUTE_ID) {
      return getAttr(buffer, maxReadLength, isOn());
    }
    // common attributes
    if (attributeId == ZCL_CLUSTER_REVISION_SERVER_ATTRIBUTE_ID) {
      return getAttr<uint16_t>(buffer, maxReadLength, ZCL_ON_OFF_CLUSTER_REVISION);
    }
    if (attributeId == ZCL_FEATURE_MAP_SERVER_ATTRIBUTE_ID) {
      return getAttr<uint32_t>(buffer, maxReadLength, mLighting ? EMBER_AF_ON_OFF_FEATURE_LIGHTING : 0);
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


string DeviceOnOff::description()
{
  string s = inherited::description();
  string_format_append(s, "\n- OnOff: %d", mOn);
  return s;
}


// MARK: - DeviceOnOffLight

const EmberAfDeviceType gOnOffLightTypes[] = {
  { DEVICE_TYPE_MA_ON_OFF_LIGHT, DEVICE_VERSION_DEFAULT },
  { DEVICE_TYPE_MA_BRIDGED_DEVICE, DEVICE_VERSION_DEFAULT }
};

void DeviceOnOffLight::finalizeDeviceDeclaration()
{
  finalizeDeviceDeclarationWithTypes(Span<const EmberAfDeviceType>(gOnOffLightTypes));
}


// MARK: - DeviceOnOffPluginUnit

const EmberAfDeviceType gOnOffPluginTypes[] = {
  { DEVICE_TYPE_MA_ON_OFF_PLUGIN_UNIT, DEVICE_VERSION_DEFAULT },
  { DEVICE_TYPE_MA_BRIDGED_DEVICE, DEVICE_VERSION_DEFAULT }
};

void DeviceOnOffPluginUnit::finalizeDeviceDeclaration()
{
  finalizeDeviceDeclarationWithTypes(Span<const EmberAfDeviceType>(gOnOffPluginTypes));
}



