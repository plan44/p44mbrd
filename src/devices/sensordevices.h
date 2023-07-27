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

#pragma once

#include "device.h"

using namespace chip;
using namespace app;


// MARK: - SensorDevice, common base class for sensors

/// Generic attribute IDs used for supposedly common attributes at this device's level
/// @note these should be checked at compile/instantiation time in concrete sensor classes against
///   ZCL definitions
/// @note we take these from temperature measurement here, so checking in DeviceTemperature is redundant
#define SENSING_COMMON_MEASURED_VALUE_ATTRIBUTE_ID (Clusters::TemperatureMeasurement::Attributes::MeasuredValue::Id)
#define SENSING_COMMON_MIN_MEASURED_VALUE_ATTRIBUTE_ID (Clusters::TemperatureMeasurement::Attributes::MinMeasuredValue::Id)
#define SENSING_COMMON_MAX_MEASURED_VALUE_ATTRIBUTE_ID (Clusters::TemperatureMeasurement::Attributes::MaxMeasuredValue::Id)
#define SENSING_COMMON_TOLERANCE_ATTRIBUTE_ID (Clusters::TemperatureMeasurement::Attributes::Tolerance::Id)


class SensorDevice : public Device
{
  typedef Device inherited;

public:

  SensorDevice(DeviceInfoDelegate& aDeviceInfoDelegate);

  /// handler for external attribute read access
  virtual EmberAfStatus HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength) override;

  /// @param aBridgeValue the bridge-side property value
  /// @return the matter side value for bridge API side values
  virtual int32_t bridgeToMatter(double aBridgeValue) = 0;

  /// @name callbacks for Sensor implementations
  /// @{

  /// @brief set tolerance / precision / resolution
  /// @note this should be called at device setup, before the device goes operational
  /// @param aTolerance the tolerance (how much +/- from the precise value the returned measuerement
  ///   value can be). This equals resolution/2.
  void setTolerance(double aTolerance) { mTolerance = (uint16_t)(int16_t)bridgeToMatter(aTolerance); }

  /// @brief set minimal value this sensor might return (lower end of measurement range)
  /// @note this should be called at device setup, before the device goes operational
  /// @param aMin minimal value
  virtual void setMin(double aMin) = 0;

  /// @brief set maximal value this sensor might return (upper end of measurement range)
  /// @note this should be called at device setup, before the device goes operational
  /// @param aMax maximal value
  virtual void setMax(double aMax) = 0;

  /// @brief update measured value
  /// @note this must be called whenever the actual input reports a new value
  ///   (or reports not having no value at all), while the device is operational
  /// @param aMeasuredValue the currently measured value
  /// @param aIsValid true if aMeasuredValue is an actual value, false if the update means "we do not have a value"
  /// @param aUpdateMode update mode for propagating the sensor value
  virtual void updateMeasuredValue(double aMeasuredValue, bool aIsValid, UpdateMode aUpdateMode) = 0;

  /// @}


protected:

  /// @return the cluster ID of the specific sensor cluster
  virtual ClusterId sensorSpecificClusterId() = 0;
  virtual uint16_t sensorSpecificClusterRevision() = 0;
  virtual uint32_t sensorSpecificFeatureMap() { return 0; /* no features by default */ };

  /// @name common attributes in all 0x04xx measurement clusters
  /// @{
  uint16_t mTolerance; ///< tolerance (resolution), 0 by default
  /// @}


};


// MARK: - UnsignedSensorDevice

class UnsignedSensorDevice : public SensorDevice
{
  typedef SensorDevice inherited;

public:

  UnsignedSensorDevice(DeviceInfoDelegate& aDeviceInfoDelegate);

  virtual EmberAfStatus HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength) override;

protected:

  virtual string description() override;

  /// @name common attributes in all unsigned measurement clusters
  /// @{
  uint16_t mMeasuredValue; ///< current measured value, NULL if currently not known
  uint16_t mMin; ///< minimum value, NULL if not known
  uint16_t mMax; ///< maximum value, NULL if not known
  /// @}

  virtual void setMin(double aMin) override { mMin = (uint16_t)bridgeToMatter(aMin); };
  virtual void setMax(double aMax) override { mMax = (uint16_t)bridgeToMatter(aMax); };

  virtual void updateMeasuredValue(double aMeasuredValue, bool aIsValid, UpdateMode aUpdateMode) override;


};

// MARK: - SignedSensorDevice

class SignedSensorDevice : public SensorDevice
{
  typedef SensorDevice inherited;

public:

  SignedSensorDevice(DeviceInfoDelegate& aDeviceInfoDelegate);

  virtual EmberAfStatus HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength) override;

protected:

  virtual string description() override;

  /// @name common attributes in all signed measurement clusters
  /// @{
  int16_t mMeasuredValue; ///< current measured value, NULL if currently not known
  int16_t mMin; ///< minimum value, NULL if not known
  int16_t mMax; ///< maximum value, NULL if not known
  /// @}

  virtual void setMin(double aMin) override { mMin = (int16_t)bridgeToMatter(aMin); };
  virtual void setMax(double aMax) override { mMax = (int16_t)bridgeToMatter(aMax); };

  virtual void updateMeasuredValue(double aMeasuredValue, bool aIsValid, UpdateMode aUpdateMode) override;

};



// MARK: - Temperature Sensor

class DeviceTemperature : public SignedSensorDevice
{
  typedef SignedSensorDevice inherited;
public:
  DeviceTemperature(DeviceInfoDelegate& aDeviceInfoDelegate);
  virtual const char *deviceType() override { return "temperature sensor"; }
  virtual int32_t bridgeToMatter(double aBridgeValue) override;
  virtual ClusterId sensorSpecificClusterId() override;
  uint16_t sensorSpecificClusterRevision() override;
protected:
  virtual void finalizeDeviceDeclaration() override;
};


// MARK: - Illumination Sensor

class DeviceIlluminance : public UnsignedSensorDevice
{
  typedef UnsignedSensorDevice inherited;
public:
  DeviceIlluminance(DeviceInfoDelegate& aDeviceInfoDelegate);
  virtual const char *deviceType() override { return "illuminance sensor"; }
  virtual int32_t bridgeToMatter(double aBridgeValue) override;
  virtual ClusterId sensorSpecificClusterId() override;
  uint16_t sensorSpecificClusterRevision() override;
protected:
  virtual void finalizeDeviceDeclaration() override;
};


// MARK: - Humidity Sensor

class DeviceHumidity : public UnsignedSensorDevice
{
  typedef UnsignedSensorDevice inherited;
public:
  DeviceHumidity(DeviceInfoDelegate& aDeviceInfoDelegate);
  virtual const char *deviceType() override { return "humidity sensor"; }
  virtual int32_t bridgeToMatter(double aBridgeValue) override;
  virtual ClusterId sensorSpecificClusterId() override;
  uint16_t sensorSpecificClusterRevision() override;
protected:
  virtual void finalizeDeviceDeclaration() override;
};
