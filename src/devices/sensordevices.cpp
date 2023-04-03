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

// MARK: - SensorDevice, common base class for sensors

SensorDevice::SensorDevice()
{
  mTolerance = 0;
}


void SensorDevice::initBridgedInfo(JsonObjectPtr aDeviceInfo, JsonObjectPtr aDeviceComponentInfo, const char* aInputType, const char* aInputId)
{
  inherited::initBridgedInfo(aDeviceInfo, aDeviceComponentInfo, aInputType, aInputId);
  JsonObjectPtr o;
  if (aDeviceComponentInfo->get("resolution", o)) {
    // tolerance is half of the resolution (when resolution is 1, true value might be max +/- 0.5 resolution away)
    mTolerance = (uint16_t)(int16_t)bridgeToMatter(o->doubleValue());
  }
  // also get current value from xxxStates
  parseSensorValue(aDeviceInfo, UpdateMode());
}


EmberAfStatus SensorDevice::HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength)
{
  if (clusterId==sensorSpecificClusterId()) {
    if (attributeId == SENSING_COMMON_TOLERANCE_ATTRIBUTE_ID) {
      return getAttr(buffer, maxReadLength, mTolerance);
    }
    // common attributes
    if (attributeId == Globals::Attributes::ClusterRevision::Id) {
      return getAttr(buffer, maxReadLength, sensorSpecificClusterRevision());
    }
    if ((attributeId == Globals::Attributes::FeatureMap::Id) && (maxReadLength == 4)) {
      return getAttr(buffer, maxReadLength, sensorSpecificFeatureMap());
    }
  }
  // let base class try
  return inherited::HandleReadAttribute(clusterId, attributeId, buffer, maxReadLength);
}


void SensorDevice::handleBridgePushProperties(JsonObjectPtr aChangedProperties)
{
  inherited::handleBridgePushProperties(aChangedProperties);
  parseSensorValue(aChangedProperties, UpdateMode(UpdateFlags::matter));
}


// MARK: - Unsigned Values Sensor Device

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(commonSensorAttrsUnsigned)
  DECLARE_DYNAMIC_ATTRIBUTE(SENSING_COMMON_MEASURED_VALUE_ATTRIBUTE_ID, INT16U, 2, 0),
  DECLARE_DYNAMIC_ATTRIBUTE(SENSING_COMMON_MIN_MEASURED_VALUE_ATTRIBUTE_ID, INT16U, 2, 0),
  DECLARE_DYNAMIC_ATTRIBUTE(SENSING_COMMON_MAX_MEASURED_VALUE_ATTRIBUTE_ID, INT16U, 2, 0),
  DECLARE_DYNAMIC_ATTRIBUTE(SENSING_COMMON_TOLERANCE_ATTRIBUTE_ID, INT16U, 2, 0),
DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

UnsignedSensorDevice::UnsignedSensorDevice()
{
  // init nullables
  using Traits = NumericAttributeTraits<uint16_t>;
  Traits::SetNull(mMeasuredValue);
  Traits::SetNull(mMin);
  Traits::SetNull(mMax);
}

string UnsignedSensorDevice::description()
{
  string s = inherited::description();
  string_format_append(s, "\n- Unsigned Sensor (%hu..%hu): %hu (+/- %hu)", mMeasuredValue, mMin, mMax, mTolerance);
  return s;
}


void UnsignedSensorDevice::initBridgedInfo(JsonObjectPtr aDeviceInfo, JsonObjectPtr aDeviceComponentInfo, const char* aInputType, const char* aInputId)
{
  inherited::initBridgedInfo(aDeviceInfo, aDeviceComponentInfo, aInputType, aInputId);
  JsonObjectPtr o;
  if (aDeviceComponentInfo->get("min", o)) {
    mMin = (uint16_t)bridgeToMatter(o->doubleValue());
  }
  if (aDeviceComponentInfo->get("max", o)) {
    mMax = (uint16_t)bridgeToMatter(o->doubleValue());
  }
}


