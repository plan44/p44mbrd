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
#include "devicecolorcontrol.h"

using namespace app;
using namespace Clusters;

// MARK: - LevelControl Device specific declarations

// REVISION DEFINITIONS:
// TODO: move these to a better place, probably into the devices that actually handle them, or
//   try to extract them from ZAP-generated defs
// =================================================================================

#define ZCL_COLOR_CONTROL_CLUSTER_REVISION (5u)
#define ZCL_COLOR_CONTROL_CLUSTER_MINIMAL_FEATURE_MAP (to_underlying(ColorControl::ColorControlFeature::kColorTemperature))
#define ZCL_COLOR_CONTROL_CLUSTER_FULLCOLOR_FEATURES (to_underlying(ColorControl::ColorControlFeature::kHueAndSaturation)|to_underlying(ColorControl::ColorControlFeature::kXy))

// TODO: maybe get this from bridge, now 100..1000 is the range the P44-DSB/LC UI offers
#define COLOR_TEMP_PHYSICAL_MIN (100)
#define COLOR_TEMP_PHYSICAL_MAX (1000)
#define COLOR_TEMP_DEFAULT (370) // 2500K = warm white

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(colorControlAttrs)
  DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::CurrentHue::Id, INT8U, 1, 0), /* current hue */
  DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::CurrentSaturation::Id, INT8U, 1, 0), /* current saturation */
  DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::ColorTemperatureMireds::Id, INT16U, 2, 0), /* current color temperature */
  DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::ColorTempPhysicalMaxMireds::Id, INT16U, 2, 0),
  DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::ColorTempPhysicalMinMireds::Id, INT16U, 2, 0),
  DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::CoupleColorTempToLevelMinMireds::Id, INT16U, 2, 0),
  DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::StartUpColorTemperatureMireds::Id, INT16U, 2, ZAP_ATTRIBUTE_MASK(WRITABLE)),
  DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::CurrentX::Id, INT16U, 2, 0), /* current X */
  DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::CurrentY::Id, INT16U, 2, 0), /* current Y */
  DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::Options::Id, BITMAP8, 1, ZAP_ATTRIBUTE_MASK(WRITABLE)), /* options */
  DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::ColorCapabilities::Id, BITMAP16, 2, 0), /* (Bit0=HS, Bit1=EnhancedHS, Bit2=ColorLoop, Bit3=XY, Bit4=ColorTemp) */
  DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::ColorMode::Id, ENUM8, 1, 0), /* current color mode (legcacy): see ColorMode enum */
  DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::EnhancedColorMode::Id, ENUM8, 1, 0), /* current color mode (enhanced hue included): see ColorMode enum */
//    DECLARE_DYNAMIC_ATTRIBUTE(ColorControl::Attributes::EnhancedCurrentHue::Id, INT16U, 2, 0), /* enhanced 16bit non XY equidistant hue */ // TODO: implement
  DECLARE_DYNAMIC_ATTRIBUTE(Globals::Attributes::FeatureMap::Id, BITMAP32, 4, 0),     /* feature map */
DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

constexpr CommandId colorControlIncomingCommands[] = {
  ColorControl::Commands::MoveToHue::Id,
  ColorControl::Commands::MoveHue::Id,
  ColorControl::Commands::StepHue::Id,
  ColorControl::Commands::MoveToSaturation::Id,
  ColorControl::Commands::MoveSaturation::Id,
  ColorControl::Commands::StepSaturation::Id,
  ColorControl::Commands::MoveToHueAndSaturation::Id,
  ColorControl::Commands::MoveToColorTemperature::Id,
  ColorControl::Commands::MoveColorTemperature::Id,
  ColorControl::Commands::StepColorTemperature::Id,
  kInvalidCommandId,
};

// MARK: ct/color light
DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(colorLightClusters)
  DECLARE_DYNAMIC_CLUSTER(ColorControl::Id, colorControlAttrs, colorControlIncomingCommands, nullptr),
DECLARE_DYNAMIC_CLUSTER_LIST_END;

const EmberAfDeviceType gCTLightTypes[] = {
  { DEVICE_TYPE_MA_COLOR_LIGHT, DEVICE_VERSION_DEFAULT },
  { DEVICE_TYPE_MA_BRIDGED_DEVICE, DEVICE_VERSION_DEFAULT }
};

const EmberAfDeviceType gColorLightTypes[] = {
  { DEVICE_TYPE_MA_COLOR_LIGHT, DEVICE_VERSION_DEFAULT },
  { DEVICE_TYPE_MA_BRIDGED_DEVICE, DEVICE_VERSION_DEFAULT }
};



// MARK: - DeviceColorControl

DeviceColorControl::DeviceColorControl(bool aCTOnly) :
  inherited(true), // level control for lighting
  mColorControlOptions(0), // No default options (see EmberAfColorControlOptions for choices)
  mCtOnly(aCTOnly),
  mColorMode(aCTOnly ? colormode_ct : colormode_hs),
  mHue(0),
  mSaturation(0),
  mColorTemp(COLOR_TEMP_DEFAULT),
  mStartupColorTemp(COLOR_TEMP_DEFAULT)
{
  // - declare specific clusters
  addClusterDeclarations(Span<EmberAfCluster>(colorLightClusters));
}

