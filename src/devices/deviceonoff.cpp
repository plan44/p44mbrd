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
#define FOCUSLOGLEVEL 6

#include "device_impl.h" // include as first file!
#include "deviceonoff.h"

using namespace Clusters;

// MARK: - OnOff Device specific declarations

// REVISION DEFINITIONS:
// TODO: move these to a better place, probably into the devices that actually handle them, or
//   try to extract them from ZAP-generated defs
// =================================================================================

#define ZCL_ON_OFF_CLUSTER_REVISION (4u)

// MARK: onOff cluster declarations

// Declare cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(onOffAttrs)
  DECLARE_DYNAMIC_ATTRIBUTE(OnOff::Attributes::OnOff::Id, BOOLEAN, 1, 0), /* on/off */
  DECLARE_DYNAMIC_ATTRIBUTE(OnOff::Attributes::GlobalSceneControl::Id, BOOLEAN, 1, 0),
  DECLARE_DYNAMIC_ATTRIBUTE(OnOff::Attributes::OnTime::Id, INT16U, 2, ZAP_ATTRIBUTE_MASK(WRITABLE)),
  DECLARE_DYNAMIC_ATTRIBUTE(OnOff::Attributes::OffWaitTime::Id, INT16U, 2, ZAP_ATTRIBUTE_MASK(WRITABLE)),
  DECLARE_DYNAMIC_ATTRIBUTE(OnOff::Attributes::StartUpOnOff::Id, ENUM8, 1, ZAP_ATTRIBUTE_MASK(WRITABLE)),
  DECLARE_DYNAMIC_ATTRIBUTE(Globals::Attributes::FeatureMap::Id, BITMAP32, 4, 0), /* feature map */
DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// Declare cluster commands
// TODO: It's not clear whether it would be better to get the command lists from the ZAP config on our last fixed endpoint instead.
constexpr CommandId onOffIncomingCommands[] = {
  OnOff::Commands::Off::Id,
  OnOff::Commands::On::Id,
  OnOff::Commands::Toggle::Id,
  OnOff::Commands::OffWithEffect::Id,
  OnOff::Commands::OnWithRecallGlobalScene::Id,
  OnOff::Commands::OnWithTimedOff::Id,
  kInvalidCommandId,
};

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(onOffLightClusters)
  DECLARE_DYNAMIC_CLUSTER(OnOff::Id, onOffAttrs, onOffIncomingCommands, nullptr),
DECLARE_DYNAMIC_CLUSTER_LIST_END;


// MARK: - DeviceOnOff

DeviceOnOff::DeviceOnOff(bool aLighting, OnOffDelegate& aOnOffDelegate, IdentifyDelegate& aIdentifyDelegate, DeviceInfoDelegate& aDeviceInfoDelegate) :
  IdentifiableDevice(aIdentifyDelegate, aDeviceInfoDelegate),
  mOnOffDelegate(aOnOffDelegate),
  mLighting(aLighting),
  mOn(false),
  mGlobalSceneControl(false),
  mOnTime(0),
  mOffWaitTime(0),
  mStartUpOnOff(to_underlying(OnOff::OnOffStartUpOnOff::kOff))
{
  // - declare onoff device specific clusters
  addClusterDeclarations(Span<EmberAfCluster>(onOffLightClusters));
}


uint8_t DeviceOnOff::identifyType()
{
  // Lights identify via light, others somehow operate the actuator (blinds, clicking relay etc.)
  return to_underlying<Identify::IdentifyTypeEnum>(mLighting ? Identify::IdentifyTypeEnum::kLightOutput : Identify::IdentifyTypeEnum::kActuator);
}


void DeviceOnOff::changeOnOff_impl(bool aOn)
{
  mOnOffDelegate.setOnOffState(aOn);
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
      FOCUSOLOG("reporting onOff attribute change to matter");
      MatterReportingAttributeChangeCallback(GetEndpointId(), OnOff::Id, OnOff::Attributes::OnOff::Id);
    }
    return true; // changed
  }
  return false; // no change
}


// MARK: Attribute access

EmberAfStatus DeviceOnOff::HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength)
{
  if (clusterId==OnOff::Id) {
    if (attributeId == OnOff::Attributes::OnOff::Id) {
      return getAttr(buffer, maxReadLength, isOn());
    }
    if (attributeId == OnOff::Attributes::GlobalSceneControl::Id) {
      return getAttr(buffer, maxReadLength, mGlobalSceneControl);
    }
    if (attributeId == OnOff::Attributes::OnTime::Id) {
      return getAttr(buffer, maxReadLength, mOnTime);
    }
    if (attributeId == OnOff::Attributes::OffWaitTime::Id) {
      return getAttr(buffer, maxReadLength, mOffWaitTime);
    }
    if (attributeId == OnOff::Attributes::StartUpOnOff::Id) {
      return getAttr(buffer, maxReadLength, mStartUpOnOff);
    }
    // common attributes
    if (attributeId == Globals::Attributes::ClusterRevision::Id) {
      return getAttr<uint16_t>(buffer, maxReadLength, ZCL_ON_OFF_CLUSTER_REVISION);
    }
    if (attributeId == Globals::Attributes::FeatureMap::Id) {
      return getAttr<uint32_t>(buffer, maxReadLength, mLighting ? to_underlying(OnOff::Feature::kLighting) : 0);
    }
  }
  // let base class try
  return inherited::HandleReadAttribute(clusterId, attributeId, buffer, maxReadLength);
}



EmberAfStatus DeviceOnOff::HandleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer)
{
  if (clusterId==OnOff::Id) {
    // Non-writable from outside, but written by standard OnOff cluster implementation
    if (attributeId == OnOff::Attributes::OnOff::Id) {
      updateOnOff(*buffer, UpdateMode(UpdateFlags::bridged));
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if (attributeId == OnOff::Attributes::GlobalSceneControl::Id) {
      return setAttr(mGlobalSceneControl, buffer);
    }
    if (attributeId == OnOff::Attributes::OnTime::Id) {
      return setAttr(mOnTime, buffer);
    }
    if (attributeId == OnOff::Attributes::OffWaitTime::Id) {
      return setAttr(mOffWaitTime, buffer);
    }
    if (attributeId == OnOff::Attributes::StartUpOnOff::Id) {
      return setAttr(mStartUpOnOff, buffer);
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



