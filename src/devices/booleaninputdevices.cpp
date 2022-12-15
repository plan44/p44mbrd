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
#include "booleaninputdevices.h"

#include <math.h>

// MARK: - BooleanState specific declarations

// REVISION DEFINITIONS:
// TODO: move these to a better place, probably into the devices that actually handle them, or
//   try to extract them from ZAP-generated defs
// =================================================================================

#define ZCL_BOOLEAN_STATE_CLUSTER_REVISION (1u)

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(booleanStateAttrs)
  DECLARE_DYNAMIC_ATTRIBUTE(ZCL_STATE_VALUE_ATTRIBUTE_ID, BOOLEAN, 1, 0),
DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(booleanStateClusters)
  DECLARE_DYNAMIC_CLUSTER(ZCL_BOOLEAN_STATE_CLUSTER_ID, booleanStateAttrs, nullptr, nullptr),
DECLARE_DYNAMIC_CLUSTER_LIST_END;


// MARK: - BooleanInputDevice

BooleanInputDevice::BooleanInputDevice()
{
  mState = false;
}


void BooleanInputDevice::initBridgedInfo(JsonObjectPtr aDeviceInfo, JsonObjectPtr aDeviceComponentInfo, const char* aInputType, const char* aInputId)
{
  inherited::initBridgedInfo(aDeviceInfo, aDeviceComponentInfo, aInputType, aInputId);
  // get current value from xxxStates
  parseInputValue(aDeviceInfo, UpdateMode());
}


void BooleanInputDevice::handleBridgePushProperties(JsonObjectPtr aChangedProperties)
{
  inherited::handleBridgePushProperties(aChangedProperties);
  parseInputValue(aChangedProperties, UpdateMode(UpdateFlags::matter));
}


string BooleanInputDevice::description()
{
  string s = inherited::description();
  string_format_append(s, "\n- Boolean State: %s", mState ? "true" : "false");
  return s;
}


void BooleanInputDevice::parseInputValue(JsonObjectPtr aProperties, UpdateMode aUpdateMode)
{
  JsonObjectPtr states;
  if (aProperties->get((mInputType+"States").c_str(), states)) {
    JsonObjectPtr state;
    if (states->get(mInputId.c_str(), state)) {
      JsonObjectPtr o;
      if (state->get("value", o, true)) {
        // non-NULL value
        mState = o->boolValue();
      }
      else {
        // NULL value or no value contained in state at all
        NumericAttributeTraits<bool>::SetNull(mState);
      }
      if (aUpdateMode.Has(UpdateFlags::matter)) {
        MatterReportingAttributeChangeCallback(GetEndpointId(), ZCL_BOOLEAN_STATE_CLUSTER_ID, ZCL_STATE_VALUE_ATTRIBUTE_ID);
      }
    }
  }
}


EmberAfStatus BooleanInputDevice::HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength)
{
  if (clusterId==ZCL_BOOLEAN_STATE_CLUSTER_ID) {
    if (attributeId == ZCL_STATE_VALUE_ATTRIBUTE_ID) {
      return getAttr(buffer, maxReadLength, mState);
    }
    // common attributes
    if (attributeId == ZCL_CLUSTER_REVISION_SERVER_ATTRIBUTE_ID) {
      return getAttr(buffer, maxReadLength, ZCL_BOOLEAN_STATE_CLUSTER_REVISION);
    }
  }
  // let base class try
  return inherited::HandleReadAttribute(clusterId, attributeId, buffer, maxReadLength);
}


// MARK: - ContactSensorDevice

const EmberAfDeviceType gContactSensorTypes[] = {
  { DEVICE_TYPE_MA_CONTACT_SENSOR, DEVICE_VERSION_DEFAULT },
  { DEVICE_TYPE_MA_BRIDGED_DEVICE, DEVICE_VERSION_DEFAULT }
};

ContactSensorDevice::ContactSensorDevice()
{
  // - declare device specific clusters
  addClusterDeclarations(Span<EmberAfCluster>(booleanStateClusters));
}

void ContactSensorDevice::finalizeDeviceDeclaration()
{
  finalizeDeviceDeclarationWithTypes(Span<const EmberAfDeviceType>(gContactSensorTypes));
}
