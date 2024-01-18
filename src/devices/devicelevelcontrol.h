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
/// @note this delegate is not only used for actual LevelControl cluster based devices, but
///    some semantically similar ones like FanControl
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

  /// @return the time when the latest started transition will end, in Mainloop::now() time
  virtual MLMicroSeconds endOfLatestTransition() = 0;

};


/// @brief interface class for Levelcontrol and semantically similar devices' implementations
class LevelControlImplementationInterface
{

public:

  virtual ~LevelControlImplementationInterface() = default;

  /// @brief set the default on level
  /// @note this should be called at device setup, before the device goes operational
  virtual void setDefaultOnLevel(double aLevelPercent) = 0;

  /// @brief update the current level (when bridged device reports it)
  virtual bool updateLevel(double aLevelPercent, Device::UpdateMode aUpdateMode) = 0;
};




/// device actually based on matter LevelControl cluster
class DeviceLevelControl : public DeviceOnOff, public LevelControlImplementationInterface
{
  typedef DeviceOnOff inherited;

  LevelControlDelegate& mLevelControlDelegate;

protected:

  typedef chip::BitMask<LevelControl::LevelControlOptions> OptType;

public:

  DeviceLevelControl(bool aLighting, LevelControlDelegate& aLevelControlDelegate, OnOffDelegate& aOnOffDelegate, IdentifyDelegate& aIdentifyDelegate, DeviceInfoDelegate& aDeviceInfoDelegate);

  virtual void didGetInstalled() override;

  virtual string description() override;

  uint8_t currentLevel() { return mLevel; };
  bool updateCurrentLevel(uint8_t aAmount, int8_t aDirection, uint16_t aTransitionTimeDs, bool aWithOnOff, UpdateMode aUpdateMode);


  /// @name LevelControlImplementationInterface
  /// @{
  virtual void setDefaultOnLevel(double aLevelPercent) override;
  virtual bool updateLevel(double aLevelPercent, Device::UpdateMode aUpdateMode) override;
  /// @}

  /// @name handlers for external attribute implementation
  /// @{
  virtual EmberAfStatus handleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength) override;
  virtual EmberAfStatus handleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer) override;
  /// @}

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

  uint16_t remainingTimeDS(); ///< return remaining execution (i.e. transition) time of current command
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


// MARK: - Scene control
// TODO: Modularize level-control server
// - for now this is extracted from app/clusters/level-control as the original
//   cluster does not allow overriding the lower level (actual transition stepping) parts of
//   the cluster, which are NOT suitable for remote hardware control in bridging apps

#include <app/clusters/scenes-server/SceneTable.h>
#include <app/clusters/scenes-server/scenes-server.h>

namespace LevelControlServer {

  chip::scenes::SceneHandler * GetSceneHandler();

} // namespace LevelControlServer


