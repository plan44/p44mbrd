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

#include "devicelevelcontrol.h"
#include "device_impl.h"

using namespace app;
using namespace Clusters;

// MARK: - LevelControl Device specific declarations

// REVISION DEFINITIONS:
// TODO: move these to a better place, probably into the devices that actually handle them, or
//   try to extract them from ZAP-generated defs
// =================================================================================

#define ZCL_LEVEL_CONTROL_CLUSTER_REVISION (5u)
#define ZCL_LEVEL_CONTROL_CLUSTER_FEATURE_MAP \
  (EMBER_AF_LEVEL_CONTROL_FEATURE_ON_OFF|EMBER_AF_LEVEL_CONTROL_FEATURE_LIGHTING)

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(levelControlAttrs)
  // DECLARE_DYNAMIC_ATTRIBUTE(attId, attType, attSizeBytes, attrMask)
  DECLARE_DYNAMIC_ATTRIBUTE(ZCL_CURRENT_LEVEL_ATTRIBUTE_ID, INT8U, 1, 0), /* current level */
  DECLARE_DYNAMIC_ATTRIBUTE(ZCL_ON_OFF_TRANSITION_TIME_ATTRIBUTE_ID, INT16U, 2, ZAP_ATTRIBUTE_MASK(WRITABLE)), /* onoff transition time */
  DECLARE_DYNAMIC_ATTRIBUTE(ZCL_ON_LEVEL_ATTRIBUTE_ID, INT8U, 1, ZAP_ATTRIBUTE_MASK(WRITABLE)), /* level for fully on */
  DECLARE_DYNAMIC_ATTRIBUTE(ZCL_OPTIONS_ATTRIBUTE_ID, INT8U, 1, ZAP_ATTRIBUTE_MASK(WRITABLE)), /* options */
  DECLARE_DYNAMIC_ATTRIBUTE(ZCL_DEFAULT_MOVE_RATE_ATTRIBUTE_ID, INT8U, 1, ZAP_ATTRIBUTE_MASK(WRITABLE)), /* default move/dim rate */
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

const EmberAfDeviceType gDimmableLightTypes[] = {
  { DEVICE_TYPE_MA_DIMMABLE_LIGHT, DEVICE_VERSION_DEFAULT },
  { DEVICE_TYPE_BRIDGED_NODE, DEVICE_VERSION_DEFAULT }
};


// MARK: - DeviceLevelControl

DeviceLevelControl::DeviceLevelControl() :
  // Attribute defaults
  mLevel(0),
  mOnLevel(0xFE), // FIXME: just assume full power for default on state
  mOptions(0), // No default options (see EmberAfLevelControlOptions for choices)
  mOnOffTransitionTimeDS(5), // FIXME: this is just the dS default of 0.5 sec, report actual value later
  mDefaultMoveRateUnitsPerS(0xFE/7) // FIXME: just dS default of full range in 7 seconds
{
  // - declare specific clusters
  addClusterDeclarations(Span<EmberAfCluster>(dimmableLightClusters));
}

void DeviceLevelControl::finalizeDeviceDeclaration()
{
  finalizeDeviceDeclarationWithTypes(Span<const EmberAfDeviceType>(gDimmableLightTypes));
}


void DeviceLevelControl::initBridgedInfo(JsonObjectPtr aDeviceInfo)
{
  inherited::initBridgedInfo(aDeviceInfo);
  // no extra info at this level so far -> NOP
}


void DeviceLevelControl::parseChannelStates(JsonObjectPtr aChannelStates, UpdateMode aUpdateMode)
{
  // OnOff just sets on/off state when brightness>0
  inherited::parseChannelStates(aChannelStates, aUpdateMode);
  // init level
  JsonObjectPtr o;
  if (aChannelStates->get("brightness", o)) {
    JsonObjectPtr vo;
    if (o->get("value", vo, true)) {
      updateCurrentLevel(static_cast<uint8_t>(vo->doubleValue()/100*0xFE), 0, 0, false, aUpdateMode);
    }
  }
}


void DeviceLevelControl::changeOnOff_impl(bool aOn)
{
  // NOP because we control the output via level control
}


