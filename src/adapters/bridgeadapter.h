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

#include "adapter.h"

#include "deviceonoff.h"

#include "bridgeapi/bridgeapi.h"


/// @brief implements the bridge for the P44 API
class P44_BridgeImpl
{

};


/// @brief base class for devices implemented via the P44 API
class P44_DeviceImpl :
  public DeviceAdapter, // common device adapter
  public DeviceInfoDelegate // the P44 side delegate implementations
{
  /// @name device properties that are immutable after instantiation
  /// @{

  string mBridgedDSUID; ///< dSUID of the bridged device (for making API calls)

  string mVendorName;
  string mModelName;
  string mConfigUrl;
  string mSerialNo;

  /// @}

  /// @name device properties that can change after instantiation
  /// @{

  bool mBridgeable; ///< device considered bridgeable from P44 side
  bool mActive; ///< device active (hardware reachable) from P44 side
  string mName; ///< current P44 side name of the device
  string mZone; ///< current P44 side name of the zone

  /// @}

public:

  P44_DeviceImpl();

  /// @name DeviceInfoDelegate
  /// @{
  virtual const string endpointUID() const override;

  virtual string vendorName() const override;
  virtual string modelName() const override;
  virtual string configUrl() const override;
  virtual string serialNo() const override;

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

  /// init device with information from bridge query results
  /// @param aDeviceInfo the JSON object for the entire bridge-side device
  /// @param aDeviceComponentInfo the JSON description object for the output or input that should be handled
  /// @param aInputType the name of the input type (sensor, binaryInput, button), or NULL if device is not an input device
  /// @param aInputId the name of the input ID within the input type, or NULL if device not an input device
  virtual void initBridgedInfo(JsonObjectPtr aDeviceInfo, JsonObjectPtr aDeviceComponentInfo = nullptr, const char* aInputType = nullptr, const char* aInputId = nullptr);

  /// called to handle notifications from bridge
  bool handleBridgeNotification(const string aNotification, JsonObjectPtr aParams);

  /// called to handle pushed properties coming from bridge
  virtual void handleBridgePushProperties(JsonObjectPtr aChangedProperties);

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


class P44_OnOffImpl : public P44_IdentifiableImpl, public OnOffDelegate
{
  typedef P44_IdentifiableImpl inherited;

  /// the default channel ID
  string mDefaultChannelId;

  /// @name OnOffDelegate
  /// @{
  virtual void setOnOffState(bool aOn) override;
  /// @}

  virtual void initBridgedInfo(JsonObjectPtr aDeviceInfo, JsonObjectPtr aDeviceComponentInfo = nullptr, const char* aInputType = nullptr, const char* aInputId = nullptr) override;

  /// called to handle pushed properties coming from bridge
  virtual void handleBridgePushProperties(JsonObjectPtr aChangedProperties) override;

  virtual void parseChannelStates(JsonObjectPtr aChannelStates, UpdateMode aUpdateMode);
};



class P44_ComposedImpl : public P44_DeviceImpl
{
  typedef P44_DeviceImpl inherited;

public:

  virtual void handleBridgePushProperties(JsonObjectPtr aChangedProperties) override;

};



class P44_InputImpl : public P44_DeviceImpl
{
  typedef P44_DeviceImpl inherited;

protected:

  string mInputType;
  string mInputId;

public:

  virtual void initBridgedInfo(JsonObjectPtr aDeviceInfo, JsonObjectPtr aDeviceComponentInfo = nullptr, const char* aInputType = nullptr, const char* aInputId = nullptr) override;

protected:

  virtual const string endpointUIDSuffix() const override { return mInputType + "_" + mInputId; }

};





// MARK: - Final composed devices
// - required implementation interfaces plus device itself in one object

#define DEVICE_ACCESSOR virtual Device &device() override { return static_cast<Device&>(*this); }

class P44_OnOffLightDevice final :
  public DeviceOnOffLight, // the matter side device
  public P44_OnOffImpl // the P44 side delegate implementation
{
public:
  P44_OnOffLightDevice() : DeviceOnOffLight(*this,*this,*this) {}; // this class itself implements all needed delegates
  DEVICE_ACCESSOR;
};


class P44_OnOffPluginUnitDevice final :
  public DeviceOnOffPluginUnit, // the matter side device
  public P44_OnOffImpl // the P44 side delegate implementation
{
public:
  P44_OnOffPluginUnitDevice() : DeviceOnOffPluginUnit(*this,*this,*this) {}; // this class itself implements all needed delegates
  DEVICE_ACCESSOR;
};


class P44_ComposedDevice final :
  public ComposedDevice, // the matter side device
  public P44_DeviceImpl // the P44 side delegate implementation
{
public:
  P44_ComposedDevice() : ComposedDevice(static_cast<P44_DeviceImpl&>(*this)) {}; // this class itself implements all needed delegates
  DEVICE_ACCESSOR;
};