void UnsignedSensorDevice::parseSensorValue(JsonObjectPtr aProperties, UpdateMode aUpdateMode)
{
  JsonObjectPtr states;
  if (aProperties->get((mInputType+"States").c_str(), states)) {
    JsonObjectPtr state;
    if (states->get(mInputId.c_str(), state)) {
      JsonObjectPtr o;
      if (state->get("value", o, true)) {
        // non-NULL value
        mMeasuredValue = (uint16_t)bridgeToMatter(o->doubleValue());
      }
      else {
        // NULL value or no value contained in state at all
        NumericAttributeTraits<uint16_t>::SetNull(mMeasuredValue);
      }
      if (aUpdateMode.Has(UpdateFlags::matter)) {
        MatterReportingAttributeChangeCallback(GetEndpointId(), sensorSpecificClusterId(), SENSING_COMMON_MEASURED_VALUE_ATTRIBUTE_ID);
      }
    }
  }
}


EmberAfStatus UnsignedSensorDevice::HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength)
{
  if (clusterId==sensorSpecificClusterId()) {
    if (attributeId == SENSING_COMMON_MEASURED_VALUE_ATTRIBUTE_ID) {
      return getAttr(buffer, maxReadLength, mMeasuredValue);
    }
    if (attributeId == SENSING_COMMON_MIN_MEASURED_VALUE_ATTRIBUTE_ID) {
      return getAttr(buffer, maxReadLength, mMin);
    }
    if (attributeId == SENSING_COMMON_MAX_MEASURED_VALUE_ATTRIBUTE_ID) {
      return getAttr(buffer, maxReadLength, mMax);
    }
  }
  // let base class try
  return inherited::HandleReadAttribute(clusterId, attributeId, buffer, maxReadLength);
}



// MARK: - Signed Values Sensor Device

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(commonSensorAttrsSigned)
  DECLARE_DYNAMIC_ATTRIBUTE(SENSING_COMMON_MEASURED_VALUE_ATTRIBUTE_ID, INT16S, 2, 0),
  DECLARE_DYNAMIC_ATTRIBUTE(SENSING_COMMON_MIN_MEASURED_VALUE_ATTRIBUTE_ID, INT16S, 2, 0),
  DECLARE_DYNAMIC_ATTRIBUTE(SENSING_COMMON_MAX_MEASURED_VALUE_ATTRIBUTE_ID, INT16S, 2, 0),
  DECLARE_DYNAMIC_ATTRIBUTE(SENSING_COMMON_TOLERANCE_ATTRIBUTE_ID, INT16U, 2, 0),
DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

SignedSensorDevice::SignedSensorDevice()
{
  // init nullables
  using Traits = NumericAttributeTraits<int16_t>;
  Traits::SetNull(mMeasuredValue);
  Traits::SetNull(mMin);
  Traits::SetNull(mMax);
}

string SignedSensorDevice::description()
{
  string s = inherited::description();
  string_format_append(s, "\n- Signed Sensor (%hd..%hd): %hd (+/- %hu)", mMeasuredValue, mMin, mMax, mTolerance);
  return s;
}


void SignedSensorDevice::initBridgedInfo(JsonObjectPtr aDeviceInfo, JsonObjectPtr aDeviceComponentInfo, const char* aInputType, const char* aInputId)
{
  inherited::initBridgedInfo(aDeviceInfo, aDeviceComponentInfo, aInputType, aInputId);
  JsonObjectPtr o;
  if (aDeviceComponentInfo->get("min", o)) {
    mMin = (int16_t)bridgeToMatter(o->doubleValue());
  }
  if (aDeviceComponentInfo->get("max", o)) {
    mMax = (int16_t)bridgeToMatter(o->doubleValue());
  }
}


void SignedSensorDevice::parseSensorValue(JsonObjectPtr aProperties, UpdateMode aUpdateMode)
{
  JsonObjectPtr states;
  if (aProperties->get((mInputType+"States").c_str(), states)) {
    JsonObjectPtr state;
    if (states->get(mInputId.c_str(), state)) {
      JsonObjectPtr o;
      if (state->get("value", o, true)) {
        // non-NULL value
        mMeasuredValue = (int16_t)bridgeToMatter(o->doubleValue());
      }
      else {
        // NULL value or no value contained in state at all
        NumericAttributeTraits<int16_t>::SetNull(mMeasuredValue);
      }
      if (aUpdateMode.Has(UpdateFlags::matter)) {
        MatterReportingAttributeChangeCallback(GetEndpointId(), sensorSpecificClusterId(), SENSING_COMMON_MEASURED_VALUE_ATTRIBUTE_ID);
      }
    }
  }
}


