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


void P44mbrdDeviceInfoProvider::loadFromFactoryData(FactoryDataProviderPtr aFactoryDataProvider)
{
  // initialize from factory data provider
  assert(aFactoryDataProvider);
  mVendorId = aFactoryDataProvider->getUInt16("VID");
  mProductId = aFactoryDataProvider->getUInt16("PID");
  mVendorName = aFactoryDataProvider->getString("VENDORNAME");
  mHWVersion = aFactoryDataProvider->getUInt16("HWVERSION");
  if (mHWVersion==0) mHWVersion = 1; // 0 means no version set
  mHWVersionStr = aFactoryDataProvider->getString("HWVERSIONSTR");
  mPartNumber = aFactoryDataProvider->getString("PARTNUMBER");
  mProductURL = aFactoryDataProvider->getString("PRODUCTURL");
  // these may be overridded by data from bridge API:
  mProductName = aFactoryDataProvider->getString("PRODUCTNAME");
  mProductLabel = aFactoryDataProvider->getString("PRODUCTLABEL");
  mSerial = aFactoryDataProvider->getString("SERIALNO");
  mUID = aFactoryDataProvider->getString("UID");
  string ds = aFactoryDataProvider->getString("MANUFACTURINGDATE"); // ISO8601 first 8 digits YYYYMMDD
  if (ds.empty()) mManuYear = 0;
  else if (sscanf(ds.c_str(), "%4hu%2hhu%2hhu", &mManuYear, &mManuMonth, &mManuDay) < 3)
    mManuYear = mManuMonth = mManuDay = 0;
}


CHIP_ERROR P44mbrdDeviceInfoProvider::GetVendorName(char * buf, size_t bufSize)
{
  ReturnErrorCodeIf(bufSize < mVendorName.size(), CHIP_ERROR_BUFFER_TOO_SMALL);
  strncpy(buf, mVendorName.c_str(), bufSize);
  return CHIP_NO_ERROR;
}

CHIP_ERROR P44mbrdDeviceInfoProvider::GetVendorId(uint16_t & vendorId)
{
  vendorId = mVendorId;
  return CHIP_NO_ERROR;
}


CHIP_ERROR P44mbrdDeviceInfoProvider::GetProductName(char * buf, size_t bufSize)
{
  ReturnErrorCodeIf(bufSize < mProductName.size(), CHIP_ERROR_BUFFER_TOO_SMALL);
  strncpy(buf, mProductName.c_str(), bufSize);
  return CHIP_NO_ERROR;
}


CHIP_ERROR P44mbrdDeviceInfoProvider::GetProductId(uint16_t & productId)
{
  productId = mProductId;
  return CHIP_NO_ERROR;
}


CHIP_ERROR P44mbrdDeviceInfoProvider::GetHardwareVersion(uint16_t & hardwareVersion)
{
  hardwareVersion = mHWVersion;
  return CHIP_NO_ERROR;
}


CHIP_ERROR P44mbrdDeviceInfoProvider::GetHardwareVersionString(char * buf, size_t bufSize)
{
  if (mHWVersionStr.empty()) {
    uint16_t hwv;
    GetHardwareVersion(hwv);
    snprintf(buf, bufSize, "v%hd", hwv);
  }
  else {
    ReturnErrorCodeIf(bufSize < mHWVersionStr.size(), CHIP_ERROR_BUFFER_TOO_SMALL);
    strncpy(buf, mHWVersionStr.c_str(), bufSize);
  }
  return CHIP_NO_ERROR;
}


CHIP_ERROR P44mbrdDeviceInfoProvider::GetPartNumber(char * buf, size_t bufSize) {
  // Optional: Human readable Part Number (same ProductID may have different packages, colors etc.)
  if (mPartNumber.empty()) return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
  ReturnErrorCodeIf(bufSize < mPartNumber.size(), CHIP_ERROR_BUFFER_TOO_SMALL);
  strncpy(buf, mPartNumber.c_str(), bufSize);
  return CHIP_NO_ERROR;
}


CHIP_ERROR P44mbrdDeviceInfoProvider::GetProductURL(char * buf, size_t bufSize) {
  // Optional: Product Website URL
  if (mProductURL.empty()) return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
  ReturnErrorCodeIf(bufSize < mProductURL.size(), CHIP_ERROR_BUFFER_TOO_SMALL);
  strncpy(buf, mProductURL.c_str(), bufSize);
  return CHIP_NO_ERROR;
}


CHIP_ERROR P44mbrdDeviceInfoProvider::GetProductLabel(char * buf, size_t bufSize) {
  // Optional: More user-friendly version of ProductName, not containing VendorName
  if (mProductLabel.empty()) return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
  ReturnErrorCodeIf(bufSize < mProductLabel.size(), CHIP_ERROR_BUFFER_TOO_SMALL);
  strncpy(buf, mProductLabel.c_str(), bufSize);
  return CHIP_NO_ERROR;
}


CHIP_ERROR P44mbrdDeviceInfoProvider::GetSerialNumber(char * buf, size_t bufSize)
{
  ReturnErrorCodeIf(bufSize < mSerial.size(), CHIP_ERROR_BUFFER_TOO_SMALL);
  strncpy(buf, mSerial.c_str(), bufSize);
  return CHIP_NO_ERROR;
}


CHIP_ERROR P44mbrdDeviceInfoProvider::GetManufacturingDate(uint16_t & year, uint8_t & month, uint8_t & day)
{
  // Optional: when the node was manufactured, ISO8601 date in the first 8 chars
  if (mManuYear==0) return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
  year = mManuYear;
  month = mManuMonth;
  day = mManuDay;
  return CHIP_NO_ERROR;
}



CHIP_ERROR P44mbrdDeviceInfoProvider::GetRotatingDeviceIdUniqueId(MutableByteSpan & uniqueIdSpan)
{
  // TODO: implement actual rotation mechanism (maybe hash from UID, but see specs to see if that's allowed)
  ReturnErrorCodeIf(mUID.size() > uniqueIdSpan.size(), CHIP_ERROR_BUFFER_TOO_SMALL);
  memcpy(uniqueIdSpan.data(), mUID.c_str(), mUID.size());
  uniqueIdSpan.reduce_size(mUID.size());
  return CHIP_NO_ERROR;
}