void DeviceColorControl::finalizeDeviceDeclaration()
{
  if (mCtOnly) {
    finalizeDeviceDeclarationWithTypes(Span<const EmberAfDeviceType>(gCTLightTypes));
  }
  else {
    finalizeDeviceDeclarationWithTypes(Span<const EmberAfDeviceType>(gColorLightTypes));
  }
}

void DeviceColorControl::initBridgedInfo(JsonObjectPtr aDeviceInfo, JsonObjectPtr aDeviceComponentInfo, const char* aInputType, const char* aInputId)
{
  inherited::initBridgedInfo(aDeviceInfo, aDeviceComponentInfo, aInputType, aInputId);
  // no extra info at this level so far -> NOP
}


void DeviceColorControl::parseChannelStates(JsonObjectPtr aChannelStates, UpdateMode aUpdateMode)
{
  inherited::parseChannelStates(aChannelStates, aUpdateMode);
  JsonObjectPtr o;
  JsonObjectPtr vo;
  bool relevant;
  // need to determine color mode
  ColorMode colorMode = colormode_unknown;
  if (aChannelStates->get("colortemp", o)) {
    if ((relevant = o->get("age", vo, true))) colorMode = colormode_ct; // age is non-null -> component detemines colormode
    if (o->get("value", vo, true)) {
      // scaling: ct is directly in mired
      updateCurrentColortemp(static_cast<uint16_t>(vo->doubleValue()), relevant && colorMode==colormode_ct ? aUpdateMode : UpdateMode(UpdateFlags::noderive), 0);
    }
  }
  if (!mCtOnly) {
    if (aChannelStates->get("hue", o)) {
      if ((relevant = o->get("age", vo, true))) colorMode = colormode_hs; // age is non-null -> component detemines colormode
      if (o->get("value", vo, true)) {
        // update only cache if not actually in hs mode
        // scaling: hue is 0..360 degrees mapped to 0..0xFE
        updateCurrentHue(static_cast<uint8_t>(vo->doubleValue()/360*0xFE), relevant && colorMode==colormode_hs ? aUpdateMode : UpdateMode(UpdateFlags::noderive), 0);
      }
    }
    if (aChannelStates->get("saturation", o)) {
      if ((relevant = o->get("age", vo, true))) colorMode = colormode_hs; // age is non-null -> component detemines colormode
      if (o->get("value", vo, true)) {
        // update only cache if not actually in hs mode
        // scaling: saturation is 0..100% mapped to 0..0xFE
        updateCurrentSaturation(static_cast<uint8_t>(vo->doubleValue()/100*0xFE), relevant && colorMode==colormode_hs ? aUpdateMode : UpdateMode(UpdateFlags::noderive), 0);
      }
    }
    if (aChannelStates->get("x", o)) {
      if ((relevant = o->get("age", vo, true))) colorMode = colormode_xy; // age is non-null -> component detemines colormode
      if (o->get("value", vo, true)) {
        // update only cache if not actually in hs mode
        // scaling: X is 0..1 mapped to 0..0x10000, with effective range 0..0xFEFF (0..0.9961)
        updateCurrentX(static_cast<uint16_t>(vo->doubleValue()*0xFFFF), relevant && colorMode==colormode_xy ? aUpdateMode : UpdateMode(UpdateFlags::noderive), 0);
      }
    }
    if (aChannelStates->get("y", o)) {
      if ((relevant = o->get("age", vo, true))) colorMode = colormode_xy; // age is non-null -> component detemines colormode
      if (o->get("value", vo, true)) {
        // update only cache if not actually in hs mode
        // scaling: Y is 0..1 mapped to 0..0x10000, with effective range 0..0xFEFF (0..0.9961)
        updateCurrentY(static_cast<uint16_t>(vo->doubleValue()*0xFFFF), relevant && colorMode==colormode_xy ? aUpdateMode : UpdateMode(UpdateFlags::noderive), 0);
      }
    }
  }
  // now actually update resulting color mode
  updateCurrentColorMode(colorMode, aUpdateMode, 0);
}


