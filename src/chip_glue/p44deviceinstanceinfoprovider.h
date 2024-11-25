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

#include "matter_common.h"
#include "p44mbrd_common.h"

#include "factorydataprovider.h"

#include <platform/DeviceInstanceInfoProvider.h>

using namespace chip;
using namespace std;

class P44DeviceInstanceInfoProvider : public DeviceLayer::DeviceInstanceInfoProvider
{

  uint16_t mVendorId; ///< Matter vendor ID
  uint16_t mProductId; ///< Matter product ID
  string mVendorName; ///< vendor name
  uint16_t mHWVersion; ///< hardware version
  string mHWVersionStr; ///< user friendly hardware version
  string mPartNumber; ///< part number
  string mProductURL; ///< product URL
  uint16_t mManuYear; ///< manufacturing year
  uint8_t mManuMonth; ///< manufacturing month
  uint8_t mManuDay; ///< manufacturing day

public:

  virtual ~P44DeviceInstanceInfoProvider() = default;

  void loadFromFactoryData(FactoryDataProviderPtr aFactoryDataProvider);

  virtual CHIP_ERROR GetVendorName(char * buf, size_t bufSize) override; ///< Human readable vendor name
  virtual CHIP_ERROR GetVendorId(uint16_t & vendorId) override; ///< The csa-iot assigned Vendor ID
  virtual CHIP_ERROR GetProductName(char * buf, size_t bufSize) override; ///< Human readable model name
  virtual CHIP_ERROR GetProductId(uint16_t & productId) override; ///< The matter Product ID
  virtual CHIP_ERROR GetHardwareVersion(uint16_t & hardwareVersion) override; ///< Vendor defined Hardware version number
  virtual CHIP_ERROR GetHardwareVersionString(char * buf, size_t bufSize) override; ///< More user friendly string for hardware version

  virtual CHIP_ERROR GetPartNumber(char * buf, size_t bufSize) override; ///< Optional: Human readable Part Number (same ProductID may have different packages, colors etc.)
  virtual CHIP_ERROR GetProductURL(char * buf, size_t bufSize) override; ///< Optional: Product Website URL
  virtual CHIP_ERROR GetProductLabel(char * buf, size_t bufSize) override; ///< Optional: More user-friendly version of ProductName, not containing VendorName
  virtual CHIP_ERROR GetSerialNumber(char * buf, size_t bufSize) override; ///< Optional: human readable, displayable
  virtual CHIP_ERROR GetManufacturingDate(uint16_t & year, uint8_t & month, uint8_t & day) override; // Optional: when the node was manufactured, ISO8601 date in the first 8 chars

  virtual CHIP_ERROR GetRotatingDeviceIdUniqueId(MutableByteSpan & uniqueIdSpan) override;

  /// @name user facing product information possibly obtained via bridge interfaces
  /// @{

  string mProductName; ///< model
  string mProductLabel; ///< product label (more user friendly product name)
  string mSerial; ///< serial number
  string mUID; ///< unique ID base

  /// @}
};
