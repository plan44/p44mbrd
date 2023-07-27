//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
//  based on Apache v2 licensed bridge-app example code (c) 2021 Project CHIP Authors
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

#include "adapters/adapters.h"

#if P44_ADAPTERS

#include "adapters/p44/p44bridgeapi.h"

#include "deviceonoff.h"
#include "devicelevelcontrol.h"
#include "devicecolorcontrol.h"
#include "sensordevices.h"
#include "booleaninputdevices.h"


// MARK: - Generic device implementation classes

/// @brief base class for devices implemented via the P44 API
class P44_DeviceImpl :
  public DeviceAdapter, // common device adapter
  public DeviceInfoDelegate // the P44 side delegate implementations
{
  /// @name device properties that are immutable after instantiation
  /// @{

  string mBridgedDSUID; ///< dSUID of the bridged device (for making API calls)

  /// @}

  /// @name device properties that can change after instantiation
  /// @{

  bool mBridgeable; ///< device considered bridgeable from P44 side
  bool mActive; ///< device active (hardware reachable) from P44 side
  string mName; ///< current P44 side name of the device
  string mZone; ///< current P44 side name of the zone

  /// @}

  JsonObjectPtr mTempDeviceInfo; ///< keeps device info temporarily until device is installed

public:

  P44_DeviceImpl();

  /// @name DeviceInfoDelegate
  /// @{
  virtual const string endpointUID() const override;

  virtual void deviceDidGetInstalled() override;

  virtual bool isReachable() const override;

  virtual string name() const override;
  virtual bool changeName(const string aNewName) override;

  virtual string zone() const override;
  //virtual bool changeZone(const string aNewZone) override; // TODO: implement

  /// @}

  /// @name P44 bridge API specific methods
  /// @{

  /// @return `P44_DeviceImpl` pointer from DevicePtr
  inline static P44_DeviceImpl* impl(DevicePtr aDevice) { return static_cast<P44_DeviceImpl*>(&(aDevice->deviceInfoDelegate())); } // we *know* the delegate *is* a P44_DeviceImpl

  /// @return suffix for endpointUID() for when a bridge-side device functionality is represented
  ///   matter side as a composed device consisting of multiple subdevices.
  virtual const string endpointUIDSuffix() const { return "output"; }

  void notify(const string aNotification, JsonObjectPtr aParams);
  void call(const string aMethod, JsonObjectPtr aParams, JSonMessageCB aResponseCB);

  /// @brief init device with information from bridge query results
  /// @note the device does not yet have a endpointID at this point and CANNOT ACCESS ATTRIBUTES yet
  /// @param aDeviceInfo the JSON object for the entire bridge-side device
  /// @param aDeviceComponentInfo the JSON description object for the output or input that should be handled
  /// @param aInputType the name of the input type (sensor, binaryInput, button), or NULL if device is not an input device
  /// @param aInputId the name of the input ID within the input type, or NULL if device not an input device
  virtual void initBridgedInfo(JsonObjectPtr aDeviceInfo, JsonObjectPtr aDeviceComponentInfo = nullptr, const char* aInputType = nullptr, const char* aInputId = nullptr);

  /// called to handle notifications from bridge
  bool handleBridgeNotification(const string aNotification, JsonObjectPtr aParams);

  /// called to handle pushed properties coming from bridge
  virtual void handleBridgePushProperties(JsonObjectPtr aChangedProperties);

protected:

  /// @brief update device with information from bridge query results now where the device is installed
  ///   and CAN ACCESS ATTRIBUTES.
  /// @param aDeviceInfo the JSON object for the entire bridge-side device
  virtual void updateBridgedInfo(JsonObjectPtr aDeviceInfo);

  /// @}

};



class P44_IdentifiableImpl : public P44_DeviceImpl, public IdentifyDelegate
{
  typedef P44_DeviceImpl inherited;

  /// @name IdentifyDelegate
  /// @{
  virtual void identify(int aDurationS) override;
  /// @}
};


class P44_ComposedImpl : public P44_DeviceImpl
{
  typedef P44_DeviceImpl inherited;

public:

  virtual void handleBridgePushProperties(JsonObjectPtr aChangedProperties) override;

};


// MARK: output devices

