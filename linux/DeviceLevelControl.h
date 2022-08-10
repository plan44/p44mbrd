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

#include "deviceonoff.h"

using namespace chip;


class DeviceLevelControl : public DeviceOnOff
{
  typedef DeviceOnOff inherited;
public:

  DeviceLevelControl();

  virtual void initBridgedInfo(JsonObjectPtr aDeviceInfo) override;

  virtual void parseChannelStates(JsonObjectPtr aChannelStates, UpdateMode aUpdateMode) override;

  uint8_t currentLevel() { return mLevel; };
  bool updateCurrentLevel(uint8_t aAmount, int8_t aDirection, uint16_t aTransitionTimeDs, bool aWithOnOff, UpdateMode aUpdateMode);

  /// handler for external attribute read access
  virtual EmberAfStatus HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength) override;
  /// handler for external attribute write access
  virtual EmberAfStatus HandleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer) override;

  /// @name handlers for command implementations
  /// @{
  void moveToLevel(uint8_t aAmount, int8_t aDirection, uint16_t aTransitionTimeDs, bool aWithOnOff, uint8_t aOptionMask, uint8_t aOptionOverride);
  void move(uint8_t aMode, uint8_t aRate, bool aWithOnOff, uint8_t aOptionMask, uint8_t aOptionOverride);
  void stop(bool aWithOnOff, uint8_t aOptionMask, uint8_t aOptionOverride);
  void effect(bool aNewValue);

  /// @}

protected:
  virtual void changeOnOff_impl(bool aOn) override;

private:

  /// called to have the final leaf class declare the correct device type list
  virtual void finalizeDeviceDeclaration() override;

  uint8_t mLevel;

  bool shouldExecute(bool aWithOnOff, uint8_t aOptionMask, uint8_t aOptionOverride);
  uint8_t getOptions(uint8_t aOptionMask, uint8_t aOptionOverride);
  void dim(uint8_t aDirection, uint8_t aRate);
};