bool DeviceColorControl::updateCurrentColorMode(ColorMode aColorMode, UpdateMode aUpdateMode, uint16_t aTransitionTimeDS)
{
  bool changed = aColorMode!=mColorMode;
  if (
    !aUpdateMode.Has(UpdateFlags::noderive) &&
    (changed || (aUpdateMode.Has(UpdateFlags::forced) && !aUpdateMode.Has(UpdateFlags::chained)))
  ) {
    OLOG(LOG_INFO, "set color mode to %d - updatemode=0x%x", (int)aColorMode, aUpdateMode.Raw());
    mColorMode = aColorMode;
    if (aUpdateMode.Has(UpdateFlags::bridged)) {
      JsonObjectPtr params = JsonObject::newObj();
      switch (mColorMode) {
        case colormode_hs:
        case colormode_EnhancedHs: // TODO: separate when we actually have EnhancedHue
          FOCUSOLOG("changing colormode to HS");
          updateCurrentHue(mHue, UpdateMode(UpdateFlags::chained, UpdateFlags::forced, UpdateFlags::bridged, UpdateFlags::noapply), aTransitionTimeDS);
          updateCurrentSaturation(mSaturation, UpdateMode(UpdateFlags::chained, UpdateFlags::forced, UpdateFlags::bridged), aTransitionTimeDS);
          break;
        case colormode_xy:
          FOCUSOLOG("changing colormode to XY");
          updateCurrentX(mX, UpdateMode(UpdateFlags::chained, UpdateFlags::forced, UpdateFlags::bridged, UpdateFlags::noapply), aTransitionTimeDS);
          updateCurrentY(mY, UpdateMode(UpdateFlags::chained, UpdateFlags::forced, UpdateFlags::bridged), aTransitionTimeDS);
          break;
        default:
        case colormode_ct:
          FOCUSOLOG("changing colormode to CT");
          updateCurrentColortemp(mColorTemp, UpdateMode(UpdateFlags::chained, UpdateFlags::forced, UpdateFlags::bridged), aTransitionTimeDS);
          break;
      }
    }
    if (changed && aUpdateMode.Has(UpdateFlags::matter)) {
      FOCUSOLOG("reporting colormode attribute change to matter");
      MatterReportingAttributeChangeCallback(GetEndpointId(), ColorControl::Id, ColorControl::Attributes::ColorMode::Id);
      MatterReportingAttributeChangeCallback(GetEndpointId(), ColorControl::Id, ColorControl::Attributes::EnhancedColorMode::Id);
    }
    return true;
  }
  return false;
}





bool DeviceColorControl::updateCurrentHue(uint8_t aHue, UpdateMode aUpdateMode, uint16_t aTransitionTimeDS)
{
  bool changed = aHue!=mHue;
  if (changed || aUpdateMode.Has(UpdateFlags::forced)) {
    OLOG(LOG_INFO, "set hue to %d - updatemode=0x%x", aHue, aUpdateMode.Raw());
    mHue = aHue;
    aUpdateMode.Clear(UpdateFlags::forced); // do not force color mode changes
    if (!updateCurrentColorMode(colormode_hs, aUpdateMode, aTransitionTimeDS)) {
      // color mode has not changed, must separately update hue (otherwise, color mode change already sends H+S)
      if (aUpdateMode.Has(UpdateFlags::bridged)) {
        JsonObjectPtr params = JsonObject::newObj();
        params->add("channelId", JsonObject::newString("hue"));
        params->add("value", JsonObject::newDouble((double)mHue*360/0xFE));
        params->add("transitionTime", JsonObject::newDouble((double)aTransitionTimeDS/10));
        params->add("apply_now", JsonObject::newBool(!aUpdateMode.Has(UpdateFlags::noapply)));
        notify("setOutputChannelValue", params);
      }
    }
    if (changed && aUpdateMode.Has(UpdateFlags::matter)) {
      FOCUSOLOG("reporting hue attribute change to matter");
      MatterReportingAttributeChangeCallback(GetEndpointId(), ColorControl::Id, ColorControl::Attributes::CurrentHue::Id);
    }
    return true; // changed
  }
  return false; // no change
}


bool DeviceColorControl::updateCurrentSaturation(uint8_t aSaturation, UpdateMode aUpdateMode, uint16_t aTransitionTimeDS)
{
  bool changed = aSaturation!=mSaturation;
  if (changed || aUpdateMode.Has(UpdateFlags::forced)) {
    OLOG(LOG_INFO, "set saturation to %d - updatemode=0x%x", aSaturation, aUpdateMode.Raw());
    mSaturation = aSaturation;
    aUpdateMode.Clear(UpdateFlags::forced); // do not force color mode changes
    if (!updateCurrentColorMode(colormode_hs, aUpdateMode, aTransitionTimeDS)) {
      // color mode has not changed, must separately update saturation (otherwise, color mode change already sends H+S)
      if (aUpdateMode.Has(UpdateFlags::bridged)) {
        JsonObjectPtr params = JsonObject::newObj();
        params->add("channelId", JsonObject::newString("saturation"));
        params->add("value", JsonObject::newDouble((double)mSaturation*100/0xFE));
        params->add("transitionTime", JsonObject::newDouble((double)aTransitionTimeDS/10));
        params->add("apply_now", JsonObject::newBool(!aUpdateMode.Has(UpdateFlags::noapply)));
        notify("setOutputChannelValue", params);
      }
    }
    if (changed && aUpdateMode.Has(UpdateFlags::matter)) {
      FOCUSOLOG("reporting saturation attribute change to matter");
      MatterReportingAttributeChangeCallback(GetEndpointId(), ColorControl::Id, ColorControl::Attributes::CurrentSaturation::Id);
    }
    return true; // changed
  }
  return false; // no change
}


