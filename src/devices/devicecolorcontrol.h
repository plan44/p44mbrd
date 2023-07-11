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
  enum {
    colormode_hs = 0,
    colormode_xy = 1,
    colormode_ct = 2,
    colormode_EnhancedHs = 3, // TODO: not yet implemented, is optional
    colormode_unknown = 0xFF
  };
  typedef uint8_t ColorMode;

  DeviceColorControl(bool aCTOnly);

  virtual const char *deviceType() override { return "color-control"; }

  virtual string description() override;

  ColorMode currentColorMode() { return mColorMode; };
  uint8_t currentHue() { return mHue; };
  uint8_t currentSaturation() { return mSaturation; };
  uint16_t currentColortemp() { return mColorTemp; };
  uint16_t currentX() { return mX; };
  uint16_t currentY() { return mX; };

  bool updateCurrentColorMode(ColorMode aColorMode, UpdateMode aUpdateMode, uint16_t aTransitionTimeDS);
  bool updateCurrentHue(uint8_t aHue, UpdateMode aUpdateMode, uint16_t aTransitionTimeDS);
  bool updateCurrentSaturation(uint8_t aSaturation, UpdateMode aUpdateMode, uint16_t aTransitionTimeDS);
  bool updateCurrentColortemp(uint16_t aColortemp, UpdateMode aUpdateMode, uint16_t aTransitionTimeDS);
  bool updateCurrentX(uint16_t aX, UpdateMode aUpdateMode, uint16_t aTransitionTimeDS);
  bool updateCurrentY(uint16_t aY, UpdateMode aUpdateMode, uint16_t aTransitionTimeDS);

  bool shouldExecuteColorChange(OptType aOptionMask, OptType aOptionOverride);

  /// handler for external attribute read access
  virtual EmberAfStatus HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength) override;
  /// handler for external attribute write access
  virtual EmberAfStatus HandleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer) override;

private:

  /// called to have the final leaf class declare the correct device type list
  virtual void finalizeDeviceDeclaration() override;

  uint8_t mColorControlOptions;

  bool mCtOnly;

  ColorMode mColorMode;
  uint8_t mHue;
  uint8_t mSaturation;
  uint16_t mColorTemp;
  uint16_t mX;
  uint16_t mY;
  uint16_t mStartupColorTemp;
};

