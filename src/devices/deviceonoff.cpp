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
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 7

#include "device_impl.h" // include as first file!
#include "deviceonoff.h"

using namespace Clusters;

// MARK: - OnOff Device specific declarations


static EmberAfClusterSpec gOnOffDeviceClusters[] = {
  { OnOff::Id, CLUSTER_MASK_SERVER },
  { Groups::Id, CLUSTER_MASK_SERVER }
#ifdef MATTER_DM_PLUGIN_SCENES
  , { Scenes::Id, CLUSTER_MASK_SERVER }
#endif
};


// MARK: - DeviceOnOff

DeviceOnOff::DeviceOnOff(bool aLighting, OnOffDelegate& aOnOffDelegate, IdentifyDelegate* aIdentifyDelegateP, DeviceInfoDelegate& aDeviceInfoDelegate) :
  inherited(aIdentifyDelegateP, aDeviceInfoDelegate),
  mOnOffDelegate(aOnOffDelegate),
  mLighting(aLighting),
  mOn(false)
{
  // - declare onoff device specific clusters
  useClusterTemplates(Span<EmberAfClusterSpec>(gOnOffDeviceClusters));
}


void DeviceOnOff::didGetInstalled()
{
  OnOff::Attributes::FeatureMap::Set(endpointId(), mLighting ? to_underlying(OnOff::Feature::kLighting) : 0);
  // call base class last
  inherited::didGetInstalled();
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
      reportAttributeChange(OnOff::Id, OnOff::Attributes::OnOff::Id);
    }
    return true; // changed
  }
  return false; // no change
}


// MARK: Attribute access

Status DeviceOnOff::handleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength)
{
  if (clusterId==OnOff::Id) {
    if (attributeId == OnOff::Attributes::OnOff::Id) {
      return getAttr(buffer, maxReadLength, isOn());
    }
  }
  // let base class try
  return inherited::handleReadAttribute(clusterId, attributeId, buffer, maxReadLength);
}



Status DeviceOnOff::handleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer)
{
  if (clusterId==OnOff::Id) {
    // Non-writable from outside, but written by standard OnOff cluster implementation
    if (attributeId == OnOff::Attributes::OnOff::Id) {
      updateOnOff(*buffer, UpdateMode(UpdateFlags::bridged));
      return Status::Success;
    }
  }
  // let base class try
  return inherited::handleWriteAttribute(clusterId, attributeId, buffer);
}


string DeviceOnOff::description()
{
  string s = inherited::description();
  string_format_append(s, "\n- OnOff: %d", mOn);
  return s;
}


// MARK: - DeviceOnOffLight

static const EmberAfDeviceType gOnOffLightTypes[] = {
  { DEVICE_TYPE_MA_ON_OFF_LIGHT, DEVICE_VERSION_DEFAULT }
};

bool DeviceOnOffLight::finalizeDeviceDeclaration()
{
  return finalizeDeviceDeclarationWithTypes(Span<const EmberAfDeviceType>(gOnOffLightTypes));
}


// MARK: - DeviceOnOffPluginUnit

static const EmberAfDeviceType gOnOffPluginTypes[] = {
  { DEVICE_TYPE_MA_ON_OFF_PLUGIN_UNIT, DEVICE_VERSION_DEFAULT }
};

bool DeviceOnOffPluginUnit::finalizeDeviceDeclaration()
{
  return finalizeDeviceDeclarationWithTypes(Span<const EmberAfDeviceType>(gOnOffPluginTypes));
}



