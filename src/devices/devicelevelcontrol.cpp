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
#define ALWAYS_DEBUG 1
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 6


#include "device_impl.h" // include as first file!
#include "devicelevelcontrol.h"


// MARK: - LevelControl Device specific declarations

// REVISION DEFINITIONS:
// TODO: move these to a better place, probably into the devices that actually handle them, or
//   try to extract them from ZAP-generated defs
// =================================================================================

#define ZCL_LEVEL_CONTROL_CLUSTER_REVISION (5u)
#define ZCL_LEVEL_CONTROL_CLUSTER_FEATURE_MAP (EMBER_AF_LEVEL_CONTROL_FEATURE_ON_OFF)

#define LEVEL_CONTROL_LIGHTING_MIN_LEVEL 1

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(levelControlAttrs)
  // DECLARE_DYNAMIC_ATTRIBUTE(attId, attType, attSizeBytes, attrMask)
  DECLARE_DYNAMIC_ATTRIBUTE(ZCL_CURRENT_LEVEL_ATTRIBUTE_ID, INT8U, 1, 0), /* current level */
  DECLARE_DYNAMIC_ATTRIBUTE(ZCL_ON_OFF_TRANSITION_TIME_ATTRIBUTE_ID, INT16U, 2, ZAP_ATTRIBUTE_MASK(WRITABLE)), /* onoff transition time */
  DECLARE_DYNAMIC_ATTRIBUTE(ZCL_ON_LEVEL_ATTRIBUTE_ID, INT8U, 1, ZAP_ATTRIBUTE_MASK(WRITABLE)), /* level for fully on */
  DECLARE_DYNAMIC_ATTRIBUTE(ZCL_OPTIONS_ATTRIBUTE_ID, BITMAP8, 1, ZAP_ATTRIBUTE_MASK(WRITABLE)), /* options */
  DECLARE_DYNAMIC_ATTRIBUTE(ZCL_DEFAULT_MOVE_RATE_ATTRIBUTE_ID, INT8U, 1, ZAP_ATTRIBUTE_MASK(WRITABLE)), /* default move/dim rate */
  DECLARE_DYNAMIC_ATTRIBUTE(ZCL_MAXIMUM_LEVEL_ATTRIBUTE_ID, INT8U, 1, 0), /* max level */
  DECLARE_DYNAMIC_ATTRIBUTE(ZCL_MINIMUM_LEVEL_ATTRIBUTE_ID, INT8U, 1, 0), /* min level */
  DECLARE_DYNAMIC_ATTRIBUTE(ZCL_FEATURE_MAP_SERVER_ATTRIBUTE_ID, BITMAP32, 4, 0),     /* feature map */
DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

constexpr CommandId levelControlIncomingCommands[] = {
  app::Clusters::LevelControl::Commands::MoveToLevel::Id,
  app::Clusters::LevelControl::Commands::Move::Id,
  app::Clusters::LevelControl::Commands::Step::Id,
  app::Clusters::LevelControl::Commands::Stop::Id,
  app::Clusters::LevelControl::Commands::MoveToLevelWithOnOff::Id,
  app::Clusters::LevelControl::Commands::MoveWithOnOff::Id,
  app::Clusters::LevelControl::Commands::StepWithOnOff::Id,
  app::Clusters::LevelControl::Commands::StopWithOnOff::Id,
//  app::Clusters::LevelControl::Commands::MoveToClosestFrequency::Id,
  kInvalidCommandId,
};

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(dimmableLightClusters)
  DECLARE_DYNAMIC_CLUSTER(ZCL_LEVEL_CONTROL_CLUSTER_ID, levelControlAttrs, levelControlIncomingCommands, nullptr),
DECLARE_DYNAMIC_CLUSTER_LIST_END;

// MARK: - DeviceLevelControl