bool DeviceColorControl::updateCurrentColortemp(uint16_t aColortemp, UpdateMode aUpdateMode, uint16_t aTransitionTimeDS)
{
  bool changed = aColortemp!=mColorTemp;
  if (changed || aUpdateMode.Has(UpdateFlags::forced)) {
    OLOG(LOG_INFO, "set colortemp to %d - updatemode=0x%x", aColortemp, aUpdateMode.Raw());
    mColorTemp = aColortemp;
    if (mColorTemp<COLOR_TEMP_PHYSICAL_MIN) mColorTemp = COLOR_TEMP_PHYSICAL_MIN;
    else if (mColorTemp>COLOR_TEMP_PHYSICAL_MAX) mColorTemp = COLOR_TEMP_PHYSICAL_MAX;
    aUpdateMode.Clear(UpdateFlags::forced); // do not force color mode changes
    if (!updateCurrentColorMode(colormode_ct, aUpdateMode, aTransitionTimeDS)) {
      // color mode has not changed, must separately update colortemp (otherwise, color mode change already sends CT)
      if (aUpdateMode.Has(UpdateFlags::bridged)) {
        JsonObjectPtr params = JsonObject::newObj();
        params->add("channelId", JsonObject::newString("colortemp"));
        params->add("value", JsonObject::newDouble(mColorTemp)); // is in mireds
        params->add("transitionTime", JsonObject::newDouble((double)aTransitionTimeDS/10));
        params->add("apply_now", JsonObject::newBool(!aUpdateMode.Has(UpdateFlags::noapply)));
        notify("setOutputChannelValue", params);
      }
    }
    if (changed && aUpdateMode.Has(UpdateFlags::matter)) {
      FOCUSOLOG("reporting colortemperature attribute change to matter");
      MatterReportingAttributeChangeCallback(GetEndpointId(), ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id);
    }
    return true; // changed
  }
  return false; // no change
}


bool DeviceColorControl::updateCurrentX(uint16_t aX, UpdateMode aUpdateMode, uint16_t aTransitionTimeDS)
{
  bool changed = aX!=mX;
  if (changed || aUpdateMode.Has(UpdateFlags::forced)) {
    OLOG(LOG_INFO, "set X to %d - updatemode=0x%x", aX, aUpdateMode.Raw());
    mX = aX;
    aUpdateMode.Clear(UpdateFlags::forced); // do not force color mode changes
    if (!updateCurrentColorMode(colormode_xy, aUpdateMode, aTransitionTimeDS)) {
      // color mode has not changed, must separately update X (otherwise, color mode change already sends X+Y)
      if (aUpdateMode.Has(UpdateFlags::bridged)) {
        JsonObjectPtr params = JsonObject::newObj();
        params->add("channelId", JsonObject::newString("x"));
        params->add("value", JsonObject::newDouble((double)mX/0xFFFE));
        params->add("transitionTime", JsonObject::newDouble((double)aTransitionTimeDS/10));
        params->add("apply_now", JsonObject::newBool(!aUpdateMode.Has(UpdateFlags::noapply)));
        notify("setOutputChannelValue", params);
      }
    }
    if (changed && aUpdateMode.Has(UpdateFlags::matter)) {
      FOCUSOLOG("reporting X attribute change to matter");
      MatterReportingAttributeChangeCallback(GetEndpointId(), ColorControl::Id, ColorControl::Attributes::CurrentX::Id);
    }
    return true; // changed
  }
  return false; // no change
}


bool DeviceColorControl::updateCurrentY(uint16_t aY, UpdateMode aUpdateMode, uint16_t aTransitionTimeDS)
{
  bool changed = aY!=mY;
  if (changed || aUpdateMode.Has(UpdateFlags::forced)) {
    OLOG(LOG_INFO, "set Y to %d - updatemode=0x%x", aY, aUpdateMode.Raw());
    mY = aY;
    aUpdateMode.Clear(UpdateFlags::forced); // do not force color mode changes
    if (!updateCurrentColorMode(colormode_xy, aUpdateMode, aTransitionTimeDS)) {
      // color mode has not changed, must separately update Y (otherwise, color mode change already sends X+Y)
      if (aUpdateMode.Has(UpdateFlags::bridged)) {
        JsonObjectPtr params = JsonObject::newObj();
        params->add("channelId", JsonObject::newString("y"));
        params->add("value", JsonObject::newDouble((double)mY/0xFFFE));
        params->add("transitionTime", JsonObject::newDouble((double)aTransitionTimeDS/10));
        params->add("apply_now", JsonObject::newBool(!aUpdateMode.Has(UpdateFlags::noapply)));
        notify("setOutputChannelValue", params);
      }
    }
    if (changed && aUpdateMode.Has(UpdateFlags::matter)) {
      FOCUSOLOG("reporting Y attribute change to matter");
      MatterReportingAttributeChangeCallback(GetEndpointId(), ColorControl::Id, ColorControl::Attributes::CurrentY::Id);
    }
    return true; // changed
  }
  return false; // no change
}