bool DeviceLevelControl::updateCurrentLevel(uint8_t aAmount, int8_t aDirection, uint16_t aTransitionTimeDs, bool aWithOnOff, UpdateMode aUpdateMode)
{
  // handle relative movement
  int level = aAmount;
  if (aDirection!=0) level = (int)mLevel + (aDirection>0 ? aAmount : -aAmount);
  if (level>0xFE) level = 0xFE;
  if (level<0) level = 0;
  // now move to given or calculated level
  if (level!=mLevel || aUpdateMode.Has(UpdateFlags::forced)) {
    ChipLogProgress(DeviceLayer, "p44 Device[%s]: set level to %d in %d00mS - updatemode=%d", GetName().c_str(), aAmount, aTransitionTimeDs, aUpdateMode.Raw());
    if ((mLevel==0 || aUpdateMode.Has(UpdateFlags::forced)) && level>0) {
      // level is zero and becomes non-null: also set OnOff when enabled
      if (aWithOnOff) updateOnOff(true, aUpdateMode);
    }
    else if (level==0) {
      // level is not zero and should becomes zero: prevent or clear OnOff
      if (aWithOnOff) updateOnOff(false, aUpdateMode);
      else if (mLevel==1) return false; // already at minimum: no change
      else level = 1; // set to minimum, but not to off
    }
    mLevel = static_cast<uint8_t>(level);
    if (aUpdateMode.Has(UpdateFlags::bridged)) {
      // adjust default channel
      // Note: transmit relative changes as such, although we already calculate the outcome above,
      //   but non-standard channels might arrive at another value (e.g. wrap around)
      JsonObjectPtr params = JsonObject::newObj();
      params->add("channel", JsonObject::newInt32(0)); // default channel
      params->add("relative", JsonObject::newBool(aDirection!=0));
      params->add("value", JsonObject::newDouble((double)aAmount*100/0xFE*(aDirection<0 ? -1 : 1)));
      if (aTransitionTimeDs!=0xFFFF) params->add("transitionTime", JsonObject::newDouble(aTransitionTimeDs/10));
      params->add("apply_now", JsonObject::newBool(true));
      notify("setOutputChannelValue", params);
    }
    if (aUpdateMode.Has(UpdateFlags::matter)) {
      MatterReportingAttributeChangeCallback(GetEndpointId(), ZCL_LEVEL_CONTROL_CLUSTER_ID, ZCL_CURRENT_LEVEL_ATTRIBUTE_ID);
    }
    return true; // changed or forced
  }
  return false; // no change
}


// MARK: levelControl cluster command implementation callbacks

using namespace LevelControl;


uint8_t DeviceLevelControl::finalOptions(uint8_t aOptionMask, uint8_t aOptionOverride)
{
  return (mOptions & (uint8_t)(~aOptionMask)) | (aOptionOverride & aOptionMask);
}


bool DeviceLevelControl::shouldExecute(bool aWithOnOff, uint8_t aOptionMask, uint8_t aOptionOverride)
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
  return finalOptions(aOptionMask, aOptionOverride) & EMBER_ZCL_LEVEL_CONTROL_OPTIONS_EXECUTE_IF_OFF;
}


