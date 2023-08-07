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


#include "matter_utils.h"

#include "utils.hpp"
#include <app/util/attribute-storage.h>

using namespace p44;

string attrString(const EndpointId aEndpointId, const ClusterId aClusterId, const AttributeId aAttributeId)
{
  const size_t maxAttrString = 512;
  uint8_t zclString[maxAttrString+2];
  EmberAfAttributeSearchRecord srch = { aEndpointId, aClusterId, aAttributeId };
  const EmberAfAttributeMetadata* mdP;
  EmberAfStatus status = emAfReadOrWriteAttribute(&srch, &mdP, zclString, sizeof(zclString), false);
  if (status==EMBER_ZCL_STATUS_SUCCESS) {
    if (emberAfIsStringAttributeType(mdP->attributeType)) {
      return string((char *)&zclString[1], emberAfStringLength(zclString));
    }
    else if (emberAfIsLongStringAttributeType(mdP->attributeType)) {
      return string((char *)&zclString[2], emberAfLongStringLength(zclString));
    }
    else {
      return string_format("<not a string, type=%d>", mdP->attributeType);
    }
  }
  return "<read error>";
}


void setAttrString(const EndpointId aEndpointId, const ClusterId aClusterId, const AttributeId aAttributeId, string aString, p44::AbbreviationStyle aAbbreviationStyle)
{
  const size_t maxStrLen = 512;
  uint8_t zclString[maxStrLen+2];
  EmberAfAttributeSearchRecord srch = { aEndpointId, aClusterId, aAttributeId };
  const EmberAfAttributeMetadata* mdP;
  EmberAfStatus status = emAfReadOrWriteAttribute(&srch, &mdP, nullptr, 0, false);
  if (status==EMBER_ZCL_STATUS_SUCCESS) {
    bool longString = emberAfIsLongStringAttributeType(mdP->attributeType);
    if (emberAfIsStringAttributeType(mdP->attributeType) || longString) {
      size_t netSz = mdP->size-(longString ? 2 : 1);
      if (netSz>maxStrLen) netSz = maxStrLen;
      abbreviate(aString, netSz, aAbbreviationStyle);
      if (longString) {
        memmove(zclString + 2, aString.data(), aString.size());
        zclString[0] = EMBER_LOW_BYTE(aString.size());
        zclString[1] = EMBER_HIGH_BYTE(aString.size());
      }
      else {
        memmove(zclString + 1, aString.data(), aString.size());
        zclString[0] = (uint8_t)aString.size();
      }
      emAfReadOrWriteAttribute(&srch, &mdP, zclString, 0, true);
    }
  }
}



