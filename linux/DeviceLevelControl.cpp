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

// REVISION DEFINITIONS:
// TODO: move these to a better place, probably into the devices that actually handle them, or
//   try to extract them from ZAP-generated defs
// =================================================================================

#define ZCL_LEVEL_CONTROL_CLUSTER_REVISION (5u)

using namespace app;
using namespace Clusters;


// MARK: - DeviceLevelControl

DeviceLevelControl::DeviceLevelControl(const char * szDeviceName, std::string szLocation, std::string aDSUID) :
  inherited(szDeviceName, szLocation, aDSUID),
  mLevel(0)
{
}


EmberAfStatus DeviceLevelControl::HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength)
{
  if (clusterId==ZCL_LEVEL_CONTROL_CLUSTER_ID) {
    if ((attributeId == ZCL_CLUSTER_REVISION_SERVER_ATTRIBUTE_ID) && (maxReadLength == 2)) {
      *((uint16_t *)buffer) = ZCL_LEVEL_CONTROL_CLUSTER_REVISION;
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if ((attributeId == ZCL_CURRENT_LEVEL_ATTRIBUTE_ID) && (maxReadLength == 1)) {
      *buffer = currentLevel();
      return EMBER_ZCL_STATUS_SUCCESS;
    }
  }
  // let base class try
  return inherited::HandleReadAttribute(clusterId, attributeId, buffer, maxReadLength);
}


void DeviceLevelControl::changeOnOff_impl(bool aOn)
{
  // NOP because we control the output via level control
}


bool DeviceLevelControl::setCurrentLevel(uint8_t aAmount, int8_t aDirection, uint16_t aTransitionTimeDs, bool aWithOnOff)
{
  // handle relative movement
  int level = aAmount;
  if (aDirection!=0) level = (int)mLevel + (aDirection>0 ? aAmount : -aAmount);
  if (level>0xFE) level = 0xFE;
  if (level<0) level = 0;
  // now move to given or calculated level
  ChipLogProgress(DeviceLayer, "Device[%s]: set level to %d in %d00mS", mName, aAmount, aTransitionTimeDs);
  if (level!=mLevel) {
    if (mLevel==0) {
      // level is zero and becomes non-null: also set OnOff when enabled
      if (aWithOnOff) setOnOff(true);
    }
    else if (level==0) {
      // level is not zero and should becomes zero: prevent or clear OnOff
      if (aWithOnOff) setOnOff(false);
      else if (mLevel==1) return false; // already at minimum: no change
      else level = 1; // set to minimum, but not to off
    }
    mLevel = level;
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
    // report back to matter
    MatterReportingAttributeChangeCallback(GetEndpointId(), ZCL_LEVEL_CONTROL_CLUSTER_ID, ZCL_CURRENT_LEVEL_ATTRIBUTE_ID);
    return true; // changed
  }
  return false; // no change
}


EmberAfStatus DeviceLevelControl::HandleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer)
{
  if (clusterId==ZCL_LEVEL_CONTROL_CLUSTER_ID) {
    if ((attributeId == ZCL_CURRENT_LEVEL_ATTRIBUTE_ID) && IsReachable()) {
      setCurrentLevel(*buffer, 0, 0, false); // absolute, immediate
      return EMBER_ZCL_STATUS_SUCCESS;
    }
  }
  // let base class try
  return inherited::HandleWriteAttribute(clusterId, attributeId, buffer);
}


// MARK: levelControl cluster command implementation callbacks

using namespace LevelControl;


uint8_t DeviceLevelControl::getOptions(uint8_t aOptionMask, uint8_t aOptionOverride)
{
  uint8_t options;
  EmberAfStatus status = Attributes::Options::Get(GetEndpointId(), &options);
  if (status!=EMBER_ZCL_STATUS_SUCCESS) {
    // should not happen because options is mandatory, but if, assume default value
    options = 0x00;
  }
  options = (options & ~aOptionMask) | (aOptionOverride & aOptionMask);
  return options;
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
  return getOptions(aOptionMask, aOptionOverride) & EMBER_ZCL_LEVEL_CONTROL_OPTIONS_EXECUTE_IF_OFF;
}


void DeviceLevelControl::moveToLevel(uint8_t aAmount, int8_t aDirection, uint16_t aTransitionTimeDs, bool aWithOnOff, uint8_t aOptionMask, uint8_t aOptionOverride)
{
  EmberAfStatus status = EMBER_ZCL_STATUS_SUCCESS;

  if (aAmount > EMBER_AF_PLUGIN_LEVEL_CONTROL_MAXIMUM_LEVEL) {
    status = EMBER_ZCL_STATUS_INVALID_COMMAND;
  }
  else if (shouldExecute(aWithOnOff, aOptionMask, aOptionOverride)) {
    // OnOff status and options do allow executing
    if (aTransitionTimeDs==0xFFFF) {
      // default, use On/Off transition time attribute's value
      EmberAfStatus status = Attributes::OnOffTransitionTime::Get(GetEndpointId(), &aTransitionTimeDs);
      if (status!=EMBER_ZCL_STATUS_SUCCESS) {
        // should not happen because options is mandatory, but if, assume default value
        aTransitionTimeDs = 0; // just instantaneous
      }
    }
    setCurrentLevel(aAmount, aDirection, aTransitionTimeDs, aWithOnOff);
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
  dev->moveToLevel(commandData.level, 0, commandData.transitionTime, false, commandData.optionMask, commandData.optionOverride);
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
    commandData.transitionTime, false, commandData.optionMask, commandData.optionOverride
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



void DeviceLevelControl::dim(uint8_t aDirection, uint8_t aRate)
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


void DeviceLevelControl::move(uint8_t aMode, uint8_t aRate, bool aWithOnOff, uint8_t aOptionMask, uint8_t aOptionOverride)
{
  EmberAfStatus status = EMBER_ZCL_STATUS_SUCCESS;

  if (aRate!=0 || shouldExecute(aWithOnOff, aOptionMask, aOptionOverride)) {
    // determine rate
    if (aRate==0xFF) {
      // use default rate
      app::DataModel::Nullable<uint8_t> defaultMoveRate;
      status = Attributes::DefaultMoveRate::Get(GetEndpointId(), defaultMoveRate);
      if (status==EMBER_ZCL_STATUS_SUCCESS && !defaultMoveRate.IsNull()) {
        aRate = defaultMoveRate.Value();
      }
    }
    switch (aMode) {
      case EMBER_ZCL_MOVE_MODE_UP:
        if (currentLevel()==0) {
          // start dimming from off level into on levels -> set onoff
          setOnOff(true);
        }
        dim(1, aRate);
        break;
      case EMBER_ZCL_MOVE_MODE_DOWN: dim(-1, aRate);
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
  dev->move(commandData.moveMode, commandData.rate, false, commandData.optionMask, commandData.optionOverride);
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
  dev->stop(false, commandData.optionMask, commandData.optionOverride);
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
    if (targetOnLevel.IsNull()) targetOnLevel.SetNonNull(0xFE);
    setCurrentLevel(targetOnLevel.Value(), 0, transitionTime, true);
  }
  else {
    setCurrentLevel(0, 0, transitionTime, true);
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
  // Set attributes
  // - p44 abstraction is always 1..100%
  Attributes::MinLevel::Set(endpoint, 1);
  Attributes::MaxLevel::Set(endpoint, 0xFE);
  Attributes::OnOffTransitionTime::Set(endpoint, 5); // 0.5 seconds by default
  // done
}

void MatterLevelControlPluginServerInitCallback() {}