DeviceLevelControl::DeviceLevelControl(bool aLighting) :
  inherited(aLighting),
  // Attribute defaults
  mLevel(0),
  mOnLevel(EMBER_AF_PLUGIN_LEVEL_CONTROL_MAXIMUM_LEVEL), // will be updated from ROOM_ON value when available
  mLevelControlOptions(0), // No default options (see EmberAfLevelControlOptions for choices)
  mOnOffTransitionTimeDS(5), // default is 0.5 Seconds for transitions (approx dS default)
  mDefaultMoveRateUnitsPerS(EMBER_AF_PLUGIN_LEVEL_CONTROL_MAXIMUM_LEVEL/7), // default "recommendation" is 0.5 Seconds for transitions (approx dS default)
  mRecommendedTransitionTimeDS(5), // FIXME: just dS default of full range in 7 seconds
  mEndOfLatestTransition(Never)
{
  // - declare specific clusters
  addClusterDeclarations(Span<EmberAfCluster>(dimmableLightClusters));
}


void DeviceLevelControl::initBridgedInfo(JsonObjectPtr aDeviceInfo, JsonObjectPtr aDeviceComponentInfo, const char* aInputType, const char* aInputId)
{
  inherited::initBridgedInfo(aDeviceInfo, aDeviceComponentInfo, aInputType, aInputId);
  // level coontrol devices should know the recommended transition time for the output
  JsonObjectPtr o;
  JsonObjectPtr o2;
  if (aDeviceInfo->get("outputDescription", o)) {
    if (o->get("x-p44-recommendedTransitionTime", o2)) {
      // adjust current
      mRecommendedTransitionTimeDS = static_cast<uint16_t>(o2->doubleValue()/0.1); // how many 1/10 seconds?
      mOnOffTransitionTimeDS = mRecommendedTransitionTimeDS; // also use as on/off default
    }
  }
  // get default on level (level of preset1 scene)
  if (aDeviceInfo->get("scenes", o)) {
    if (o->get("5", o2)) {
      if (o2->get("channels", o2)) {
        if (o2->get(mDefaultChannelId.c_str(), o2)) {
          if (o2->get("value", o2)) {
            mOnLevel = static_cast<uint8_t>(o2->doubleValue()/100*(maxLevel()-minLevel()))+minLevel();
          }
        }
      }
    }
  }
}


void DeviceLevelControl::parseChannelStates(JsonObjectPtr aChannelStates, UpdateMode aUpdateMode)
{
  // OnOff just sets on/off state when brightness>0
  inherited::parseChannelStates(aChannelStates, aUpdateMode);
  // init level
  JsonObjectPtr o;
  if (aChannelStates->get(mDefaultChannelId.c_str(), o)) {
    JsonObjectPtr vo;
    if (o->get("value", vo, true)) {
      // bridge side is always 0..100%, mapped to minLevel()..maxLevel()
      // Note: updating on/off attribute is handled separately (in deviceonoff), don't do it here
      updateCurrentLevel(static_cast<uint8_t>(vo->doubleValue()/100*(maxLevel()-minLevel()))+minLevel(), 0, 0, false, aUpdateMode);
    }
  }
}


void DeviceLevelControl::changeOnOff_impl(bool aOn)
{
  // NOP because we control the output via level control
}


uint8_t DeviceLevelControl::minLevel()
{
  // minimum level is different in general case and for lighting
  return mLighting ? LEVEL_CONTROL_LIGHTING_MIN_LEVEL : EMBER_AF_PLUGIN_LEVEL_CONTROL_MINIMUM_LEVEL;
}

uint8_t DeviceLevelControl::maxLevel()
{
  return EMBER_AF_PLUGIN_LEVEL_CONTROL_MAXIMUM_LEVEL;
}