// MARK: color control cluster command implementation callbacks

using namespace ColorControl;

bool DeviceColorControl::shouldExecuteColorChange(OptType aOptionMask, OptType aOptionOverride)
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
  if (isOn()) {
    // is already on -> execute anyway
    return true;
  }
  // now the options bit decides about executing or not
  return (mColorControlOptions & (uint8_t)(~aOptionMask.Raw())) | (aOptionOverride.Raw() & aOptionMask.Raw());
}


#ifdef EMBER_AF_PLUGIN_COLOR_CONTROL_SERVER_HSV

bool emberAfColorControlClusterMoveHueCallback(app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
                                               const Commands::MoveHue::DecodableType & commandData)
{
  // FIXME: implement
  FOCUSLOG("=== Received MoveHue Command - NOT IMPLEMENTED YET");
  return false;
//  return ColorControlServer::Instance().moveHueCommand(commandPath.mEndpointId, commandData.moveMode, commandData.rate,
//                                                         commandData.optionsMask, commandData.optionsOverride, false);
}

bool emberAfColorControlClusterMoveSaturationCallback(app::CommandHandler * commandObj,
                                                      const app::ConcreteCommandPath & commandPath,
                                                      const Commands::MoveSaturation::DecodableType & commandData)
{
  // FIXME: implement
  FOCUSLOG("=== Received MoveSaturation Command - NOT IMPLEMENTED YET");
  return false;
//  return ColorControlServer::Instance().moveSaturationCommand(commandPath, commandData);
}


bool emberAfColorControlClusterMoveToHueCallback(app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
                                                 const Commands::MoveToHue::DecodableType & commandData)
{
  FOCUSLOG("=== Received MoveToHue Command");
  auto dev = DeviceEndpoints::getDevice<DeviceColorControl>(commandPath.mEndpointId);
  if (!dev) return false;


  if (dev->shouldExecuteColorChange(commandData.optionsMask, commandData.optionsOverride)) {
    dev->updateCurrentHue(commandData.hue, Device::UpdateMode(Device::UpdateFlags::bridged, Device::UpdateFlags::matter), commandData.transitionTime);
  }
  commandObj->AddStatus(commandPath, Status::Success);
  return true;
}

bool emberAfColorControlClusterMoveToSaturationCallback(app::CommandHandler * commandObj,
                                                        const app::ConcreteCommandPath & commandPath,
                                                        const Commands::MoveToSaturation::DecodableType & commandData)
{
  FOCUSLOG("=== Received MoveToSaturation Command");
  auto dev = DeviceEndpoints::getDevice<DeviceColorControl>(commandPath.mEndpointId);
  if (!dev) return false;
  // FIXME: completely basic implementation, no transition time
  if (dev->shouldExecuteColorChange(commandData.optionsMask, commandData.optionsOverride)) {
    dev->updateCurrentSaturation(commandData.saturation, Device::UpdateMode(Device::UpdateFlags::bridged, Device::UpdateFlags::matter), commandData.transitionTime);
  }
  commandObj->AddStatus(commandPath, Status::Success);
  return true;
}

bool emberAfColorControlClusterMoveToHueAndSaturationCallback(app::CommandHandler * commandObj,
                                                              const app::ConcreteCommandPath & commandPath,
                                                              const Commands::MoveToHueAndSaturation::DecodableType & commandData)
{
  FOCUSLOG("=== Received MoveToHueAndSaturation Command");
  auto dev = DeviceEndpoints::getDevice<DeviceColorControl>(commandPath.mEndpointId);
  if (!dev) return false;
  // FIXME: completely basic implementation, no transition time
  if (dev->shouldExecuteColorChange(commandData.optionsMask, commandData.optionsOverride)) {
    dev->updateCurrentSaturation(commandData.saturation, Device::UpdateMode(Device::UpdateFlags::bridged, Device::UpdateFlags::matter, Device::UpdateFlags::noapply, Device::UpdateFlags::forced), commandData.transitionTime);
    dev->updateCurrentHue(commandData.hue, Device::UpdateMode(Device::UpdateFlags::bridged, Device::UpdateFlags::matter, Device::UpdateFlags::forced), commandData.transitionTime);
  }
  commandObj->AddStatus(commandPath, Status::Success);
  return true;
}

bool emberAfColorControlClusterStepHueCallback(app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
                                               const Commands::StepHue::DecodableType & commandData)
{
  // FIXME: implement
  FOCUSLOG("=== Received StepHue Command - NOT IMPLEMENTED YET");
  return false;
//    return ColorControlServer::Instance().stepHueCommand(commandPath.mEndpointId, commandData.stepMode, commandData.stepSize,
//                                                         commandData.transitionTime, commandData.optionsMask,
//                                                         commandData.optionsOverride, false);
}

