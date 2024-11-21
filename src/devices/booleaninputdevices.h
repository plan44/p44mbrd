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


// MARK: - BinaryInputDevice


class BinaryInputDevice : public IdentifiableDevice
{
  typedef IdentifiableDevice inherited;

public:

  BinaryInputDevice(IdentifyDelegate* aIdentifyDelegateP, DeviceInfoDelegate& aDeviceInfoDelegate);

  virtual string description() override;

  /// @name callbacks for Sensor implementations
  /// @{

  /// @brief update input state
  /// @note this must be called whenever the actual input reports a new state
  ///   (or reports not having no state at all), while the device is operational
  /// @param aState current state
  /// @param aIsValid true if aState is an actually know state, false if the update means "we do not know the state"
  /// @param aUpdateMode update mode for propagating the sensor value
  virtual void updateCurrentState(bool aState, bool aIsValid, UpdateMode aUpdateMode) = 0;

  /// @}

};



// MARK: - Devices based on BooleanState cluster


class BoolanStateDevice  : public BinaryInputDevice
{
  typedef BinaryInputDevice inherited;

public:

  BoolanStateDevice(IdentifyDelegate* aIdentifyDelegateP, DeviceInfoDelegate& aDeviceInfoDelegate);

  /// update state in boolean state cluster
  virtual void updateCurrentState(bool aState, bool aIsValid, UpdateMode aUpdateMode) override;

};


class ContactSensorDevice : public BoolanStateDevice
{
  typedef BoolanStateDevice inherited;
  
public:

  ContactSensorDevice(IdentifyDelegate* aIdentifyDelegateP, DeviceInfoDelegate& aDeviceInfoDelegate);

  virtual const char *deviceType() override { return "contact sensor"; }

protected:

  /// called to have the final leaf class declare the correct device type list
  virtual bool finalizeDeviceDeclaration() override;
};



// MARK: - Devices NOT based on specific clusters, not BooleanState

class OccupancySensingDevice : public BinaryInputDevice
{
  typedef BinaryInputDevice inherited;

public:

  OccupancySensingDevice(IdentifyDelegate* aIdentifyDelegateP, DeviceInfoDelegate& aDeviceInfoDelegate);

  virtual const char *deviceType() override { return "occupancy sensor"; }

  virtual void didGetInstalled() override;

  /// update state in occupancy sensing cluster
  virtual void updateCurrentState(bool aState, bool aIsValid, UpdateMode aUpdateMode) override;

protected:

  /// called to have the final leaf class declare the correct device type list
  virtual bool finalizeDeviceDeclaration() override;
};
