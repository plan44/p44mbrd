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

#include <app/clusters/window-covering-server/window-covering-delegate.h>
#include <app/clusters/window-covering-server/window-covering-server.h>

using namespace chip;


/// @brief delegate for window covering implementations
class WindowCoveringDelegate
{
public:

  virtual ~WindowCoveringDelegate() = default;

  /// Initiate movement to target value(s)
  /// @param aMovementType indicates movement to perform (lift or tilt), hardware might need
  ///   to perform both at the same time
  /// @note this is called for position-aware window coverings. Implementation must consult
  ///   relevant attributes to decide what movement needs to be initiated
  virtual void startMovement(WindowCovering::WindowCoveringType aMovementType) = 0;

  /// Start simple movement for non-position-aware window coverings
  /// @param aMovementType indicates movement to perform (lift or tilt), hardware might need
  ///   to perform both at the same time or be unable to differentiate at all
  /// @param aUpOrOpen if true, the movement should start in the "up or open" direction, otherwise "down or close"
  virtual void simpleStartMovement(WindowCovering::WindowCoveringType aMovementType, bool aUpOrOpen) = 0;

  /// Stop movement
  virtual void stopMovement() = 0;

};


class DeviceWindowCovering : public IdentifiableDevice, public WindowCovering::Delegate
{
  typedef IdentifiableDevice inherited;

  WindowCoveringDelegate& mWindowCoveringDelegate;

public:

  DeviceWindowCovering(WindowCoveringDelegate& aWindowCoveringDelegate, IdentifyDelegate* aIdentifyDelegateP, DeviceInfoDelegate& aDeviceInfoDelegate);

  virtual void didGetInstalled() override;

  virtual const char *deviceType() override { return "window covering"; }

  virtual string description() override;

  /// @name Matter Cluster's delegate
  /// @{
  virtual CHIP_ERROR HandleMovement(WindowCovering::WindowCoveringType type) override;
  virtual CHIP_ERROR StartNonPAMovement(WindowCovering::WindowCoveringType type, bool aUpOrOpen) override;
  virtual CHIP_ERROR HandleStopMotion() override;
  /// @}

protected:

  /// called to have the final leaf class declare the correct device type list
  virtual bool finalizeDeviceDeclaration() override;

private:

  
  // additional storage for windowCovering cluster

};
