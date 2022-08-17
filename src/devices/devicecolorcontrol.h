//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2022 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "devicelevelcontrol.h"

using namespace chip;

class DeviceColorControl : public DeviceLevelControl
{
  typedef DeviceLevelControl inherited;
public:

  /// @note: enum values are from matter specs
  typedef enum {
    colormode_hs = 0,
    colormode_xy = 1,
    colormode_ct = 2,
    colormode_unknown = 0xFF
  } ColorMode;

  DeviceColorControl(bool aCTOnly);

  virtual void logStatus(const char *aReason = NULL) override;

  virtual void initBridgedInfo(JsonObjectPtr aDeviceInfo) override;

  virtual void parseChannelStates(JsonObjectPtr aChannelStates, UpdateMode aUpdateMode) override;

  ColorMode currentColorMode() { return mColorMode; };
  uint8_t currentHue() { return mHue; };
  uint8_t currentSaturation() { return mSaturation; };
  uint16_t currentColortemp() { return mColorTemp; };
  uint16_t currentX() { return mX; };
  uint16_t currentY() { return mX; };

  bool updateCurrentColorMode(ColorMode aColorMode, UpdateMode aUpdateMode);
  bool updateCurrentHue(uint8_t aHue, UpdateMode aUpdateMode);
  bool updateCurrentSaturation(uint8_t aSaturation, UpdateMode aUpdateMode);
  bool updateCurrentColortemp(uint16_t aColortemp, UpdateMode aUpdateMode);
  bool updateCurrentX(uint16_t aX, UpdateMode aUpdateMode);
  bool updateCurrentY(uint16_t aY, UpdateMode aUpdateMode);

  /// handler for external attribute read access
  virtual EmberAfStatus HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength) override;
  /// handler for external attribute write access
  virtual EmberAfStatus HandleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer) override;

private:

  /// called to have the final leaf class declare the correct device type list
  virtual void finalizeDeviceDeclaration() override;

  bool mCtOnly;

  ColorMode mColorMode;
  uint8_t mHue;
  uint8_t mSaturation;
  uint16_t mColorTemp;
  uint16_t mX;
  uint16_t mY;
};