bool DeviceLevelControl::updateCurrentLevel(uint8_t aAmount, int8_t aDirection, uint16_t aTransitionTimeDs, bool aWithOnOff, UpdateMode aUpdateMode)
{
  // handle relative movement
  int level = aAmount;
  if (aDirection!=0) level = (int)mLevel + (aDirection>0 ? aAmount : -aAmount);
  if (level>maxLevel()) level = maxLevel();
  if (level<minLevel()) level = minLevel();
  // now move to given or calculated level
  if (level!=mLevel || aUpdateMode.Has(UpdateFlags::forced)) {
    OLOG(LOG_INFO, "setting level to %d in %d00mS - %supdatemode=0x%x", aAmount, aTransitionTimeDs, aWithOnOff ? "WITH OnOff, " : "", aUpdateMode.Raw());
    uint8_t previousLevel = mLevel;
    if ((previousLevel<=minLevel() || aUpdateMode.Has(UpdateFlags::forced)) && level>minLevel()) {
      // level is minimum and becomes non-minimum: also set OnOff when enabled
      if (aWithOnOff) updateOnOff(true, aUpdateMode);
    }
    else if (level<=minLevel()) {
      // level is not minimum and should become minimum: prevent or clear OnOff
      if (aWithOnOff) updateOnOff(false, aUpdateMode);
      else if (previousLevel==minLevel()) return false; // already at minimum: no change
      else level = minLevel(); // set to minimum, but not to off
    }
    mLevel = static_cast<uint8_t>(level);
    if (aUpdateMode.Has(UpdateFlags::bridged)) {
      if (aTransitionTimeDs==0xFFFF) {
        // means using default of the device, so we take the recommended transition time
        aTransitionTimeDs = mRecommendedTransitionTimeDS;
      }
      // adjust default channel
      // Note: transmit relative changes as such, although we already calculate the outcome above,
      //   but non-standard channels might arrive at another value (e.g. wrap around)
      JsonObjectPtr params = JsonObject::newObj();
      params->add("channel", JsonObject::newInt32(0)); // default channel
      params->add("relative", JsonObject::newBool(aDirection!=0));
      params->add("value", JsonObject::newDouble((double)(aAmount-minLevel())/(maxLevel()-minLevel())*(aDirection<0 ? -100 : 100))); // bridge side is always 0..100%, mapped to minLevel()..maxLevel()
      params->add("transitionTime", JsonObject::newDouble((double)aTransitionTimeDs/10));
      params->add("apply_now", JsonObject::newBool(true));
      notify("setOutputChannelValue", params);
      // calculate time when transition will be done
      mEndOfLatestTransition = MainLoop::now()+aTransitionTimeDs*(Second/10);
    }
    if (aUpdateMode.Has(UpdateFlags::matter)) {
      FOCUSOLOG("reporting currentLevel attribute change to matter");
      MatterReportingAttributeChangeCallback(GetEndpointId(), ZCL_LEVEL_CONTROL_CLUSTER_ID, ZCL_CURRENT_LEVEL_ATTRIBUTE_ID);
      FOCUSOLOG("reported currentLevel attribute change to matter");
    }
    return true; // changed or forced
  }
  return false; // no change
}


uint16_t DeviceLevelControl::remainingTimeDS()
{
  if (mEndOfLatestTransition==Never) return 0;
  MLMicroSeconds tr = mEndOfLatestTransition-MainLoop::now();
  if (tr<0) return 0; // no transition running
  return static_cast<uint16_t>(tr/(Second/10)); // return as decisecond
}


// MARK: levelControl cluster command implementation callbacks

using namespace LevelControl;


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
  return (mLevelControlOptions & (uint8_t)(~aOptionMask.Raw())) | (aOptionOverride.Raw() & aOptionMask.Raw());
}



void DeviceLevelControl::moveToLevel(uint8_t aAmount, int8_t aDirection, DataModel::Nullable<uint16_t> aTransitionTime, bool aWithOnOff, OptType aOptionMask,OptType aOptionOverride)
{
  EmberAfStatus status = EMBER_ZCL_STATUS_SUCCESS;

  if (aAmount > EMBER_AF_PLUGIN_LEVEL_CONTROL_MAXIMUM_LEVEL) {
    status = EMBER_ZCL_STATUS_INVALID_COMMAND;
  }
  else if (shouldExecuteLevelChange(aWithOnOff, aOptionMask, aOptionOverride)) {
    // OnOff status and options do allow executing
    uint16_t transitionTime;
    if (aTransitionTime.IsNull()) {
      // default, use On/Off transition time attribute's value
      transitionTime = mOnOffTransitionTimeDS;
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
      OnOff::Attributes::GlobalSceneControl::Set(GetEndpointId(), true);
    }
  }
  // send response
  emberAfSendImmediateDefaultResponse(status);
}


