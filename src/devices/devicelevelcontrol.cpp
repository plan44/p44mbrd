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

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 7


#include "device_impl.h" // include as first file!
#include "devicelevelcontrol.h"


// MARK: - LevelControl Device specific declarations

#define LEVEL_CONTROL_LIGHTING_MIN_LEVEL 1

ClusterId dimmableLightClusters[] = { LevelControl::Id };

// MARK: - DeviceLevelControl

using namespace LevelControl;

DeviceLevelControl::DeviceLevelControl(bool aLighting, LevelControlDelegate& aLevelControlDelegate, OnOffDelegate& aOnOffDelegate, IdentifyDelegate& aIdentifyDelegate, DeviceInfoDelegate& aDeviceInfoDelegate) :
  inherited(aLighting, aOnOffDelegate, aIdentifyDelegate, aDeviceInfoDelegate),
  mLevelControlDelegate(aLevelControlDelegate),
  // external attribute defaults
  mLevel(0)
{
  // - declare specific clusters
  useClusterTemplates(Span<ClusterId>(dimmableLightClusters));
}


string DeviceLevelControl::description()
{
  string s = inherited::description();
  string_format_append(s, "\n- currentLevel: %d", mLevel);
  return s;
}


void DeviceLevelControl::didGetInstalled()
{
  Attributes::FeatureMap::Set(endpointId(), to_underlying(LevelControl::Feature::kOnOff));
  Attributes::OnOffTransitionTime::Set(endpointId(), 5); // default is 0.5 Seconds for transitions (approx dS default)
  Attributes::OnLevel::Set(endpointId(), EMBER_AF_PLUGIN_LEVEL_CONTROL_MAXIMUM_LEVEL);
  Attributes::DefaultMoveRate::Set(endpointId(), EMBER_AF_PLUGIN_LEVEL_CONTROL_MAXIMUM_LEVEL/7); // default "recommendation" is 0.5 Seconds for transitions (approx dS default)
  Attributes::MinLevel::Set(endpointId(), mLighting ? LEVEL_CONTROL_LIGHTING_MIN_LEVEL : EMBER_AF_PLUGIN_LEVEL_CONTROL_MINIMUM_LEVEL);
  Attributes::MaxLevel::Set(endpointId(), EMBER_AF_PLUGIN_LEVEL_CONTROL_MAXIMUM_LEVEL);
  // call base class last
  inherited::didGetInstalled();
}



void DeviceLevelControl::changeOnOff_impl(bool aOn)
{
  // NOP because we control the output via level control
}