EmberAfStatus SignedSensorDevice::HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength)
{
  if (clusterId==sensorSpecificClusterId()) {
    if (attributeId == SENSING_COMMON_MEASURED_VALUE_ATTRIBUTE_ID) {
      return getAttr(buffer, maxReadLength, mMeasuredValue);
    }
    if (attributeId == SENSING_COMMON_MIN_MEASURED_VALUE_ATTRIBUTE_ID) {
      return getAttr(buffer, maxReadLength, mMin);
    }
    if (attributeId == SENSING_COMMON_MAX_MEASURED_VALUE_ATTRIBUTE_ID) {
      return getAttr(buffer, maxReadLength, mMax);
    }
  }
  // let base class try
  return inherited::HandleReadAttribute(clusterId, attributeId, buffer, maxReadLength);
}


// MARK: - Temperature Sensor Device

// TODO: try to extract revision definitions from ZAP-generated defs
#define ZCL_TEMP_MEASUREMENT_CLUSTER_REVISION (3u)

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(temperatureSensorClusters)
  DECLARE_DYNAMIC_CLUSTER(TemperatureMeasurement::Id, commonSensorAttrsSigned, nullptr, nullptr),
DECLARE_DYNAMIC_CLUSTER_LIST_END;

const EmberAfDeviceType gTemperatureSensorTypes[] = {
  { DEVICE_TYPE_MA_TEMP_SENSOR, DEVICE_VERSION_DEFAULT },
  { DEVICE_TYPE_MA_BRIDGED_DEVICE, DEVICE_VERSION_DEFAULT }
};


DeviceTemperature::DeviceTemperature()
{
  // Note: this check safeguards our assumption that this cluster's IDs are common among serveral
  //   measurement clusters, allowing a common implementation. If it fails, code must be rewritten.
  assert(
    TemperatureMeasurement::Attributes::MeasuredValue::Id==SENSING_COMMON_MEASURED_VALUE_ATTRIBUTE_ID &&
    TemperatureMeasurement::Attributes::MinMeasuredValue::Id==SENSING_COMMON_MIN_MEASURED_VALUE_ATTRIBUTE_ID &&
    TemperatureMeasurement::Attributes::MaxMeasuredValue::Id==SENSING_COMMON_MAX_MEASURED_VALUE_ATTRIBUTE_ID &&
    TemperatureMeasurement::Attributes::Tolerance::Id==SENSING_COMMON_TOLERANCE_ATTRIBUTE_ID
  );
  // - declare device specific clusters
  addClusterDeclarations(Span<EmberAfCluster>(temperatureSensorClusters));
}


void DeviceTemperature::finalizeDeviceDeclaration()
{
  finalizeDeviceDeclarationWithTypes(Span<const EmberAfDeviceType>(gTemperatureSensorTypes));
}


int32_t DeviceTemperature::bridgeToMatter(double aBridgeValue)
{
  // matter unit is 1/100th degree celsius
  return static_cast<int16_t>(aBridgeValue*100.0+0.5);
}

ClusterId DeviceTemperature::sensorSpecificClusterId()
{
  return TemperatureMeasurement::Id;
}

uint16_t DeviceTemperature::sensorSpecificClusterRevision()
{
  return ZCL_TEMP_MEASUREMENT_CLUSTER_REVISION;
}

// MARK: - Illumination Sensor Device

// TODO: try to extract revision definitions from ZAP-generated defs
#define ZCL_ILLUM_MEASUREMENT_CLUSTER_REVISION (3u)

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(illuminanceSensorClusters)
  DECLARE_DYNAMIC_CLUSTER(IlluminanceMeasurement::Id, commonSensorAttrsUnsigned, nullptr, nullptr),
DECLARE_DYNAMIC_CLUSTER_LIST_END;

const EmberAfDeviceType gIlluminanceSensorTypes[] = {
  { DEVICE_TYPE_MA_ILLUM_SENSOR, DEVICE_VERSION_DEFAULT },
  { DEVICE_TYPE_MA_BRIDGED_DEVICE, DEVICE_VERSION_DEFAULT }
};