bool emberAfLevelControlClusterMoveToLevelCallback(
  CommandHandler * commandObj, const ConcreteCommandPath & commandPath,
  const Commands::MoveToLevel::DecodableType & commandData
)
{
  auto dev = DeviceEndpoints::getDevice<DeviceLevelControl>(commandPath.mEndpointId);
  if (!dev) return false;
  dev->moveToLevel(commandData.level, 0, commandData.transitionTime, false, commandData.optionsMask, commandData.optionsOverride);
  return true;
}

bool emberAfLevelControlClusterMoveToLevelWithOnOffCallback(
  CommandHandler * commandObj, const ConcreteCommandPath & commandPath,
  const Commands::MoveToLevelWithOnOff::DecodableType & commandData
)
{
  auto dev = DeviceEndpoints::getDevice<DeviceLevelControl>(commandPath.mEndpointId);
  if (!dev) return false;
  dev->moveToLevel(commandData.level, 0, commandData.transitionTime, true, 0, 0);
  return true;
}


bool emberAfLevelControlClusterStepCallback(
  app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
  const Commands::Step::DecodableType & commandData
)
{
  auto dev = DeviceEndpoints::getDevice<DeviceLevelControl>(commandPath.mEndpointId);
  if (!dev) return false;
  dev->moveToLevel(
    commandData.stepSize,
    commandData.stepMode==EMBER_ZCL_STEP_MODE_UP ? 1 : -1,
    commandData.transitionTime, false, commandData.optionsMask, commandData.optionsOverride
  );
  return true;
}

bool emberAfLevelControlClusterStepWithOnOffCallback(
  app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
  const Commands::StepWithOnOff::DecodableType & commandData
)
{
  auto dev = DeviceEndpoints::getDevice<DeviceLevelControl>(commandPath.mEndpointId);
  if (!dev) return false;
  dev->moveToLevel(
    commandData.stepSize,
    commandData.stepMode==EMBER_ZCL_STEP_MODE_UP ? 1 : -1,
    commandData.transitionTime, true, 0, 0
  );
  return true;
}



void DeviceLevelControl::dim(int8_t aDirection, uint8_t aRate)
{
  // adjust default channel
  JsonObjectPtr params = JsonObject::newObj();
  params->add("channel", JsonObject::newInt32(0)); // default channel
  params->add("mode", JsonObject::newInt32(aDirection));
  params->add("autostop", JsonObject::newBool(false));
  // matter rate is 0..0xFE units per second, p44 rate is 0..100 units per millisecond
  if (aDirection!=0 && aRate!=0xFF) params->add("dimPerMS", JsonObject::newDouble((double)aRate*100/EMBER_AF_PLUGIN_LEVEL_CONTROL_MAXIMUM_LEVEL/1000));
  notify("dimChannel", params);
}


void DeviceLevelControl::move(uint8_t aMode, DataModel::Nullable<uint8_t> aRate, bool aWithOnOff, OptType aOptionMask, OptType aOptionOverride)
{
  EmberAfStatus status = EMBER_ZCL_STATUS_SUCCESS;

  uint8_t rate;
  if (aRate.IsNull()) {
    // use default rate
    rate = mDefaultMoveRateUnitsPerS;
  }
  else {
    rate = aRate.Value();
  }
  if (rate!=0 || shouldExecuteLevelChange(aWithOnOff, aOptionMask, aOptionOverride)) {
    switch (aMode) {
      case EMBER_ZCL_MOVE_MODE_UP:
        if (currentLevel()==0) {
          // start dimming from off level into on levels -> set onoff
          updateOnOff(true, UpdateMode(UpdateFlags::matter));
        }
        dim(1, rate);
        break;
      case EMBER_ZCL_MOVE_MODE_DOWN:
        dim(-1, rate);
        break;
      default:
        status = EMBER_ZCL_STATUS_INVALID_COMMAND;
        break;
    }
  }
  // send response
  emberAfSendImmediateDefaultResponse(status);
}