class P44_OnOffImpl : public P44_IdentifiableImpl, public OnOffDelegate
{
  typedef P44_IdentifiableImpl inherited;

protected:

  /// the default channel ID
  string mDefaultChannelId;

  /// @name OnOffDelegate
  /// @{
  virtual void setOnOffState(bool aOn) override;
  /// @}

  virtual void initBridgedInfo(JsonObjectPtr aDeviceInfo, JsonObjectPtr aDeviceComponentInfo = nullptr, const char* aInputType = nullptr, const char* aInputId = nullptr) override;
  virtual void updateBridgedInfo(JsonObjectPtr aDeviceInfo) override;
  virtual void handleBridgePushProperties(JsonObjectPtr aChangedProperties) override;
  virtual void parseChannelStates(JsonObjectPtr aChannelStates, UpdateMode aUpdateMode);
};


class P44_LevelControlImpl : public P44_OnOffImpl, public LevelControlDelegate
{
  typedef P44_OnOffImpl inherited;

protected:

  /// the hardware recommended transition time (usually provided by the bridged hardware)
  uint16_t mRecommendedTransitionTimeDS;
  MLMicroSeconds mEndOfLatestTransition;

  /// @name LevelControlDelegate
  /// @{
  virtual void setLevel(double aNewLevel, uint16_t aTransitionTimeDS) override;
  virtual void dim(int8_t aDirection, uint8_t aRate) override;
  virtual MLMicroSeconds endOfLatestTransition() override { return mEndOfLatestTransition; };
  /// @}

  virtual void updateBridgedInfo(JsonObjectPtr aDeviceInfo) override;
  virtual void parseChannelStates(JsonObjectPtr aChannelStates, UpdateMode aUpdateMode) override;

public:
  P44_LevelControlImpl();

};


class P44_ColorControlImpl : public P44_LevelControlImpl, public ColorControlDelegate
{
  typedef P44_LevelControlImpl inherited;

  /// @name ColorControlDelegate
  /// @{
  virtual void setHue(uint8_t aHue, uint16_t aTransitionTimeDS, bool aApply) override;
  virtual void setSaturation(uint8_t aSaturation, uint16_t aTransitionTimeDS, bool aApply) override;
  virtual void setCieX(uint16_t aX, uint16_t aTransitionTimeDS, bool aApply) override;
  virtual void setCieY(uint16_t aY, uint16_t aTransitionTimeDS, bool aApply) override;
  virtual void setColortemp(uint16_t aColortemp, uint16_t aTransitionTimeDS, bool aApply) override;
  /// @}

  virtual void initBridgedInfo(JsonObjectPtr aDeviceInfo, JsonObjectPtr aDeviceComponentInfo = nullptr, const char* aInputType = nullptr, const char* aInputId = nullptr) override;
  virtual void parseChannelStates(JsonObjectPtr aChannelStates, UpdateMode aUpdateMode) override;

public:
  P44_ColorControlImpl();

};



// MARK: - Input devices

class P44_InputImpl : public P44_DeviceImpl
{
  typedef P44_DeviceImpl inherited;

protected:

  string mInputType;
  string mInputId;

public:

  virtual void initBridgedInfo(JsonObjectPtr aDeviceInfo, JsonObjectPtr aDeviceComponentInfo = nullptr, const char* aInputType = nullptr, const char* aInputId = nullptr) override;

protected:

  virtual const string endpointUIDSuffix() const override;

};


class P44_SensorImpl : public P44_InputImpl
{
  typedef P44_InputImpl inherited;

public:

  virtual void initBridgedInfo(JsonObjectPtr aDeviceInfo, JsonObjectPtr aDeviceComponentInfo = nullptr, const char* aInputType = nullptr, const char* aInputId = nullptr) override;
  virtual void handleBridgePushProperties(JsonObjectPtr aChangedProperties) override;

private:

  void parseSensorValue(JsonObjectPtr aProperties, UpdateMode aUpdateMode);

};


class P44_BinaryInputImpl : public P44_InputImpl
{
  typedef P44_InputImpl inherited;

public:

  virtual void initBridgedInfo(JsonObjectPtr aDeviceInfo, JsonObjectPtr aDeviceComponentInfo = nullptr, const char* aInputType = nullptr, const char* aInputId = nullptr) override;
  virtual void handleBridgePushProperties(JsonObjectPtr aChangedProperties) override;

private:

