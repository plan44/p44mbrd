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

class SwitchDevice : public IdentifiableDevice
{
  typedef IdentifiableDevice inherited;

public:

  typedef std::map<int, string> PositionsMap;
  PositionsMap mActivePositions;

  SwitchDevice(IdentifyDelegate* aIdentifyDelegateP, DeviceInfoDelegate& aDeviceInfoDelegate);

  void setActivePosition(int aPosition, const string& aPositionName);

};


// MARK: - Push Button

class DevicePushbutton : public SwitchDevice
{
  typedef SwitchDevice inherited;
public:
  DevicePushbutton(IdentifyDelegate* aIdentifyDelegateP, DeviceInfoDelegate& aDeviceInfoDelegate) : inherited(aIdentifyDelegateP, aDeviceInfoDelegate) {};
  virtual const char *deviceType() override { return "push button"; }
  virtual bool isLatching() { return false; };
protected:
  virtual bool finalizeDeviceDeclaration() override;
};
