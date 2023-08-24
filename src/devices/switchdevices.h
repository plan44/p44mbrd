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

class SwitchDevice : public Device
{
  typedef Device inherited;

public:

  SwitchDevice(DeviceInfoDelegate& aDeviceInfoDelegate) : inherited(aDeviceInfoDelegate) {};

  /// @brief report switch event
  /// @param aNewPosition position the switch has reached now
  /// @param aPreviousPosition position the switch had before
  /// @param aLong beginning or end of a long press detection
  /// @param aCount number of multi-presses detected in the current sequence so far.
  ///   0=none/unknown, as could be after initial press before short/long-press separation timeout
  ///   or events between clicks that do not carry the count
  void reportSwitchEvent(int aNewPosition, int aPreviousPosition, bool aLong, int aCount);

};


// MARK: - Push Button

class DevicePushbutton : public SwitchDevice
{
  typedef SwitchDevice inherited;
public:
  DevicePushbutton(DeviceInfoDelegate& aDeviceInfoDelegate);
  virtual const char *deviceType() override { return "push button"; }
  virtual bool isLatching() { return false; };
protected:
  virtual void finalizeDeviceDeclaration() override;
};