bool DeviceLevelControl::updateCurrentLevel(uint8_t aAmount, int8_t aDirection, uint16_t aTransitionTimeDs, bool aWithOnOff, UpdateMode aUpdateMode)
{
  uint8_t minlevel, maxlevel;
  Attributes::MinLevel::Get(endpointId(), &minlevel);
  Attributes::MaxLevel::Get(endpointId(), &maxlevel);

  // handle relative movement
  int level = aAmount;
  if (aDirection!=0) level = (int)mLevel + (aDirection>0 ? aAmount : -aAmount);
  if (level>maxlevel) level = maxlevel;
  if (level<minlevel) level = minlevel;
  // now move to given or calculated level
  if (level!=mLevel || aUpdateMode.Has(UpdateFlags::forced)) {
    OLOG(LOG_INFO, "setting level to %d (clipping to %d..%d) in %d00mS - %supdatemode=0x%x", aAmount, minlevel, maxlevel, aTransitionTimeDs, aWithOnOff ? "WITH OnOff, " : "", aUpdateMode.Raw());
    uint8_t previousLevel = mLevel;
    if ((previousLevel<=minlevel || aUpdateMode.Has(UpdateFlags::forced)) && level>minlevel) {
      // level is minimum and becomes non-minimum: also set OnOff when enabled
      if (aWithOnOff) updateOnOff(true, aUpdateMode);
    }
    else if (level<=minlevel) {
      // level is not minimum and should become minimum: prevent or clear OnOff
      if (aWithOnOff) updateOnOff(false, aUpdateMode);
      else if (previousLevel==minlevel) return false; // already at minimum: no change
      else level = minlevel; // set to minimum, but not to off
    }
    mLevel = static_cast<uint8_t>(level);
    if (aUpdateMode.Has(UpdateFlags::bridged)) {
      mLevelControlDelegate.setLevel(
        (double)(level-minlevel)/(maxlevel-minlevel)*100, // bridge side is always 0..100%, mapped to minlevel..maxlevel
        aTransitionTimeDs // in tenths of seconds, 0xFFFF for using hardware's default
      );
    }
    if (aUpdateMode.Has(UpdateFlags::matter)) {
      FOCUSOLOG("reporting currentLevel attribute change to matter");
      reportAttributeChange(LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
    }
    return true; // changed or forced
  }
  return false; // no change
}


uint16_t DeviceLevelControl::remainingTimeDS()
{
  MLMicroSeconds endOfTransition = mLevelControlDelegate.endOfLatestTransition();
  if (endOfTransition==Never) return 0;
  MLMicroSeconds tr = endOfTransition-MainLoop::now();
  if (tr<0) return 0; // no transition running
  return static_cast<uint16_t>(tr/(Second/10)); // return as decisecond
}


// MARK: callbacks for LevelControlDelegate implementations

void DeviceLevelControl::setDefaultOnLevel(double aLevelPercent)
{
  uint8_t minlevel, maxlevel;
  Attributes::MinLevel::Get(endpointId(), &minlevel);
  Attributes::MaxLevel::Get(endpointId(), &maxlevel);
  LevelControl::Attributes::OnLevel::Set(endpointId(), static_cast<uint8_t>(aLevelPercent/100*(maxlevel-minlevel))+minlevel);
}


bool DeviceLevelControl::updateLevel(double aLevelPercent, UpdateMode aUpdateMode)
{
  uint8_t minlevel, maxlevel;
  Attributes::MinLevel::Get(endpointId(), &minlevel);
  Attributes::MaxLevel::Get(endpointId(), &maxlevel);
  return updateCurrentLevel(static_cast<uint8_t>(aLevelPercent/100*(maxlevel-minlevel))+minlevel, 0, 0, false, aUpdateMode);
}


// MARK: levelControl cluster command implementation callbacks


bool DeviceLevelControl::shouldExecuteLevelChange(bool aWithOnOff, OptType aOptionMask, OptType aOptionOverride)
{
  // From 3.10.2.2.8.1 of ZCL7 document 14-0127-20j-zcl-ch-3-general.docx:
  //   "Command execution SHALL NOT continue beyond the Options processing if
  //    all of these criteria are true:
  //      - The command is one of the ‘without On/Off’ commands: Move, Move to
  //        Level, Stop, or Step.
  //      - The On/Off cluster exists on the same endpoint as this cluster.
  //      - The OnOff attribute of the On/Off cluster, on this endpoint, is 0x00
  //        (FALSE).
  //      - The value of the ExecuteIfOff bit is 0."
  if (aWithOnOff) {
    // command includes On/Off -> always execute
    return true;
  }
  if (isOn()) {
    // is already on -> execute anyway
    return true;
  }
  // now the options bit decides about executing or not
  chip::BitMask<chip::app::Clusters::LevelControl::LevelControlOptions> opts;
  Attributes::Options::Get(endpointId(), &opts);
  return (opts.Raw() & (uint8_t)(~aOptionMask.Raw())) | (aOptionOverride.Raw() & aOptionMask.Raw());
}



Status DeviceLevelControl::moveToLevel(uint8_t aAmount, int8_t aDirection, DataModel::Nullable<uint16_t> aTransitionTime, bool aWithOnOff, OptType aOptionMask,OptType aOptionOverride)
{
  Status status = Status::Success;

  if (aAmount > EMBER_AF_PLUGIN_LEVEL_CONTROL_MAXIMUM_LEVEL) {
    status = Status::InvalidCommand;
  }
  else if (shouldExecuteLevelChange(aWithOnOff, aOptionMask, aOptionOverride)) {
    // OnOff status and options do allow executing
    uint16_t transitionTime;
    if (aTransitionTime.IsNull()) {
      // default, use On/Off transition time attribute's value
      Attributes::OnOffTransitionTime::Get(endpointId(), &transitionTime);
    }
    else {
      transitionTime = aTransitionTime.Value();
    }
    bool wasOn = isOn();
    updateCurrentLevel(aAmount, aDirection, transitionTime, aWithOnOff, UpdateMode(UpdateFlags::bridged, UpdateFlags::matter));
    // Support for global scene for last state before getting switched off
    // - The GlobalSceneControl attribute is defined in order to prevent a second off command storing the
    //   all-devices-off situation as a global scene, and to prevent a second on command destroying the current
    //   settings by going back to the global scene.
    // - The GlobalSceneControl attribute SHALL be set to TRUE after the reception of a command which causes the OnOff
    //   attribute to be set to TRUE, such as a standard On command, a **Move to level (with on/off) command**, a Recall
    //   scene command or a On with recall global scene command.
    if (aWithOnOff && !wasOn && isOn() && mLighting) {
      OnOff::Attributes::GlobalSceneControl::Set(endpointId(), true);
    }
  }
  return status;
}


bool emberAfLevelControlClusterMoveToLevelCallback(
  CommandHandler * commandObj, const ConcreteCommandPath & commandPath,
  const Commands::MoveToLevel::DecodableType & commandData
)
{
  auto dev = DeviceEndpoints::getDevice<DeviceLevelControl>(commandPath.mEndpointId);
  if (!dev) return false;
  commandObj->AddStatus(commandPath, dev->moveToLevel(commandData.level, 0, commandData.transitionTime, false, commandData.optionsMask, commandData.optionsOverride));
  return true;
}

bool emberAfLevelControlClusterMoveToLevelWithOnOffCallback(
  CommandHandler * commandObj, const ConcreteCommandPath & commandPath,
  const Commands::MoveToLevelWithOnOff::DecodableType & commandData
)
{
  auto dev = DeviceEndpoints::getDevice<DeviceLevelControl>(commandPath.mEndpointId);
  if (!dev) return false;
  commandObj->AddStatus(commandPath, dev->moveToLevel(commandData.level, 0, commandData.transitionTime, true, 0, 0));
  return true;
}


bool emberAfLevelControlClusterStepCallback(
  app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
  const Commands::Step::DecodableType & commandData
)
{
  auto dev = DeviceEndpoints::getDevice<DeviceLevelControl>(commandPath.mEndpointId);
  if (!dev) return false;
  commandObj->AddStatus(commandPath, dev->moveToLevel(
    commandData.stepSize,
    commandData.stepMode==EMBER_ZCL_STEP_MODE_UP ? 1 : -1,
    commandData.transitionTime, false, commandData.optionsMask, commandData.optionsOverride
  ));
  return true;
}

bool emberAfLevelControlClusterStepWithOnOffCallback(
  app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
  const Commands::StepWithOnOff::DecodableType & commandData
)
{
  auto dev = DeviceEndpoints::getDevice<DeviceLevelControl>(commandPath.mEndpointId);
  if (!dev) return false;
  commandObj->AddStatus(commandPath, dev->moveToLevel(
    commandData.stepSize,
    commandData.stepMode==EMBER_ZCL_STEP_MODE_UP ? 1 : -1,
    commandData.transitionTime, true, 0, 0
  ));
  return true;
}



Status DeviceLevelControl::move(uint8_t aMode, DataModel::Nullable<uint8_t> aRate, bool aWithOnOff, OptType aOptionMask, OptType aOptionOverride)
{
  Status status = Status::Success;

  DataModel::Nullable<uint8_t> rate;
  if (aRate.IsNull()) {
    // use default rate
    Attributes::DefaultMoveRate::Get(endpointId(), rate);
  }
  else {
    rate = aRate;
  }
  if ((!rate.IsNull() && rate.Value()!=0) || shouldExecuteLevelChange(aWithOnOff, aOptionMask, aOptionOverride)) {
    switch (aMode) {
      case EMBER_ZCL_MOVE_MODE_UP:
        if (currentLevel()==0) {
          // start dimming from off level into on levels -> set onoff
          updateOnOff(true, UpdateMode(UpdateFlags::matter));
        }
        mLevelControlDelegate.dim(1, rate.Value());
        break;
      case EMBER_ZCL_MOVE_MODE_DOWN:
        mLevelControlDelegate.dim(-1, rate.Value());
        break;
      default:
        status = Status::InvalidCommand;
        break;
    }
  }
  return status;
}


bool emberAfLevelControlClusterMoveCallback(
  app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
  const Commands::Move::DecodableType & commandData
)
{
  auto dev = DeviceEndpoints::getDevice<DeviceLevelControl>(commandPath.mEndpointId);
  if (!dev) return false;
  commandObj->AddStatus(commandPath, dev->move(commandData.moveMode, commandData.rate, false, commandData.optionsMask, commandData.optionsOverride));
  return true;
}

bool emberAfLevelControlClusterMoveWithOnOffCallback(
  app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
  const Commands::MoveWithOnOff::DecodableType & commandData
)
{
  auto dev = DeviceEndpoints::getDevice<DeviceLevelControl>(commandPath.mEndpointId);
  if (!dev) return false;
  commandObj->AddStatus(commandPath, dev->move(commandData.moveMode, commandData.rate, true, 0, 0));
  return true;
}


Status DeviceLevelControl::stop(bool aWithOnOff, OptType aOptionMask, OptType aOptionOverride)
{
  Status status = Status::Success;

  if (shouldExecuteLevelChange(aWithOnOff, aOptionMask, aOptionOverride)) {
    mLevelControlDelegate.dim(0,0); // stop dimming
  }
  return status;
}



bool emberAfLevelControlClusterStopCallback(
  app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
  const Commands::Stop::DecodableType & commandData
)
{
  auto dev = DeviceEndpoints::getDevice<DeviceLevelControl>(commandPath.mEndpointId);
  if (!dev) return false;
  commandObj->AddStatus(commandPath, dev->stop(false, commandData.optionsMask, commandData.optionsOverride));
  return true;
}

bool emberAfLevelControlClusterStopWithOnOffCallback(
  app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
  const Commands::StopWithOnOff::DecodableType & commandData
)
{
  auto dev = DeviceEndpoints::getDevice<DeviceLevelControl>(commandPath.mEndpointId);
  if (!dev) return false;
  commandObj->AddStatus(commandPath, dev->stop(true, 0, 0));
  return true;
}


// MARK: levelControl cluster general callbacks

void DeviceLevelControl::effect(bool aTurnOn)
{
  EmberAfStatus status = EMBER_ZCL_STATUS_SUCCESS;

  // get the OnOffTransitionTime attribute.
  uint16_t transitionTime = 0xFFFF;
  if (emberAfContainsAttribute(endpointId(), LevelControl::Id, Attributes::OnOffTransitionTime::Id)) {
    status = Attributes::OnOffTransitionTime::Get(endpointId(), &transitionTime);
    if (status!=EMBER_ZCL_STATUS_SUCCESS) {
      transitionTime = 0xFFFF;
    }
  }
  // turn on or off
  OLOG(LOG_INFO, "levelcontrol effect: turnOn=%d", aTurnOn);
  if (aTurnOn) {
    // get the default onLevel
    app::DataModel::Nullable<uint8_t> targetOnLevel;
    if (emberAfContainsAttribute(endpointId(), LevelControl::Id, Attributes::OnLevel::Id)) {
      status = Attributes::OnLevel::Get(endpointId(), targetOnLevel);
      if (status!=EMBER_ZCL_STATUS_SUCCESS || targetOnLevel.IsNull()) {
        // no OnLevel value, use currentlevel
        targetOnLevel.SetNonNull(currentLevel());
      }
    }
    if (targetOnLevel.IsNull()) targetOnLevel.SetNonNull(static_cast<uint8_t>(EMBER_AF_PLUGIN_LEVEL_CONTROL_MAXIMUM_LEVEL));
    updateCurrentLevel(targetOnLevel.Value(), 0, transitionTime, true, UpdateMode(UpdateFlags::bridged, UpdateFlags::matter));
  }
  else {
    updateCurrentLevel(0, 0, transitionTime, true, UpdateMode(UpdateFlags::bridged, UpdateFlags::matter));
  }
}


void emberAfOnOffClusterLevelControlEffectCallback(EndpointId endpoint, bool newValue)
{
  auto dev = DeviceEndpoints::getDevice<DeviceLevelControl>(endpoint);
  if (!dev) return;
  dev->effect(newValue);
}



void emberAfLevelControlClusterServerInitCallback(EndpointId endpoint)
{
  // NOP for now
}

void MatterLevelControlPluginServerInitCallback()
{
  // NOP for now
}


void MatterLevelControlClusterServerShutdownCallback(EndpointId endpoint)
{
  // NOP for now
}



// Note: copied from original levelcontrol implementation, needed by on-off
bool LevelControlHasFeature(EndpointId endpoint, LevelControl::Feature feature)
{
  bool success;
  uint32_t featureMap;
  success = (Attributes::FeatureMap::Get(endpoint, &featureMap) == EMBER_ZCL_STATUS_SUCCESS);

  return success ? ((featureMap & to_underlying(feature)) != 0) : false;
}


// MARK: attribute access

EmberAfStatus DeviceLevelControl::handleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength)
{
  if (clusterId==LevelControl::Id) {
    if (attributeId == LevelControl::Attributes::CurrentLevel::Id) {
      return getAttr(buffer, maxReadLength, currentLevel());
    }
    if (attributeId == LevelControl::Attributes::RemainingTime::Id) {
      return getAttr(buffer, maxReadLength, remainingTimeDS());
    }
  }
  // let base class try
  return inherited::handleReadAttribute(clusterId, attributeId, buffer, maxReadLength);
}


