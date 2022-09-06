//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2022 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include <platform/DeviceInstanceInfoProvider.h>
#include <string>

using namespace chip;
using namespace std;

class P44DeviceInfoProvider : public DeviceLayer::DeviceInstanceInfoProvider
{
public:
  P44DeviceInfoProvider() : mVendorId(0), mProductId(0) {};
  virtual ~P44DeviceInfoProvider() = default;

  virtual CHIP_ERROR GetVendorName(char * buf, size_t bufSize) override;
  virtual CHIP_ERROR GetVendorId(uint16_t & vendorId) override;
  virtual CHIP_ERROR GetProductName(char * buf, size_t bufSize) override;
  virtual CHIP_ERROR GetProductId(uint16_t & productId) override;
  virtual CHIP_ERROR GetSerialNumber(char * buf, size_t bufSize) override;
  virtual CHIP_ERROR GetManufacturingDate(uint16_t & year, uint8_t & month, uint8_t & day) override;
  virtual CHIP_ERROR GetHardwareVersion(uint16_t & hardwareVersion) override;
  virtual CHIP_ERROR GetHardwareVersionString(char * buf, size_t bufSize) override;
  virtual CHIP_ERROR GetRotatingDeviceIdUniqueId(MutableByteSpan & uniqueIdSpan) override;

  string mProductName; ///< model
  string mLabel; ///< label
  string mSerial; ///< serial number
  string mDSUID; ///< unique ID base

  uint16_t mVendorId; ///< vendor ID
  uint16_t mProductId; ///< product ID
};