bool emberAfColorControlClusterStepSaturationCallback(app::CommandHandler * commandObj,
                                                      const app::ConcreteCommandPath & commandPath,
                                                      const Commands::StepSaturation::DecodableType & commandData)
{
  // FIXME: implement
  FOCUSLOG("=== Received StepSaturation Command - NOT IMPLEMENTED YET");
  return false;
//    return ColorControlServer::Instance().stepSaturationCommand(commandPath, commandData);
}

bool emberAfColorControlClusterEnhancedMoveHueCallback(app::CommandHandler * commandObj,
                                                       const app::ConcreteCommandPath & commandPath,
                                                       const Commands::EnhancedMoveHue::DecodableType & commandData)
{
  // FIXME: implement
  FOCUSLOG("=== Received EnhancedMoveHue Command - NOT IMPLEMENTED YET");
  return false;
//  return ColorControlServer::Instance().moveHueCommand(commandPath.mEndpointId, commandData.moveMode, commandData.rate,
//                                                         commandData.optionsMask, commandData.optionsOverride, true);
}

bool emberAfColorControlClusterEnhancedMoveToHueCallback(app::CommandHandler * commandObj,
                                                         const app::ConcreteCommandPath & commandPath,
                                                         const Commands::EnhancedMoveToHue::DecodableType & commandData)
{
  // FIXME: implement
  FOCUSLOG("=== Received EnhancedMoveToHue Command - NOT IMPLEMENTED YET");
  return false;
//    return ColorControlServer::Instance().moveToHueCommand(commandPath.mEndpointId, commandData.enhancedHue, commandData.direction,
//                                                           commandData.transitionTime, commandData.optionsMask,
//                                                           commandData.optionsOverride, true);
}

bool emberAfColorControlClusterEnhancedMoveToHueAndSaturationCallback(
    app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
    const Commands::EnhancedMoveToHueAndSaturation::DecodableType & commandData)
{
  // FIXME: implement
  FOCUSLOG("=== Received EnhancedMoveToHueAndSaturation Command - NOT IMPLEMENTED YET");
  return false;
//    return ColorControlServer::Instance().moveToHueAndSaturationCommand(commandPath.mEndpointId, commandData.enhancedHue,
//                                                                        commandData.saturation, commandData.transitionTime,
//                                                                        commandData.optionsMask, commandData.optionsOverride, true);
}

bool emberAfColorControlClusterEnhancedStepHueCallback(app::CommandHandler * commandObj,
                                                       const app::ConcreteCommandPath & commandPath,
                                                       const Commands::EnhancedStepHue::DecodableType & commandData)
{
  // FIXME: implement
  FOCUSLOG("=== Received EnhancedStepHue Command - NOT IMPLEMENTED YET");
  return false;
//    return ColorControlServer::Instance().stepHueCommand(commandPath.mEndpointId, commandData.stepMode, commandData.stepSize,
//                                                         commandData.transitionTime, commandData.optionsMask,
//                                                         commandData.optionsOverride, true);
}

bool emberAfColorControlClusterColorLoopSetCallback(app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
                                                    const Commands::ColorLoopSet::DecodableType & commandData)
{
  FOCUSLOG("=== Received ColorLoopSet Command - NOT IMPLEMENTED YET");
  return false;
//    return ColorControlServer::Instance().colorLoopCommand(commandPath, commandData);
}

#endif // EMBER_AF_PLUGIN_COLOR_CONTROL_SERVER_HSV

#ifdef EMBER_AF_PLUGIN_COLOR_CONTROL_SERVER_XY

bool emberAfColorControlClusterMoveToColorCallback(app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
                                                   const Commands::MoveToColor::DecodableType & commandData)
{
  auto dev = DeviceEndpoints::getDevice<DeviceColorControl>(commandPath.mEndpointId);
  if (!dev) return false;
  // FIXME: completely basic implementation, no transition time
  if (dev->shouldExecuteColorChange(commandData.optionsMask, commandData.optionsOverride)) {
    dev->updateCurrentX(commandData.colorX, Device::UpdateMode(Device::UpdateFlags::bridged, Device::UpdateFlags::matter, Device::UpdateFlags::noapply, Device::UpdateFlags::forced), commandData.transitionTime);
    dev->updateCurrentY(commandData.colorY, Device::UpdateMode(Device::UpdateFlags::bridged, Device::UpdateFlags::matter, Device::UpdateFlags::forced), commandData.transitionTime);
  }
  commandObj->AddStatus(commandPath, Status::Success);
  return true;
}

bool emberAfColorControlClusterMoveColorCallback(app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
                                                 const Commands::MoveColor::DecodableType & commandData)
{
  // FIXME: implement
  FOCUSLOG("=== Received MoveColor Command - NOT IMPLEMENTED YET");
  return false;
//    return ColorControlServer::Instance().moveColorCommand(commandPath, commandData);
}

bool emberAfColorControlClusterStepColorCallback(app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
                                                 const Commands::StepColor::DecodableType & commandData)
{
  // FIXME: implement
  FOCUSLOG("=== Received StepColor Command - NOT IMPLEMENTED YET");
  return false;
//    return ColorControlServer::Instance().stepColorCommand(commandPath, commandData);
}