EmberAfStatus DeviceLevelControl::handleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer)
{
  if (clusterId==LevelControl::Id) {
    /* none */
  }
  // let base class try
  return inherited::handleWriteAttribute(clusterId, attributeId, buffer);
}


// MARK: - DeviceDimmableLight

const EmberAfDeviceType gDimmableLightTypes[] = {
  { DEVICE_TYPE_MA_DIMMABLE_LIGHT, DEVICE_VERSION_DEFAULT },
  { DEVICE_TYPE_MA_BRIDGED_DEVICE, DEVICE_VERSION_DEFAULT }
};

void DeviceDimmableLight::finalizeDeviceDeclaration()
{
  finalizeDeviceDeclarationWithTypes(Span<const EmberAfDeviceType>(gDimmableLightTypes));
}


// MARK: - DeviceDimmablePluginUnit

const EmberAfDeviceType gDimmablePluginTypes[] = {
  { DEVICE_TYPE_MA_DIMMABLE_PLUGIN_UNIT, DEVICE_VERSION_DEFAULT },
  { DEVICE_TYPE_MA_BRIDGED_DEVICE, DEVICE_VERSION_DEFAULT }
};

void DeviceDimmablePluginUnit::finalizeDeviceDeclaration()
{
  finalizeDeviceDeclarationWithTypes(Span<const EmberAfDeviceType>(gDimmablePluginTypes));
}

