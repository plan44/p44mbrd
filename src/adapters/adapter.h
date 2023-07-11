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
#include "device.h"


/// @brief common class for device adapters
/// Contains utilities needed to access the matter side device from delegates
class DeviceAdapter
{
public:
  using UpdateMode = Device::UpdateMode;
  using UpdateFlags = Device::UpdateFlags;

  virtual ~DeviceAdapter() = default;

  /// @return reference to the actual device this adapter is part of
  /// @note this method must be implemented in all final subclasses of Device for delegates to access the
  ///    device instance
  virtual Device &device() = 0;

  /// @return const to the actual device this adapter is part of
  inline const Device &const_device() const { return const_cast<DeviceAdapter *>(this)->device(); }

  /// @return non-null pointer to subclass pointer as specified by DevType, or fails assertion.
  template<typename DevType> auto deviceP() { auto devP = dynamic_cast<DevType*>(&device()); assert(devP); return devP; }
};

// logging from within adapter implementation classes
#define DLOGENABLED(lvl) (device().logEnabled(lvl))
#define DLOG(lvl,...) { if (device().logEnabled(lvl)) device().log(lvl,##__VA_ARGS__); }