#endif // EMBER_AF_PLUGIN_COLOR_CONTROL_SERVER_XY

#ifdef EMBER_AF_PLUGIN_COLOR_CONTROL_SERVER_TEMP

bool emberAfColorControlClusterMoveToColorTemperatureCallback(app::CommandHandler * commandObj,
                                                              const app::ConcreteCommandPath & commandPath,
                                                              const Commands::MoveToColorTemperature::DecodableType & commandData)
{
  FOCUSLOG("=== Received MoveToColorTemperature Command");
  auto dev = DeviceEndpoints::getDevice<DeviceColorControl>(commandPath.mEndpointId);
  if (!dev) return false;
  // FIXME: completely basic implementation, no transition time
  if (dev->shouldExecuteColorChange(commandData.optionsMask, commandData.optionsOverride)) {
    dev->updateCurrentColortemp(commandData.colorTemperatureMireds, Device::UpdateMode(Device::UpdateFlags::bridged, Device::UpdateFlags::matter), commandData.transitionTime);
  }
  commandObj->AddStatus(commandPath, Status::Success);
  return true;
}

bool emberAfColorControlClusterMoveColorTemperatureCallback(app::CommandHandler * commandObj,
                                                            const app::ConcreteCommandPath & commandPath,
                                                            const Commands::MoveColorTemperature::DecodableType & commandData)
{
  // FIXME: implement
  FOCUSLOG("=== Received MoveColorTemperature Command - NOT IMPLEMENTED YET");
  return false;
//    return ColorControlServer::Instance().moveColorTempCommand(commandPath, commandData);
}

bool emberAfColorControlClusterStepColorTemperatureCallback(app::CommandHandler * commandObj,
                                                            const app::ConcreteCommandPath & commandPath,
                                                            const Commands::StepColorTemperature::DecodableType & commandData)
{
  FOCUSLOG("=== Received StepColorTemperature Command - NOT IMPLEMENTED YET");
  return false;
//    return ColorControlServer::Instance().stepColorTempCommand(commandPath, commandData);
}

void emberAfPluginLevelControlCoupledColorTempChangeCallback(EndpointId endpoint)
{
  // FIXME: implement
  FOCUSLOG("=== Received emberAfPluginLevelControlCoupledColorTempChangeCallback - NOT IMPLEMENTED YET");
  // TODO: implement
//    ColorControlServer::Instance().levelControlColorTempChangeCommand(endpoint);
}

#endif // EMBER_AF_PLUGIN_COLOR_CONTROL_SERVER_TEMP

bool emberAfColorControlClusterStopMoveStepCallback(app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
                                                    const Commands::StopMoveStep::DecodableType & commandData)
{
  // FIXME: implement as soon as we have move commands!!
  FOCUSLOG("=== Received StopMoveStep Command - NOT IMPLEMENTED YET");
  return false;
//    return ColorControlServer::Instance().stopMoveStepCommand(commandPath.mEndpointId, commandData.optionsMask,
//                                                              commandData.optionsOverride);
}

void emberAfColorControlClusterServerInitCallback(EndpointId endpoint)
{
  // TODO: check if we need this
}

#ifdef EMBER_AF_PLUGIN_COLOR_CONTROL_SERVER_TEMP
/**
 * @brief Callback for temperature update when timer is finished
 *
 * @param endpoint endpointId
 */
void emberAfPluginColorControlServerTempTransitionEventHandler(EndpointId endpoint)
{
  // TODO: check if we need this
}
#endif // EMBER_AF_PLUGIN_COLOR_CONTROL_SERVER_TEMP

#ifdef EMBER_AF_PLUGIN_COLOR_CONTROL_SERVER_XY
/**
 * @brief Callback for color update when timer is finished
 *
 * @param endpoint endpointId
 */
void emberAfPluginColorControlServerXyTransitionEventHandler(EndpointId endpoint)
{
  // TODO: check if we need this
}
#endif // EMBER_AF_PLUGIN_COLOR_CONTROL_SERVER_XY

#ifdef EMBER_AF_PLUGIN_COLOR_CONTROL_SERVER_HSV
/**
 * @brief Callback for color hue and saturation update when timer is finished
 *
 * @param endpoint endpointId
 */
void emberAfPluginColorControlServerHueSatTransitionEventHandler(EndpointId endpoint)
{
  // TODO: check if we need this
}
#endif // EMBER_AF_PLUGIN_COLOR_CONTROL_SERVER_HSV

void MatterColorControlPluginServerInitCallback()
{
  // NOP for now
}

void MatterColorControlClusterServerShutdownCallback(EndpointId endpoint)
{
  // NOP for now
}



// MARK: Attribute access

