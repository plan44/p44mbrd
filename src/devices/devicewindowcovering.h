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

using namespace chip;


/// @brief delegate for window covering implementations
class WindowCoveringDelegate
{
public:

  virtual ~WindowCoveringDelegate() = default;

  /// Initiate or stop movement
  /// @param aMove if true, movement should be initiated according to target position attributes.
  ///   if false, movement should stop ASAP
  virtual void setMovement(bool aMove) = 0;

};


class DeviceWindowCovering : public IdentifiableDevice, public WindowCovering::Delegate
{
  typedef IdentifiableDevice inherited;

  WindowCoveringDelegate& mWindowCoveringDelegate;

public:

  DeviceWindowCovering(WindowCoveringDelegate& aWindowCoveringDelegate, IdentifyDelegate& aIdentifyDelegate, DeviceInfoDelegate& aDeviceInfoDelegate);

  virtual void didGetInstalled() override;

  virtual const char *deviceType() override { return "window covering"; }

  virtual string description() override;

  /// @name Matter Cluster's delegate
  /// @{
  virtual CHIP_ERROR HandleMovement(WindowCovering::WindowCoveringType type) override;
  virtual CHIP_ERROR HandleStopMotion() override;
  /// @}

protected:

  /// called to have the final leaf class declare the correct device type list
  virtual void finalizeDeviceDeclaration() override;

private:

  // additional storage for windowCovering cluster

};