DeviceIlluminance::DeviceIlluminance()
{
  // Note: this check safeguards our assumption that this cluster's IDs are common among serveral
  //   measurement clusters, allowing a common implementation. If it fails, code must be rewritten.
  assert(
    IlluminanceMeasurement::Attributes::MeasuredValue::Id==SENSING_COMMON_MEASURED_VALUE_ATTRIBUTE_ID &&
    IlluminanceMeasurement::Attributes::MinMeasuredValue::Id==SENSING_COMMON_MIN_MEASURED_VALUE_ATTRIBUTE_ID &&
    IlluminanceMeasurement::Attributes::MaxMeasuredValue::Id==SENSING_COMMON_MAX_MEASURED_VALUE_ATTRIBUTE_ID &&
    IlluminanceMeasurement::Attributes::Tolerance::Id==SENSING_COMMON_TOLERANCE_ATTRIBUTE_ID
  );
  // - declare device specific clusters
  addClusterDeclarations(Span<EmberAfCluster>(illuminanceSensorClusters));
}


void DeviceIlluminance::finalizeDeviceDeclaration()
{
  finalizeDeviceDeclarationWithTypes(Span<const EmberAfDeviceType>(gIlluminanceSensorTypes));
}


int32_t DeviceIlluminance::bridgeToMatter(double aBridgeValue)
{
  // matter unit is 10000*log10(lux)+1
  return static_cast<uint16_t>(10000.0*log10(aBridgeValue)+1);
}


ClusterId DeviceIlluminance::sensorSpecificClusterId()
{
  return IlluminanceMeasurement::Id;
}

uint16_t DeviceIlluminance::sensorSpecificClusterRevision()
{
  return ZCL_ILLUM_MEASUREMENT_CLUSTER_REVISION;
}

// MARK: - Humidity Sensor Device

// TODO: try to extract revision definitions from ZAP-generated defs
#define ZCL_RELATIVE_HUMIDITY_MEASUREMENT_CLUSTER_REVISION (3u)

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(relativeHumiditySensorClusters)
  DECLARE_DYNAMIC_CLUSTER(RelativeHumidityMeasurement::Id, commonSensorAttrsUnsigned, nullptr, nullptr),
DECLARE_DYNAMIC_CLUSTER_LIST_END;

const EmberAfDeviceType gRelativeHumiditySensorTypes[] = {
  { DEVICE_TYPE_MA_RELATIVE_HUMIDITY_SENSOR, DEVICE_VERSION_DEFAULT },
  { DEVICE_TYPE_MA_BRIDGED_DEVICE, DEVICE_VERSION_DEFAULT }
};


DeviceHumidity::DeviceHumidity()
{
  // Note: this check safeguards our assumption that this cluster's IDs are common among serveral
  //   measurement clusters, allowing a common implementation. If it fails, code must be rewritten.
  assert(
    RelativeHumidityMeasurement::Attributes::MeasuredValue::Id==SENSING_COMMON_MEASURED_VALUE_ATTRIBUTE_ID &&
    RelativeHumidityMeasurement::Attributes::MinMeasuredValue::Id==SENSING_COMMON_MIN_MEASURED_VALUE_ATTRIBUTE_ID &&
    RelativeHumidityMeasurement::Attributes::MaxMeasuredValue::Id==SENSING_COMMON_MAX_MEASURED_VALUE_ATTRIBUTE_ID &&
    RelativeHumidityMeasurement::Attributes::Tolerance::Id==SENSING_COMMON_TOLERANCE_ATTRIBUTE_ID
  );
  // - declare device specific clusters
  addClusterDeclarations(Span<EmberAfCluster>(relativeHumiditySensorClusters));
}


void DeviceHumidity::finalizeDeviceDeclaration()
{
  finalizeDeviceDeclarationWithTypes(Span<const EmberAfDeviceType>(gRelativeHumiditySensorTypes));
}


int32_t DeviceHumidity::bridgeToMatter(double aBridgeValue)
{
  // matter unit is 100 * humidity percentage
  return static_cast<uint16_t>(100.0*aBridgeValue+0.5);
}


ClusterId DeviceHumidity::sensorSpecificClusterId()
{
  return RelativeHumidityMeasurement::Id;
}

uint16_t DeviceHumidity::sensorSpecificClusterRevision()
{
  return ZCL_RELATIVE_HUMIDITY_MEASUREMENT_CLUSTER_REVISION;
}
