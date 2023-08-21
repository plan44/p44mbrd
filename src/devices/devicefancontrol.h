//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#include "devicelevelcontrol.h" // for using same delegate, allowing same adapter side implementation

#include <app/clusters/fan-control-server/fan-control-server.h>


using namespace chip;


/// @brief delegate for extended fan control implementations (with automatic mode etc.)
class FanControlExtrasDelegate
{
public:

  virtual ~FanControlExtrasDelegate() = default;

  /// @return true if this fan has automatic intensity mode
  virtual bool hasAutoMode() { return true; /* usually, because having the delegate means it's not just level setting */ }

  /// @param aAuto set to enable automatic intensity
  /// @param aCurrentLevel indicates the current level at the time auto mode is switched on of off
  virtual void setAutoMode(bool aAuto, double aCurrentLevel) { /* to be implemented if we have auto mode */ };

};


class DeviceFanControl : public IdentifiableDevice, public LevelControlImplementationInterface, public FanControl::Delegate
{
  typedef IdentifiableDevice inherited;

  LevelControlDelegate& mLevelControlDelegate;
  FanControlExtrasDelegate* mFanControlExtrasDelegateP;

public:

  DeviceFanControl(FanControlExtrasDelegate* aOptionalFanControlExtrasDelegate, LevelControlDelegate& aLevelControlDelegate, IdentifyDelegate& aIdentifyDelegate, DeviceInfoDelegate& aDeviceInfoDelegate);

  virtual void didGetInstalled() override;

  virtual const char *deviceType() override { return "fan control"; }

  virtual string description() override;



  /// @name LevelControlImplementationInterface
  /// @{
  virtual void setDefaultOnLevel(double aLevelPercent) override;
  virtual bool updateLevel(double aLevelPercent, Device::UpdateMode aUpdateMode) override;
  /// @}

  /// @name callbacks for FanControlExtrasDelegate implementations
  /// @{
  bool updateAuto(bool aAuto, double aLevel, Device::UpdateMode aUpdateMode);
  /// @}

  /// @name handlers for attribute processing
  /// @{
  virtual void handleAttributeChange(ClusterId aClusterId, chip::AttributeId aAttributeId) override;
  /// @}

  /// @name Matter Cluster's delegate
  /// @{

  virtual Status HandleStep(FanControl::StepDirectionEnum aDirection, bool aWrap, bool aLowestOff) override;
  /// @}


protected:

  /// called to have the final leaf class declare the correct device type list
  virtual void finalizeDeviceDeclaration() override;

private:

  void setImpliedLevel(Percent aLevelPercent);

};