void DeviceLevelControl::moveToLevel(uint8_t aAmount, int8_t aDirection, DataModel::Nullable<uint16_t> aTransitionTime, bool aWithOnOff, uint8_t aOptionMask, uint8_t aOptionOverride)
{
  EmberAfStatus status = EMBER_ZCL_STATUS_SUCCESS;

  if (aAmount > EMBER_AF_PLUGIN_LEVEL_CONTROL_MAXIMUM_LEVEL) {
    status = EMBER_ZCL_STATUS_INVALID_COMMAND;
  }
  else if (shouldExecute(aWithOnOff, aOptionMask, aOptionOverride)) {
    // OnOff status and options do allow executing
    uint16_t transitionTime;
    if (aTransitionTime.IsNull()) {
      // default, use On/Off transition time attribute's value
      transitionTime = mOnOffTransitionTimeDS;
    }
    else {
      transitionTime = aTransitionTime.Value();
    }
    updateCurrentLevel(aAmount, aDirection, transitionTime, aWithOnOff, UpdateMode(UpdateFlags::bridged, UpdateFlags::matter));
    // Support for global scene for last state before getting switched off
    // - The GlobalSceneControl attribute is defined in order to prevent a second off command storing the
    //   all-devices-off situation as a global scene, and to prevent a second on command destroying the current
    //   settings by going back to the global scene.
    // - The GlobalSceneControl attribute SHALL be set to TRUE after the reception of a command which causes the OnOff
    //   attribute to be set to TRUE, such as a standard On command, a **Move to level (with on/off) command**, a Recall
    //   scene command or a On with recall global scene command.
    if (aWithOnOff) {
      uint32_t featureMap;
      if (
        Attributes::FeatureMap::Get(GetEndpointId(), &featureMap) == EMBER_ZCL_STATUS_SUCCESS &&
        READBITS(featureMap, EMBER_AF_LEVEL_CONTROL_FEATURE_LIGHTING)
      ) {
        OnOff::Attributes::GlobalSceneControl::Set(GetEndpointId(), true);
      }
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
  if (aDirection!=0 && aRate!=0xFF) params->add("dimPerMS", JsonObject::newDouble((double)aRate*100/0xFE/1000));
  notify("dimChannel", params);
}


void DeviceLevelControl::move(uint8_t aMode, DataModel::Nullable<uint8_t> aRate, bool aWithOnOff, uint8_t aOptionMask, uint8_t aOptionOverride)
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
  if (rate!=0 || shouldExecute(aWithOnOff, aOptionMask, aOptionOverride)) {
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
        status = EMBER_ZCL_STATUS_INVALID_FIELD;
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


void DeviceLevelControl::stop(bool aWithOnOff, uint8_t aOptionMask, uint8_t aOptionOverride)
{
  EmberAfStatus status = EMBER_ZCL_STATUS_SUCCESS;

  if (shouldExecute(aWithOnOff, aOptionMask, aOptionOverride)) {
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
    if (targetOnLevel.IsNull()) targetOnLevel.SetNonNull(static_cast<uint8_t>(0xFE));
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


// MARK: attribute access

EmberAfStatus DeviceLevelControl::HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength)
{
  if (clusterId==ZCL_LEVEL_CONTROL_CLUSTER_ID) {
    if ((attributeId == ZCL_CURRENT_LEVEL_ATTRIBUTE_ID) && (maxReadLength == 1)) {
      *buffer = currentLevel();
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if ((attributeId == ZCL_ON_OFF_TRANSITION_TIME_ATTRIBUTE_ID) && (maxReadLength == 2)) {
      *((uint16_t *)buffer) = mOnOffTransitionTimeDS;
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if ((attributeId == ZCL_ON_LEVEL_ATTRIBUTE_ID) && (maxReadLength == 1)) {
      *buffer = mOnLevel;
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if ((attributeId == ZCL_OPTIONS_ATTRIBUTE_ID) && (maxReadLength == 1)) {
      *buffer = mOptions;
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if ((attributeId == ZCL_DEFAULT_MOVE_RATE_ATTRIBUTE_ID) && (maxReadLength == 1)) {
      *buffer = mDefaultMoveRateUnitsPerS;
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    // common attributes
    if ((attributeId == ZCL_CLUSTER_REVISION_SERVER_ATTRIBUTE_ID) && (maxReadLength == 2)) {
      *((uint16_t*)buffer) = (uint16_t) ZCL_LEVEL_CONTROL_CLUSTER_REVISION;
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if ((attributeId == ZCL_FEATURE_MAP_SERVER_ATTRIBUTE_ID) && (maxReadLength == 4)) {
      *((uint32_t*)buffer) = (uint32_t) ZCL_LEVEL_CONTROL_CLUSTER_FEATURE_MAP;
      return EMBER_ZCL_STATUS_SUCCESS;
    }
  }
  // let base class try
  return inherited::HandleReadAttribute(clusterId, attributeId, buffer, maxReadLength);
}


EmberAfStatus DeviceLevelControl::HandleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer)
{
  if (clusterId==ZCL_LEVEL_CONTROL_CLUSTER_ID) {
    if (attributeId == ZCL_ON_OFF_TRANSITION_TIME_ATTRIBUTE_ID) {
      mOnOffTransitionTimeDS = *((uint16_t *)buffer);
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if (attributeId == ZCL_ON_LEVEL_ATTRIBUTE_ID) {
      mOnLevel = *buffer;
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if (attributeId == ZCL_OPTIONS_ATTRIBUTE_ID) {
      mOptions = *buffer;
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if (attributeId == ZCL_DEFAULT_MOVE_RATE_ATTRIBUTE_ID) {
      mDefaultMoveRateUnitsPerS = *buffer;
      return EMBER_ZCL_STATUS_SUCCESS;
    }
  }
  // let base class try
  return inherited::HandleWriteAttribute(clusterId, attributeId, buffer);
}


void DeviceLevelControl::logStatus(const char *aReason)
{
  inherited::logStatus(aReason);
  ChipLogDetail(DeviceLayer, "- currentLevel: %d", mLevel);
  ChipLogDetail(DeviceLayer, "- OnLevel: %d", mOnLevel);
  ChipLogDetail(DeviceLayer, "- Options: %d", mOptions);
  ChipLogDetail(DeviceLayer, "- OnOffTime: %d", mOnOffTransitionTimeDS);
  ChipLogDetail(DeviceLayer, "- DimRate: %d", mDefaultMoveRateUnitsPerS);
}
