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

#if CC51_ADAPTERS

#include "deviceonoff.h"
#include "devicelevelcontrol.h"
#include "devicecolorcontrol.h"
#include "sensordevices.h"
#include "booleaninputdevices.h"


// MARK: - Generic device implementation classes

/// @brief base class for devices implemented via the CC51 API
class CC51_DeviceImpl :
  public DeviceAdapter, // common device adapter
  public DeviceInfoDelegate // the CC51 side delegate implementations
{
  /// @name device properties that are immutable after instantiation
  /// @{

  // TODO: usually things like model, vendor, UUID etc.

  /// @}

  /// @name device properties that can change after instantiation
  /// @{

  string mName; ///< current CC51 side name of the device
  string mZone; ///< current CC51 side name of the zone

  /// @}

public:

  CC51_DeviceImpl();

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

  /// @name CC51 bridge API specific methods
  /// @{

  /// @return `CC51_DeviceImpl` pointer from DevicePtr
  inline static CC51_DeviceImpl* impl(DevicePtr aDevice) { return static_cast<CC51_DeviceImpl*>(&(aDevice->deviceInfoDelegate())); } // we *know* the delegate *is* a CC51_DeviceImpl

  /// @}

};



class CC51_IdentifiableImpl : public CC51_DeviceImpl, public IdentifyDelegate
{
  typedef CC51_DeviceImpl inherited;

  /// @name IdentifyDelegate
  /// @{
  virtual void identify(int aDurationS) override;
  /// @}
};


// MARK: output devices

class CC51_OnOffImpl : public CC51_IdentifiableImpl, public OnOffDelegate
{
  typedef CC51_IdentifiableImpl inherited;

protected:

  /// the default channel ID
  string mDefaultChannelId;

  /// @name OnOffDelegate
  /// @{
  virtual void setOnOffState(bool aOn) override;
  /// @}

};




// MARK: - Final device classes - required implementation interfaces plus device itself in one object


/// @brief CC51 composed device (no functionality of its own, just container with device info
class CC51_ComposedDevice final :
  public ComposedDevice, // the matter side device
  public CC51_DeviceImpl // the CC51 side delegate implementation
{
public:
  CC51_ComposedDevice() : ComposedDevice(DG(DeviceInfo)) {}; // this class itself implements all needed delegates
  DEVICE_ACCESSOR;
};


// MARK: Outputs

class CC51_OnOffLightDevice final :
  public DeviceOnOffLight, // the matter side device
  public CC51_OnOffImpl // the CC51 side delegate implementation
{
public:
  CC51_OnOffLightDevice() : DeviceOnOffLight(DG(OnOff), DG(Identify), DG(DeviceInfo)) {}; // this class itself implements all needed delegates
  DEVICE_ACCESSOR;
};


class CC51_OnOffPluginUnitDevice final :
  public DeviceOnOffPluginUnit, // the matter side device
  public CC51_OnOffImpl // the CC51 side delegate implementation
{
public:
  CC51_OnOffPluginUnitDevice() : DeviceOnOffPluginUnit(DG(OnOff), DG(Identify), DG(DeviceInfo)) {}; // this class itself implements all needed delegates
  DEVICE_ACCESSOR;
};



#endif // CC51_ADAPTERS
