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

  DeviceColorControl(const std::string aDSUID, bool aCTOnly);

  uint8_t currentHue() { return mHue; };
  uint8_t currentSaturation() { return mSaturation; };
  uint16_t currentColortemp() { return mColorTemp; };

  bool setCurrentHue(uint8_t aHue);
  bool setCurrentSaturation(uint8_t aSaturation);
  bool setCurrentColortemp(uint8_t aColortemp);

  /// handler for external attribute read access
  virtual EmberAfStatus HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength) override;
  /// handler for external attribute write access
  virtual EmberAfStatus HandleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer) override;

private:

  /// called to have the final leaf class declare the correct device type list
  virtual void finalizeDeviceDeclaration() override;

  bool mCtOnly;
  uint8_t mColorMode; // 0=HS, 1=XY, 2=Colortemp
  uint8_t mHue;
  uint8_t mSaturation;
  uint16_t mColorTemp;
};