  void parseInputValue(JsonObjectPtr aProperties, UpdateMode aUpdateMode);

};




// MARK: - Final device classes - required implementation interfaces plus device itself in one object

/// @brief P44 composed device (no functionality of its own, just container with device info
class P44_ComposedDevice final :
  public ComposedDevice, // the matter side device
  public P44_DeviceImpl // the P44 side delegate implementation
{
public:
  P44_ComposedDevice() : ComposedDevice(DG(DeviceInfo)) {}; // this class itself implements all needed delegates
  DEVICE_ACCESSOR;
};


// MARK: Outputs

class P44_OnOffLightDevice final :
  public DeviceOnOffLight, // the matter side device
  public P44_OnOffImpl // the P44 side delegate implementation
{
public:
  P44_OnOffLightDevice() : DeviceOnOffLight(DG(OnOff), DG(Identify), DG(DeviceInfo)) {}; // this class itself implements all needed delegates
  DEVICE_ACCESSOR;
};


class P44_OnOffPluginUnitDevice final :
  public DeviceOnOffPluginUnit, // the matter side device
  public P44_OnOffImpl // the P44 side delegate implementation
{
public:
  P44_OnOffPluginUnitDevice() : DeviceOnOffPluginUnit(DG(OnOff), DG(Identify), DG(DeviceInfo)) {}; // this class itself implements all needed delegates
  DEVICE_ACCESSOR;
};


class P44_DimmableLightDevice final :
  public DeviceDimmableLight, // the matter side device
  public P44_LevelControlImpl // the P44 side delegate implementation
{
public:
  P44_DimmableLightDevice() : DeviceDimmableLight(DG(LevelControl), DG(OnOff), DG(Identify), DG(DeviceInfo)) {}; // this class itself implements all needed delegates
  DEVICE_ACCESSOR;
};


class P44_DimmablePluginUnitDevice final :
  public DeviceDimmablePluginUnit, // the matter side device
  public P44_LevelControlImpl // the P44 side delegate implementation
{
public:
  P44_DimmablePluginUnitDevice() : DeviceDimmablePluginUnit(DG(LevelControl), DG(OnOff), DG(Identify), DG(DeviceInfo)) {}; // this class itself implements all needed delegates
  DEVICE_ACCESSOR;
};


class P44_ColorLightDevice final :
  public DeviceColorControl, // the matter side device
  public P44_ColorControlImpl // the P44 side delegate implementation
{
public:
  P44_ColorLightDevice(bool aCtOnly) : DeviceColorControl(aCtOnly, DG(ColorControl), DG(LevelControl), DG(OnOff), DG(Identify), DG(DeviceInfo)) {}; // this class itself implements all needed delegates
  DEVICE_ACCESSOR;
};


// MARK: Sensors

class P44_TemperatureSensor final :
  public DeviceTemperature, // the matter side device
  public P44_SensorImpl // the P44 side implementation
{
public:
  P44_TemperatureSensor() : DeviceTemperature(DG(DeviceInfo)) {}; // this class itself implements all needed delegates
  DEVICE_ACCESSOR;
};


class P44_IlluminanceSensor final :
  public DeviceIlluminance, // the matter side device
  public P44_SensorImpl // the P44 side implementation
{
public:
  P44_IlluminanceSensor() : DeviceIlluminance(DG(DeviceInfo)) {}; // this class itself implements all needed delegates
  DEVICE_ACCESSOR;
};


class P44_HumiditySensor final :
  public DeviceHumidity, // the matter side device
  public P44_SensorImpl // the P44 side implementation
{
public:
  P44_HumiditySensor() : DeviceHumidity(DG(DeviceInfo)) {}; // this class itself implements all needed delegates
  DEVICE_ACCESSOR;
};


// MARK: Boolean Inputs


class P44_ContactInput final :
  public ContactSensorDevice, // the matter side device
  public P44_BinaryInputImpl // the P44 side implementation
{
public:
  P44_ContactInput() : ContactSensorDevice(DG(DeviceInfo)) {}; // this class itself implements all needed delegates
  DEVICE_ACCESSOR;
};




#endif // P44_ADAPTERS

