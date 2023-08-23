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
#include "booleaninputdevices.h"

#include <math.h>

using namespace Clusters;

// MARK: - BinaryInputDevice

BinaryInputDevice::BinaryInputDevice(DeviceInfoDelegate& aDeviceInfoDelegate) :
  inherited(aDeviceInfoDelegate)
{
}


string BinaryInputDevice::description()
{
  string s = inherited::description();
  string_format_append(s, "\n- Boolean State");
  return s;
}


void BinaryInputDevice::updateCurrentState(bool aState, bool aIsValid, UpdateMode aUpdateMode)
{
  if (aIsValid) {
    BooleanState::Attributes::StateValue::Set(endpointId(), aState);
    if (aUpdateMode.Has(UpdateFlags::matter)) {
      reportAttributeChange(BooleanState::Id, BooleanState::Attributes::StateValue::Id);
    }
  }
}


// MARK: - BoolanStateDevice

ClusterId booleanStateClusters[] = { BooleanState::Id };

BoolanStateDevice::BoolanStateDevice(DeviceInfoDelegate& aDeviceInfoDelegate) :
  inherited(aDeviceInfoDelegate)
{
  // - declare device specific clusters
  useClusterTemplates(Span<ClusterId>(booleanStateClusters));
}




// MARK: - ContactSensorDevice

const EmberAfDeviceType gContactSensorTypes[] = {
  { DEVICE_TYPE_MA_CONTACT_SENSOR, DEVICE_VERSION_DEFAULT },
  { DEVICE_TYPE_MA_BRIDGED_DEVICE, DEVICE_VERSION_DEFAULT }
};

ContactSensorDevice::ContactSensorDevice(DeviceInfoDelegate& aDeviceInfoDelegate) :
  inherited(aDeviceInfoDelegate)
{
}

void ContactSensorDevice::finalizeDeviceDeclaration()
{
  finalizeDeviceDeclarationWithTypes(Span<const EmberAfDeviceType>(gContactSensorTypes));
}


// MARK: - OccupancySensingDevice

ClusterId occupancySensingClusters[] = { OccupancySensing::Id };

const EmberAfDeviceType gOccupancySensingTypes[] = {
  { DEVICE_TYPE_MA_OCCUPANCY_SENSOR, DEVICE_VERSION_DEFAULT },
  { DEVICE_TYPE_MA_BRIDGED_DEVICE, DEVICE_VERSION_DEFAULT }
};


OccupancySensingDevice::OccupancySensingDevice(DeviceInfoDelegate& aDeviceInfoDelegate) :
  inherited(aDeviceInfoDelegate)
{
  // - declare device specific clusters
  useClusterTemplates(Span<ClusterId>(occupancySensingClusters));
}


void OccupancySensingDevice::finalizeDeviceDeclaration()
{
  finalizeDeviceDeclarationWithTypes(Span<const EmberAfDeviceType>(gOccupancySensingTypes));
}


void OccupancySensingDevice::didGetInstalled()
{
  // override static attribute defaults
  // Note: actual device implementation might override these if it has better information
  //   about the type of sensor
  using namespace OccupancySensing;
  Attributes::OccupancySensorType::Set(endpointId(), OccupancySensorTypeEnum::kPir);
  Attributes::OccupancySensorTypeBitmap::Set(endpointId(), BitMask<OccupancySensorTypeBitmap>(OccupancySensorTypeBitmap::kPir));
  // call base class last (which will call implementation delegate, which then can override the defaults above)
  inherited::didGetInstalled();
}



void OccupancySensingDevice::updateCurrentState(bool aState, bool aIsValid, UpdateMode aUpdateMode)
{
  using namespace OccupancySensing;
  if (aIsValid) {
    BitMask<OccupancyBitmap> b;
    if (aState) b.Set(OccupancyBitmap::kOccupied);
    Attributes::Occupancy::Set(endpointId(), b);
    if (aUpdateMode.Has(UpdateFlags::matter)) {
      reportAttributeChange(BooleanState::Id, BooleanState::Attributes::StateValue::Id);
    }
  }
}
