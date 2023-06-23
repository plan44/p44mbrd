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

#include "deviceonoff.h"

#include "bridgeapi/bridgeapi.h"


/// @brief implements the bridge for the P44 API
class P44_BridgeImpl
{

}


/// @brief base class for devices implemented via the P44 API
class P44_DeviceImpl :
  public DeviceInfoDelegate // the P44 side delegate implementations
{

  /// @name DeviceInfoDelegate
  /// @{
  virtual string getVendorName() override;
  virtual string getModelName() override;
  virtual string getConfigUrl() override;
  /// @}




  /// init device with information from bridge query results
  /// @param aDeviceInfo the JSON object for the entire bridge-side device
  /// @param aDeviceComponentInfo the JSON description object for the output or input that should be handled
  /// @param aInputType the name of the input type (sensor, binaryInput, button), or NULL if device is not an input device
  /// @param aInputId the name of the input ID within the input type, or NULL if device not an input device
  virtual void initBridgedInfo(JsonObjectPtr aDeviceInfo, JsonObjectPtr aDeviceComponentInfo = nullptr, const char* aInputType = nullptr, const char* aInputId = nullptr);




  /// @name bridge API helpers
  /// @{

  void notify(const string aNotification, JsonObjectPtr aParams);
  void call(const string aMethod, JsonObjectPtr aParams, JSonMessageCB aResponseCB);

  /// called to handle notifications from bridge
  bool handleBridgeNotification(const string aNotification, JsonObjectPtr aParams);

  /// called to handle pushed properties coming from bridge
  virtual void handleBridgePushProperties(JsonObjectPtr aChangedProperties);

  /// @}

}



class P44_IdentifiableImpl : public P44_DeviceImpl, public IdentityDelegate
{
  /// @name IdentityDelegate
  /// @{
  virtual identify(int aDurationS) override;
  /// @}
}



// Device composition
// - required implementation interfaces plus device itself in one object
class P44_OnOffDevice :
  public DeviceOnOff, // the matter side device
  public P44_IdentifiableImpl, public OnOffDelegate // the P44 side delegate implementations
{
  P44_OnOffDevice() : DeviceOnOff(*this,*this,*this); // this class implements all delegates

  /// @name OnOffDelegate
  /// @{
  virtual void setOnOffState(bool aOn) override;
  /// @}
}


