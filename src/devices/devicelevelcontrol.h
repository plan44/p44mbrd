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
using namespace app;
using namespace Clusters;


/// @brief delegate for controlling a output level in a device (such as light, or fan speed etc.)
class LevelControlDelegate
{
public:

  virtual ~LevelControlDelegate() = default;

  /// Set new output level for device
  /// @param aNewLevel new level to set [0..100]
  /// @param aTransitionTimeDS transition time in tenths of a second, 0: immediately, 0xFFFF: use hardware recommended default
  virtual void setLevel(double aNewLevel, uint16_t aTransitionTimeDS) = 0;

  /// Set new output level for device
  /// @param aDirection >0: start dimming up, <0: start dimming down, 0: stop dimming
  /// @param aRate rate of change, 0xFF = use default
  virtual void dim(int8_t aDirection, uint8_t aRate) = 0;

  /// @return recommended transition time for this device in tenths of seconds
  virtual uint16_t recommendedTransitionTimeDS() = 0;

  /// @return the time when the latest started transition will end, in Mainloop::now() time
  virtual MLMicroSeconds endOfLatestTransition() = 0;

};


class DeviceLevelControl : public DeviceOnOff
{
  typedef DeviceOnOff inherited;

  LevelControlDelegate& mLevelControlDelegate;

protected:

  typedef chip::BitMask<LevelControl::LevelControlOptions> OptType;

public:

  DeviceLevelControl(bool aLighting, LevelControlDelegate& aLevelControlDelegate, OnOffDelegate& aOnOffDelegate, IdentifyDelegate& aIdentifyDelegate, DeviceInfoDelegate& aDeviceInfoDelegate);

  virtual void willBeInstalled() override;
  virtual string description() override;

  uint8_t currentLevel() { return mLevel; };
  bool updateCurrentLevel(uint8_t aAmount, int8_t aDirection, uint16_t aTransitionTimeDs, bool aWithOnOff, UpdateMode aUpdateMode);

  /// @name callbacks for LevelControlDelegate implementations
  /// @{

  /// @brief set the default on level
  /// @note this should be called at device setup, before the device goes operational
  void setDefaultOnLevel(double aLevelPercent);

  /// @brief update the current level (when bridged device reports it)
  bool updateLevel(double aLevelPercent, UpdateMode aUpdateMode);

  /// @}

  /// handler for external attribute read access
  virtual EmberAfStatus HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength) override;
  /// handler for external attribute write access
  virtual EmberAfStatus HandleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer) override;

  /// @name handlers for command implementations
  /// @{
  Status moveToLevel(uint8_t aAmount, int8_t aDirection, DataModel::Nullable<uint16_t> aTransitionTimeDs, bool aWithOnOff, OptType aOptionMask, OptType aOptionOverride);
  Status move(uint8_t aMode, DataModel::Nullable<uint8_t> aRate, bool aWithOnOff, OptType aOptionMask, OptType aOptionOverride);
  Status stop(bool aWithOnOff, OptType aOptionMask, OptType aOptionOverride);
  void effect(bool aNewValue);

  /// @}

protected:

  virtual void changeOnOff_impl(bool aOn) override;

private:

  // attributes
  uint8_t mLevel;
  uint8_t mOnLevel;
  uint8_t mLevelControlOptions;
  uint16_t mOnOffTransitionTimeDS;
  uint8_t mDefaultMoveRateUnitsPerS;

  uint16_t remainingTimeDS(); ///< return remaining execution (i.e. transition) time of current command
  uint8_t minLevel(); ///< return minimum level (different for generic and lighting cases)
  uint8_t maxLevel(); ///< return maximum level
  bool shouldExecuteLevelChange(bool aWithOnOff, OptType aOptionMask, OptType aOptionOverride);
};



class DeviceDimmableLight : public DeviceLevelControl
{
  typedef DeviceLevelControl inherited;
public:

  DeviceDimmableLight(LevelControlDelegate& aLevelControlDelegate, OnOffDelegate& aOnOffDelegate, IdentifyDelegate& aIdentifyDelegate, DeviceInfoDelegate& aDeviceInfoDelegate) :
    inherited(true, aLevelControlDelegate, aOnOffDelegate, aIdentifyDelegate, aDeviceInfoDelegate)
  {};

  virtual const char *deviceType() override { return "dimmable light"; }

protected:

  /// called to have the final leaf class declare the correct device type list
  virtual void finalizeDeviceDeclaration() override;

};


class DeviceDimmablePluginUnit : public DeviceLevelControl
{
  typedef DeviceLevelControl inherited;
public:

  DeviceDimmablePluginUnit(LevelControlDelegate& aLevelControlDelegate, OnOffDelegate& aOnOffDelegate, IdentifyDelegate& aIdentifyDelegate, DeviceInfoDelegate& aDeviceInfoDelegate) :
    inherited(false, aLevelControlDelegate, aOnOffDelegate, aIdentifyDelegate, aDeviceInfoDelegate)
  {};

  virtual const char *deviceType() override { return "dimmable plug-in unit"; }

protected:

  /// called to have the final leaf class declare the correct device type list
  virtual void finalizeDeviceDeclaration() override;

};