bool emberAfLevelControlClusterMoveCallback(
  app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
  const Commands::Move::DecodableType & commandData
)
{
  auto dev = DeviceEndpoints::getDevice<DeviceLevelControl>(commandPath.mEndpointId);
  if (!dev) return false;
  dev->move(commandData.moveMode, commandData.rate, false, commandData.optionsMask, commandData.optionsOverride);
  return true;
}

bool emberAfLevelControlClusterMoveWithOnOffCallback(
  app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
  const Commands::MoveWithOnOff::DecodableType & commandData
)
{
  auto dev = DeviceEndpoints::getDevice<DeviceLevelControl>(commandPath.mEndpointId);
  if (!dev) return false;
  dev->move(commandData.moveMode, commandData.rate, true, 0, 0);
  return true;
}


void DeviceLevelControl::stop(bool aWithOnOff, OptType aOptionMask, OptType aOptionOverride)
{
  EmberAfStatus status = EMBER_ZCL_STATUS_SUCCESS;

  if (shouldExecuteLevelChange(aWithOnOff, aOptionMask, aOptionOverride)) {
    dim(0,0); // stop dimming
  }
  // send response
  emberAfSendImmediateDefaultResponse(status);
}



bool emberAfLevelControlClusterStopCallback(
  app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
  const Commands::Stop::DecodableType & commandData
)
{
  auto dev = DeviceEndpoints::getDevice<DeviceLevelControl>(commandPath.mEndpointId);
  if (!dev) return false;
  dev->stop(false, commandData.optionsMask, commandData.optionsOverride);
  return true;
}

bool emberAfLevelControlClusterStopWithOnOffCallback(
  app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
  const Commands::StopWithOnOff::DecodableType & commandData
)
{
  auto dev = DeviceEndpoints::getDevice<DeviceLevelControl>(commandPath.mEndpointId);
  if (!dev) return false;
  dev->stop(true, 0, 0);
  return true;
}


// MARK: levelControl cluster general callbacks

