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
#include "sensordevices.h"

#include <math.h>

using namespace Clusters;

// MARK: - Temperature Sensor Device

const EmberAfDeviceType gTemperatureSensorTypes[] = {
  { DEVICE_TYPE_MA_TEMP_SENSOR, DEVICE_VERSION_DEFAULT },
  { DEVICE_TYPE_MA_BRIDGED_DEVICE, DEVICE_VERSION_DEFAULT }
};

ClusterId temperatureSensorClusters[] = { TemperatureMeasurement::Id };


DeviceTemperature::DeviceTemperature(DeviceInfoDelegate& aDeviceInfoDelegate) :
  inherited(aDeviceInfoDelegate)
{
  // - declare device specific clusters
  useClusterTemplates(Span<ClusterId>(temperatureSensorClusters));
}


void DeviceTemperature::finalizeDeviceDeclaration()
{
  finalizeDeviceDeclarationWithTypes(Span<const EmberAfDeviceType>(gTemperatureSensorTypes));
}


int16_t DeviceTemperature::matterValue(double aBridgeValue, bool aIsValid)
{
  // matter unit is 1/100th degree celsius
  int16_t v;
  if (aIsValid) {
    v = static_cast<int16_t>(aBridgeValue*100.0+0.5);
  }
  else {
    NumericAttributeTraits<int16_t>::SetNull(v);
  }
  return v;
}


void DeviceTemperature::setupSensorParams(bool aHasMin, double aMin, bool aHasMax, double aMax, double aTolerance)
{
  TemperatureMeasurement::Attributes::MinMeasuredValue::Set(endpointId(), matterValue(aMin, aHasMin));
  TemperatureMeasurement::Attributes::MaxMeasuredValue::Set(endpointId(), matterValue(aMax, aHasMax));
  TemperatureMeasurement::Attributes::Tolerance::Set(endpointId(), static_cast<uint16_t>(matterValue(aTolerance)));
}


void DeviceTemperature::updateMeasuredValue(double aMeasuredValue, bool aIsValid, UpdateMode aUpdateMode)
{
  int16_t v = matterValue(aMeasuredValue);
  if (!aIsValid) NumericAttributeTraits<int16_t>::SetNull(v);
  TemperatureMeasurement::Attributes::MeasuredValue::Set(endpointId(), v);
  if (aUpdateMode.Has(UpdateFlags::matter)) {
    MatterReportingAttributeChangeCallback(endpointId(), TemperatureMeasurement::Id, TemperatureMeasurement::Attributes::MeasuredValue::Id);
  }
}



// MARK: - Illumination Sensor Device

const EmberAfDeviceType gIlluminanceSensorTypes[] = {
  { DEVICE_TYPE_MA_ILLUM_SENSOR, DEVICE_VERSION_DEFAULT },
  { DEVICE_TYPE_MA_BRIDGED_DEVICE, DEVICE_VERSION_DEFAULT }
};

ClusterId illuminanceSensorClusters[] = { IlluminanceMeasurement::Id };


DeviceIlluminance::DeviceIlluminance(DeviceInfoDelegate& aDeviceInfoDelegate) :
  inherited(aDeviceInfoDelegate)
{
  // - declare device specific clusters
  useClusterTemplates(Span<ClusterId>(illuminanceSensorClusters));
}


void DeviceIlluminance::finalizeDeviceDeclaration()
{
  finalizeDeviceDeclarationWithTypes(Span<const EmberAfDeviceType>(gIlluminanceSensorTypes));
}


uint16_t DeviceIlluminance::matterValue(double aBridgeValue, bool aIsValid)
{
  // matter unit is 10000*log10(lux)+1
  uint16_t v;
  if (aIsValid) {
    v = static_cast<uint16_t>(10000.0*log10(aBridgeValue)+1);
  }
  else {
    NumericAttributeTraits<uint16_t>::SetNull(v);
  }
  return v;
}


void DeviceIlluminance::setupSensorParams(bool aHasMin, double aMin, bool aHasMax, double aMax, double aTolerance)
{
  IlluminanceMeasurement::Attributes::MinMeasuredValue::Set(endpointId(), matterValue(aMin, aHasMin));
  IlluminanceMeasurement::Attributes::MaxMeasuredValue::Set(endpointId(), matterValue(aMax, aHasMax));
  IlluminanceMeasurement::Attributes::Tolerance::Set(endpointId(), static_cast<uint16_t>(matterValue(aTolerance)));
}


void DeviceIlluminance::updateMeasuredValue(double aMeasuredValue, bool aIsValid, UpdateMode aUpdateMode)
{
  uint16_t v = matterValue(aMeasuredValue, aIsValid);
  IlluminanceMeasurement::Attributes::MeasuredValue::Set(endpointId(), v);
  if (aUpdateMode.Has(UpdateFlags::matter)) {
    MatterReportingAttributeChangeCallback(endpointId(), IlluminanceMeasurement::Id, IlluminanceMeasurement::Attributes::MeasuredValue::Id);
  }
}


// MARK: - Humidity Sensor Device

const EmberAfDeviceType gRelativeHumiditySensorTypes[] = {
  { DEVICE_TYPE_MA_RELATIVE_HUMIDITY_SENSOR, DEVICE_VERSION_DEFAULT },
  { DEVICE_TYPE_MA_BRIDGED_DEVICE, DEVICE_VERSION_DEFAULT }
};

ClusterId relativeHumiditySensorClusters[] = { RelativeHumidityMeasurement::Id };


DeviceHumidity::DeviceHumidity(DeviceInfoDelegate& aDeviceInfoDelegate) :
  inherited(aDeviceInfoDelegate)
{
  // - declare device specific clusters
  useClusterTemplates(Span<ClusterId>(relativeHumiditySensorClusters));
}


void DeviceHumidity::finalizeDeviceDeclaration()
{
  finalizeDeviceDeclarationWithTypes(Span<const EmberAfDeviceType>(gRelativeHumiditySensorTypes));
}


uint16_t DeviceHumidity::matterValue(double aBridgeValue, bool aIsValid)
{
  // matter unit is 100 * humidity percentage
  uint16_t v;
  if (aIsValid) {
    v = static_cast<uint16_t>(100.0*aBridgeValue+0.5);
  }
  else {
    NumericAttributeTraits<uint16_t>::SetNull(v);
  }
  return v;
}


void DeviceHumidity::setupSensorParams(bool aHasMin, double aMin, bool aHasMax, double aMax, double aTolerance)
{
  RelativeHumidityMeasurement::Attributes::MinMeasuredValue::Set(endpointId(), matterValue(aMin, aHasMin));
  RelativeHumidityMeasurement::Attributes::MaxMeasuredValue::Set(endpointId(), matterValue(aMax, aHasMax));
  RelativeHumidityMeasurement::Attributes::Tolerance::Set(endpointId(), static_cast<uint16_t>(matterValue(aTolerance)));
}


void DeviceHumidity::updateMeasuredValue(double aMeasuredValue, bool aIsValid, UpdateMode aUpdateMode)
{
  uint16_t v = matterValue(aMeasuredValue, aIsValid);
  RelativeHumidityMeasurement::Attributes::MeasuredValue::Set(endpointId(), v);
  if (aUpdateMode.Has(UpdateFlags::matter)) {
    MatterReportingAttributeChangeCallback(endpointId(), RelativeHumidityMeasurement::Id, RelativeHumidityMeasurement::Attributes::MeasuredValue::Id);
  }
}


