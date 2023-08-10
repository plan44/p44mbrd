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


#include "factorydataprovider.h"

#include "application.hpp"

uint16_t FactoryDataProvider::getUInt16(const char* aKey)
{
  return static_cast<uint16_t>(getUInt32(aKey));
}


uint8_t FactoryDataProvider::getUInt8(const char* aKey)
{
  return static_cast<uint8_t>(getUInt32(aKey));
}


bool FactoryDataProvider::getOptionalString(const char* aKey, string& aString)
{
  string s = getString(aKey);
  if (s.empty()) return false;
  aString=s;
  return true;
}


FileBasedFactoryDataProvider::FileBasedFactoryDataProvider(const string aFactoryDataResourcePaths, const string aResourcePrefix)
{
  mDataItems.clear();
  const char* p = aFactoryDataResourcePaths.c_str();
  string fname;
  while (nextPart(p, fname, ':')) {
    // scan this file
    string fn = Application::sharedApplication()->resourcePath(fname, aResourcePrefix);
    FILE* f = fopen(fn.c_str(), "r");
    if (f) {
      string line;
      string data;
      string key;
      while (string_fgetline(f, line)) {
        if (line.empty()) continue; // skip empty lines
        if (line[0]=='#') continue; // skip comment lines
        if (line[0]==' ') {
          data += line; // append to previous item's data
        }
        else {
          // no continuation, store previous item
          if (!key.empty()) mDataItems[key] = data;
          key.clear();
        }
        // start new item
        keyAndValue(line, key, data, '=');
      }
      if (!key.empty()) mDataItems[key] = data; // store last item
    }
  }
}

bool FileBasedFactoryDataProvider::getItem(const char* aKey, string &aItem)
{
  DataMap::iterator pos = mDataItems.find(aKey);
  if (pos==mDataItems.end()) return false;
  aItem = pos->second;
  return true;
}



uint32_t FileBasedFactoryDataProvider::getUInt32(const char* aKey)
{
  string s;
  if (getItem(aKey, s)) {
    if (strncmp(s.c_str(), "0x", 2)==0) {
      return (uint32_t)strtoll(s.c_str(), NULL, 0);
    }
    else {
      return (uint32_t)strtoll(s.c_str(), NULL, 10);
    }
  }
  return 0;
}


string FileBasedFactoryDataProvider::getString(const char* aKey)
{
  string s;
  getItem(aKey, s);
  return s;
}


string FileBasedFactoryDataProvider::getBytes(const char* aKey)
{
  string s;
  getItem(aKey, s);
  return p44::hexToBinaryString(s.c_str(), true);
}