void DeviceLevelControl::effect(bool aTurnOn)
{
  EmberAfStatus status = EMBER_ZCL_STATUS_SUCCESS;

  // get the OnOffTransitionTime attribute.
  uint16_t transitionTime = 0xFFFF;
  if (emberAfContainsAttribute(GetEndpointId(), LevelControl::Id, Attributes::OnOffTransitionTime::Id)) {
    status = Attributes::OnOffTransitionTime::Get(GetEndpointId(), &transitionTime);
    if (status!=EMBER_ZCL_STATUS_SUCCESS) {
      transitionTime = 0xFFFF;
    }
  }
  // turn on or off
  OLOG(LOG_INFO, "levelcontrol effect: turnOn=%d", aTurnOn);
  if (aTurnOn) {
    // get the default onLevel
    app::DataModel::Nullable<uint8_t> targetOnLevel;
    if (emberAfContainsAttribute(GetEndpointId(), LevelControl::Id, Attributes::OnLevel::Id)) {
      status = Attributes::OnLevel::Get(GetEndpointId(), targetOnLevel);
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

void MatterLevelControlPluginServerInitCallback() {}



// Note: copied from original levelcontrol implementation, needed by on-off
bool LevelControlHasFeature(EndpointId endpoint, LevelControlFeature feature)
{
  bool success;
  uint32_t featureMap;
  success = (Attributes::FeatureMap::Get(endpoint, &featureMap) == EMBER_ZCL_STATUS_SUCCESS);

  return success ? ((featureMap & to_underlying(feature)) != 0) : false;
}


// MARK: attribute access

EmberAfStatus DeviceLevelControl::HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength)
{
  if (clusterId==ZCL_LEVEL_CONTROL_CLUSTER_ID) {
    if (attributeId == ZCL_CURRENT_LEVEL_ATTRIBUTE_ID) {
      return getAttr(buffer, maxReadLength, currentLevel());
    }
    if (attributeId == ZCL_LEVEL_CONTROL_REMAINING_TIME_ATTRIBUTE_ID) {
      return getAttr(buffer, maxReadLength, remainingTimeDS());
    }
    if (attributeId == ZCL_ON_OFF_TRANSITION_TIME_ATTRIBUTE_ID) {
      return getAttr(buffer, maxReadLength, mOnOffTransitionTimeDS);
    }
    if (attributeId == ZCL_ON_LEVEL_ATTRIBUTE_ID) {
      return getAttr(buffer, maxReadLength, mOnLevel);
    }
    if (attributeId == ZCL_OPTIONS_ATTRIBUTE_ID) {
      return getAttr(buffer, maxReadLength, mLevelControlOptions);
    }
    if (attributeId == ZCL_DEFAULT_MOVE_RATE_ATTRIBUTE_ID) {
      return getAttr(buffer, maxReadLength, mDefaultMoveRateUnitsPerS);
    }
    if (attributeId == ZCL_MINIMUM_LEVEL_ATTRIBUTE_ID) {
      return getAttr(buffer, maxReadLength, minLevel());
    }
    if (attributeId == ZCL_MAXIMUM_LEVEL_ATTRIBUTE_ID) {
      return getAttr(buffer, maxReadLength, maxLevel());
    }
    if (attributeId == ZCL_START_UP_CURRENT_LEVEL_ATTRIBUTE_ID) {
      return getAttr(buffer, maxReadLength, minLevel()); // TODO: default to off, check if this matches intentions
    }
    // common attributes
    if (attributeId == ZCL_CLUSTER_REVISION_SERVER_ATTRIBUTE_ID) {
      return getAttr<uint16_t>(buffer, maxReadLength, ZCL_LEVEL_CONTROL_CLUSTER_REVISION);
    }
    if (attributeId == ZCL_FEATURE_MAP_SERVER_ATTRIBUTE_ID) {
      return getAttr<uint32_t>(buffer, maxReadLength, ZCL_LEVEL_CONTROL_CLUSTER_FEATURE_MAP | (mLighting ? EMBER_AF_LEVEL_CONTROL_FEATURE_LIGHTING : 0));
    }
  }
  // let base class try
  return inherited::HandleReadAttribute(clusterId, attributeId, buffer, maxReadLength);
}


EmberAfStatus DeviceLevelControl::HandleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer)
{
  if (clusterId==ZCL_LEVEL_CONTROL_CLUSTER_ID) {
    if (attributeId == ZCL_ON_OFF_TRANSITION_TIME_ATTRIBUTE_ID) {
      return setAttr(mOnOffTransitionTimeDS, buffer);
    }
    if (attributeId == ZCL_ON_LEVEL_ATTRIBUTE_ID) {
      return setAttr(mOnLevel, buffer);
    }
    if (attributeId == ZCL_OPTIONS_ATTRIBUTE_ID) {
      return setAttr(mLevelControlOptions, buffer);
    }
    if (attributeId == ZCL_DEFAULT_MOVE_RATE_ATTRIBUTE_ID) {
      return setAttr(mDefaultMoveRateUnitsPerS, buffer);
    }
  }
  // let base class try
  return inherited::HandleWriteAttribute(clusterId, attributeId, buffer);
}


string DeviceLevelControl::description()
{
  string s = inherited::description();
  string_format_append(s, "\n- LevelControlOptions: %d", mLevelControlOptions);
  string_format_append(s, "\n- currentLevel: %d", mLevel);
  string_format_append(s, "\n- OnLevel: %d", mOnLevel);
  string_format_append(s, "\n- OnOffTime: %d", mOnOffTransitionTimeDS);
  string_format_append(s, "\n- DimRate: %d", mDefaultMoveRateUnitsPerS);
  return s;
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

