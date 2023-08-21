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

class SensorDevice : public Device
{
  typedef Device inherited;

public:

  SensorDevice(DeviceInfoDelegate& aDeviceInfoDelegate) : inherited(aDeviceInfoDelegate) {};

  /// @brief convenience generic method to set sensor params
  /// @param aMin minimal value the sensor can take
  /// @param aMax maximal value the sensor can take
  virtual void setupSensorParams(bool aHasMin, double aMin, bool aHasMax, double aMax, double aTolerance) = 0;

  /// @brief convenience generic method to update measured value of a sensor
  /// @note this must be called whenever the actual input reports a new value
  ///   (or reports not having no value at all), while the device is operational
  /// @param aMeasuredValue the currently measured value
  /// @param aIsValid true if aMeasuredValue is an actual value, false if the update means "we do not have a value"
  /// @param aUpdateMode update mode for propagating the sensor value
  virtual void updateMeasuredValue(double aMeasuredValue, bool aIsValid, UpdateMode aUpdateMode) = 0;

};


// MARK: - Temperature Sensor

class DeviceTemperature : public SensorDevice
{
  typedef SensorDevice inherited;
public:
  DeviceTemperature(DeviceInfoDelegate& aDeviceInfoDelegate);
  virtual const char *deviceType() override { return "temperature sensor"; }
  virtual void setupSensorParams(bool aHasMin, double aMin, bool aHasMax, double aMax, double aTolerance) override;
  virtual void updateMeasuredValue(double aMeasuredValue, bool aIsValid, UpdateMode aUpdateMode) override;
protected:
  virtual void finalizeDeviceDeclaration() override;
  static int16_t matterValue(double aValue);
};


// MARK: - Illumination Sensor

class DeviceIlluminance : public SensorDevice
{
  typedef SensorDevice inherited;
public:
  DeviceIlluminance(DeviceInfoDelegate& aDeviceInfoDelegate);
  virtual const char *deviceType() override { return "illuminance sensor"; }
  virtual void setupSensorParams(bool aHasMin, double aMin, bool aHasMax, double aMax, double aTolerance) override;
  virtual void updateMeasuredValue(double aMeasuredValue, bool aIsValid, UpdateMode aUpdateMode) override;
protected:
  virtual void finalizeDeviceDeclaration() override;
  static uint16_t matterValue(double aValue);
};


// MARK: - Humidity Sensor

class DeviceHumidity : public SensorDevice
{
  typedef SensorDevice inherited;
public:
  DeviceHumidity(DeviceInfoDelegate& aDeviceInfoDelegate);
  virtual const char *deviceType() override { return "humidity sensor"; }
  virtual void setupSensorParams(bool aHasMin, double aMin, bool aHasMax, double aMax, double aTolerance) override;
  virtual void updateMeasuredValue(double aMeasuredValue, bool aIsValid, UpdateMode aUpdateMode) override;
protected:
  virtual void finalizeDeviceDeclaration() override;
  static uint16_t matterValue(double aValue);
};