EmberAfStatus DeviceColorControl::HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength)
{
  if (clusterId==ColorControl::Id) {
    if (attributeId == ColorControl::Attributes::ColorCapabilities::Id) {
      // Bit0=HS, Bit1=EnhancedHue, Bit2=ColorLoop, Bit3=XY, Bit4=ColorTemp
      return getAttr<uint16_t>(
        buffer, maxReadLength,
        (uint16_t)to_underlying(ColorControl::ColorCapabilities::kColorTemperatureSupported) |
        (uint16_t)(mCtOnly ? 0 : to_underlying(ColorControl::ColorCapabilities::kHueSaturationSupported)|to_underlying(ColorControl::ColorCapabilities::kXYAttributesSupported))
      );
    }
    if (attributeId == ColorControl::Attributes::ColorMode::Id) {
      // color mode: 0=Hue+Sat (normal and enhanced!), 1=XY, 2=Colortemp
      return getAttr<uint8_t>(buffer, maxReadLength, mColorMode==colormode_EnhancedHs ? (uint8_t)colormode_hs : mColorMode);
    }
    if (attributeId == ColorControl::Attributes::EnhancedColorMode::Id) {
      // TODO: this is already prepared for EnhancedHue, which is not yet implemented itself
      // color mode: 0=Hue+Sat, 1=XY, 2=Colortemp, 3=EnhancedHue+Sat
      return getAttr<uint8_t>(buffer, maxReadLength, mColorMode);
    }
    if (attributeId == ColorControl::Attributes::CurrentHue::Id) {
      return getAttr(buffer, maxReadLength, currentHue());
    }
    if (attributeId == ColorControl::Attributes::CurrentSaturation::Id) {
      return getAttr(buffer, maxReadLength, currentSaturation());
    }
    if (attributeId == ColorControl::Attributes::ColorTemperatureMireds::Id) {
      return getAttr(buffer, maxReadLength, currentColortemp());
    }
    if (attributeId == ColorControl::Attributes::CurrentX::Id) {
      return getAttr(buffer, maxReadLength, currentX());
    }
    if (attributeId == ColorControl::Attributes::CurrentY::Id) {
      return getAttr(buffer, maxReadLength, currentY());
    }
    if (attributeId == ColorControl::Attributes::Options::Id) {
      return getAttr(buffer, maxReadLength, mColorControlOptions);
    }
    if (attributeId == ColorControl::Attributes::StartUpColorTemperatureMireds::Id) {
      return getAttr(buffer, maxReadLength, mStartupColorTemp);
    }
    // constants
    if (attributeId == ColorControl::Attributes::CoupleColorTempToLevelMinMireds::Id) {
      // this is the level that corresponds to max level when CT is coupled to level
      // TODO: level coupling is not yet implemented, maybe make this variable later
      return getAttr<uint16_t>(buffer, maxReadLength, COLOR_TEMP_PHYSICAL_MIN);
    }
    if (attributeId == ColorControl::Attributes::NumberOfPrimaries::Id) {
      return getAttr<uint8_t>(buffer, maxReadLength, 0);
    }
    if (attributeId == ColorControl::Attributes::ColorTempPhysicalMinMireds::Id) {
      return getAttr<uint16_t>(buffer, maxReadLength, COLOR_TEMP_PHYSICAL_MIN);
    }
    if (attributeId == ColorControl::Attributes::ColorTempPhysicalMaxMireds::Id) {
      return getAttr<uint16_t>(buffer, maxReadLength, COLOR_TEMP_PHYSICAL_MAX);
    }
    // common
    if (attributeId == Globals::Attributes::ClusterRevision::Id) {
      return getAttr<uint16_t>(buffer, maxReadLength, ZCL_COLOR_CONTROL_CLUSTER_REVISION);
    }
    if (attributeId == Globals::Attributes::FeatureMap::Id) {
      return getAttr<uint32_t>(
        buffer, maxReadLength,
        ZCL_COLOR_CONTROL_CLUSTER_MINIMAL_FEATURE_MAP | (mCtOnly ? 0 : ZCL_COLOR_CONTROL_CLUSTER_FULLCOLOR_FEATURES)
      );
    }
  }
  // let base class try
  return inherited::HandleReadAttribute(clusterId, attributeId, buffer, maxReadLength);
}


EmberAfStatus DeviceColorControl::HandleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer)
{
  if (clusterId==ColorControl::Id) {
    if (attributeId == ColorControl::Attributes::Options::Id) {
      return setAttr(mColorControlOptions, buffer);
    }
    if (attributeId == ColorControl::Attributes::StartUpColorTemperatureMireds::Id) {
      return setAttr(mStartupColorTemp, buffer);
    }
  }
  // let base class try
  return inherited::HandleWriteAttribute(clusterId, attributeId, buffer);
}


string DeviceColorControl::description()
{
  string s = inherited::description();
  string_format_append(s, "\n- colorControlOptions: %d", mColorControlOptions);
  string_format_append(s, "\n- colormode: %d", mColorMode);
  string_format_append(s, "\n- hue: %d", mHue);
  string_format_append(s, "\n- saturation: %d", mSaturation);
  string_format_append(s, "\n- ct: %d", mColorTemp);
  string_format_append(s, "\n- X: %d", mX);
  string_format_append(s, "\n- Y: %d", mY);
  return s;
}
