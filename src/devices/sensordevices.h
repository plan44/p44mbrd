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
/// @note these should be checked at compile time in concrete sensor classes against
///   ZCL definitions
#define SENSING_COMMON_MEASURED_VALUE_ATTRIBUTE_ID (0x0000)
#define SENSING_COMMON_MIN_MEASURED_VALUE_ATTRIBUTE_ID (0x0001)
#define SENSING_COMMON_MAX_MEASURED_VALUE_ATTRIBUTE_ID (0x0002)
#define SENSING_COMMON_TOLERANCE_ATTRIBUTE_ID (0x0003)


class SensorDevice : public InputDevice
{
  typedef InputDevice inherited;
public:

  SensorDevice();

  virtual void initBridgedInfo(JsonObjectPtr aDeviceInfo, JsonObjectPtr aDeviceComponentInfo = nullptr, const char* aInputType = nullptr, const char* aInputId = nullptr) override;

  /// handler for external attribute read access
  virtual EmberAfStatus HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength) override;

  /// called to handle pushed properties coming from bridge
  virtual void handleBridgePushProperties(JsonObjectPtr aChangedProperties) override;

protected:

  /// @param aBridgeValue the bridge-side property value
  /// @return the matter side value for bridge API side values
  virtual int32_t bridgeToMatter(double aBridgeValue) = 0;

  /// @return the cluster ID of the specific sensor cluster
  virtual ClusterId sensorSpecificClusterId() = 0;
  virtual uint16_t sensorSpecificClusterRevision() = 0;
  virtual uint32_t sensorSpecificFeatureMap() { return 0; /* no features by default */ };

  /// Parse measured sensor value (according to InputDevice's mInputType and mInputId)
  /// from property container
  /// @param aProperties the root property container, either received via push or queried
  /// @param aUpdateMode what to update
  virtual void parseSensorValue(JsonObjectPtr aProperties, UpdateMode aUpdateMode) = 0;

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
  UnsignedSensorDevice();
  virtual void initBridgedInfo(JsonObjectPtr aDeviceInfo, JsonObjectPtr aDeviceComponentInfo = nullptr, const char* aInputType = nullptr, const char* aInputId = nullptr) override;
  virtual EmberAfStatus HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength) override;
protected:
  virtual void parseSensorValue(JsonObjectPtr aProperties, UpdateMode aUpdateMode);
  virtual string description() override;
private:
  /// @name common attributes in all unsigned measurement clusters
  /// @{
  uint16_t mMeasuredValue; ///< current measured value, NULL if currently not known
  uint16_t mMin; ///< minimum value, NULL if not known
  uint16_t mMax; ///< maximum value, NULL if not known
  /// @}
};

// MARK: - SignedSensorDevice

class SignedSensorDevice : public SensorDevice
{
  typedef SensorDevice inherited;
public:
  SignedSensorDevice();
  virtual void initBridgedInfo(JsonObjectPtr aDeviceInfo, JsonObjectPtr aDeviceComponentInfo = nullptr, const char* aInputType = nullptr, const char* aInputId = nullptr) override;
  virtual EmberAfStatus HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength) override;
protected:
  virtual void parseSensorValue(JsonObjectPtr aProperties, UpdateMode aUpdateMode);
  virtual string description() override;
private:
  /// @name common attributes in all signed measurement clusters
  /// @{
  int16_t mMeasuredValue; ///< current measured value, NULL if currently not known
  int16_t mMin; ///< minimum value, NULL if not known
  int16_t mMax; ///< maximum value, NULL if not known
  /// @}
};



// MARK: - Temperature Sensor

class DeviceTemperature : public SignedSensorDevice
{
  typedef SignedSensorDevice inherited;
public:
  DeviceTemperature();
  virtual string description() override;
  virtual int32_t bridgeToMatter(double aBridgeValue) override;
  virtual ClusterId sensorSpecificClusterId() override;
  uint16_t sensorSpecificClusterRevision() override;
protected:
  virtual void finalizeDeviceDeclaration() override;
};


// MARK: - Illumination Sensor

class DeviceIlluminance : public UnsignedSensorDevice
{
  typedef SensorDevice inherited;
public:
  DeviceIlluminance();
  virtual string description() override;
  virtual int32_t bridgeToMatter(double aBridgeValue) override;
  virtual ClusterId sensorSpecificClusterId() override;
  uint16_t sensorSpecificClusterRevision() override;
protected:
  virtual void finalizeDeviceDeclaration() override;
};


// MARK: - Humidity Sensor

class DeviceHumidity : public UnsignedSensorDevice
{
  typedef SensorDevice inherited;
public:
  DeviceHumidity();
  virtual string description() override;
  virtual int32_t bridgeToMatter(double aBridgeValue) override;
  virtual ClusterId sensorSpecificClusterId() override;
  uint16_t sensorSpecificClusterRevision() override;
protected:
  virtual void finalizeDeviceDeclaration() override;
};
