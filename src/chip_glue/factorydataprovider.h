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

#include "p44mbrd_common.h"

using namespace p44;

class FactoryDataProvider : public P44LoggingObj
{
public:

  virtual string logContextPrefix() override { return "FactoryDataProvider"; }

  /// @brief get unsigned integer data (such as PID, VID etc.) item from the provider
  /// @param aKey the key for the item, case insensitiv
  /// @return contents of aKey, 0 if aKey does not exist
  virtual uint32_t getUInt32(const char* aKey) = 0;

  /// @return contents of aKey casted to uint16, 0 if aKey does not exist
  uint16_t getUInt16(const char* aKey);

  /// @return contents of aKey casted to uint8, 0 if aKey does not exist
  uint8_t getUInt8(const char* aKey);

  /// @brief get string data item from the provider
  /// @param aKey the key for the item, case insensitiv
  /// @return contents of aKey, empty string if aKey does not exist
  virtual string getString(const char* aKey) = 0;

  bool getOptionalString(const char* aKey, string& aString);

  /// @brief get binary data string item from the provider
  /// @param aKey the key for the item, case insensitiv
  /// @return contents of aKey, empty string if aKey does not exist
  virtual string getBytes(const char* aKey) = 0;

};
typedef boost::intrusive_ptr<FactoryDataProvider> FactoryDataProviderPtr;


class FileBasedFactoryDataProvider : public FactoryDataProvider
{
  typedef std::map<const string, string, lessStrucmp> DataMap;

  DataMap mDataItems;

public:

  /// create factory data provider populated with data from files
  /// @param aFactoryDataResourcePaths list of one or multiple colon separated resource paths
  ///   (expanded via Application::resourcePath()) for loading the factory data resources
  ///   from a key=value data file.
  ///   Files are scanned in the order given in this parameter, overriding already
  ///   populated keys, so most specific data must be in the last path specified
  /// @param aResourcePrefix the prefix used with Application::resourcePath()
  FileBasedFactoryDataProvider(const string aFactoryDataResourcePaths, const string aResourcePrefix = "");

  virtual uint32_t getUInt32(const char* aKey) override;
  virtual string getString(const char* aKey) override;
  virtual string getBytes(const char* aKey) override;

private:

  bool getItem(const char* aKey, string &aItem);

};
