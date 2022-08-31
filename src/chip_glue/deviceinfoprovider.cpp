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

#include "deviceinfoprovider.h"

#define P44_VENDOR_NAME "plan44.ch"

CHIP_ERROR P44DeviceInfoProvider::GetVendorName(char * buf, size_t bufSize)
{
  ReturnErrorCodeIf(bufSize < sizeof(P44_VENDOR_NAME), CHIP_ERROR_BUFFER_TOO_SMALL);
  strcpy(buf, P44_VENDOR_NAME);
  return CHIP_NO_ERROR;
}

CHIP_ERROR P44DeviceInfoProvider::GetVendorId(uint16_t & vendorId)
{
  vendorId = 0xFFF1; // FIXME: testing
  return CHIP_NO_ERROR;
}


CHIP_ERROR P44DeviceInfoProvider::GetProductName(char * buf, size_t bufSize)
{
  ReturnErrorCodeIf(bufSize < mProductName.size(), CHIP_ERROR_BUFFER_TOO_SMALL);
  strcpy(buf, mProductName.c_str());
  return CHIP_NO_ERROR;
}


CHIP_ERROR P44DeviceInfoProvider::GetProductId(uint16_t & productId)
{
  productId = 0x8002; // FIXME: testing
  return CHIP_NO_ERROR;
}


CHIP_ERROR P44DeviceInfoProvider::GetSerialNumber(char * buf, size_t bufSize)
{
  ReturnErrorCodeIf(bufSize < mSerial.size(), CHIP_ERROR_BUFFER_TOO_SMALL);
  strcpy(buf, mSerial.c_str());
  return CHIP_NO_ERROR;
}


CHIP_ERROR P44DeviceInfoProvider::GetManufacturingDate(uint16_t & year, uint8_t & month, uint8_t & day)
{
  // FIXME: testing
  year = 2022;
  month = 8;
  day = 11;
  return CHIP_NO_ERROR;
}


CHIP_ERROR P44DeviceInfoProvider::GetHardwareVersion(uint16_t & hardwareVersion)
{
  hardwareVersion = 0; // FIXME: testing
  return CHIP_NO_ERROR;
}


CHIP_ERROR P44DeviceInfoProvider::GetHardwareVersionString(char * buf, size_t bufSize)
{
  *buf = 0; // FIXME: testing, no version
  return CHIP_NO_ERROR;
}


CHIP_ERROR P44DeviceInfoProvider::GetRotatingDeviceIdUniqueId(MutableByteSpan & uniqueIdSpan)
{
  ReturnErrorCodeIf(mDSUID.size() > uniqueIdSpan.size(), CHIP_ERROR_BUFFER_TOO_SMALL);
  memcpy(uniqueIdSpan.data(), mDSUID.c_str(), mDSUID.size());
  uniqueIdSpan.reduce_size(mDSUID.size());
  return CHIP_NO_ERROR;
}


