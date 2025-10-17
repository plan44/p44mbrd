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
#include "devicecolorcontrol.h"

using namespace app;
using namespace Clusters;

// MARK: - LevelControl Device specific declarations

#define ZCL_COLOR_CONTROL_CLUSTER_MINIMAL_FEATURE_MAP (to_underlying(ColorControl::Feature::kColorTemperature))
#define ZCL_COLOR_CONTROL_CLUSTER_FULLCOLOR_FEATURES (to_underlying(ColorControl::Feature::kHueAndSaturation)|to_underlying(ColorControl::Feature::kXy))

// TODO: maybe get this from bridge, now 100..1000 is the range the P44-DSB/LC UI offers
#define COLOR_TEMP_PHYSICAL_MIN (100)
#define COLOR_TEMP_PHYSICAL_MAX (1000)
#define COLOR_TEMP_DEFAULT (370) // 2500K = warm white

static const EmberAfDeviceType gCTLightTypes[] = {
  { DEVICE_TYPE_MA_COLOR_LIGHT, DEVICE_VERSION_DEFAULT }
};

static const EmberAfDeviceType gColorLightTypes[] = {
  { DEVICE_TYPE_MA_COLOR_LIGHT, DEVICE_VERSION_DEFAULT }
};

static EmberAfClusterSpec gColorLightClusters[] = { { ColorControl::Id, CLUSTER_MASK_SERVER } };


// MARK: - DeviceColorControl

using namespace ColorControl;

DeviceColorControl::DeviceColorControl(bool aCTOnly, ColorControlDelegate& aColorControlDelegate, LevelControlDelegate& aLevelControlDelegate, OnOffDelegate& aOnOffDelegate, IdentifyDelegate* aIdentifyDelegateP, DeviceInfoDelegate& aDeviceInfoDelegate) :
  inherited(true, aLevelControlDelegate, aOnOffDelegate, aIdentifyDelegateP, aDeviceInfoDelegate), // level control for lighting
  mColorControlDelegate(aColorControlDelegate),
  mCtOnly(aCTOnly),
  mColorMode(aCTOnly ? InternalColorMode::ct : InternalColorMode::hs),
  mHue(0),
  mSaturation(0),
  mColorTemp(COLOR_TEMP_DEFAULT),
  mX(0),
  mY(0)
{
  // - declare specific clusters
  useClusterTemplates(Span<EmberAfClusterSpec>(gColorLightClusters));
}


bool DeviceColorControl::finalizeDeviceDeclaration()
{
  if (mCtOnly) {
    return finalizeDeviceDeclarationWithTypes(Span<const EmberAfDeviceType>(gCTLightTypes));
  }
  else {
    return finalizeDeviceDeclarationWithTypes(Span<const EmberAfDeviceType>(gColorLightTypes));
  }
}


uint32_t DeviceColorControl::featureMap()
{
  return ZCL_COLOR_CONTROL_CLUSTER_MINIMAL_FEATURE_MAP | (mCtOnly ? 0 : ZCL_COLOR_CONTROL_CLUSTER_FULLCOLOR_FEATURES);
}


bool DeviceColorControl::hasFeature(ColorControl::Feature aFeature)
{
  return (featureMap() && to_underlying(aFeature))!=0;
}


void DeviceColorControl::didGetInstalled()
{
  Attributes::FeatureMap::Set(endpointId(), featureMap());
  Attributes::ColorCapabilities::Set(
    endpointId(),
    (uint16_t)to_underlying(ColorControl::ColorCapabilities::ColorTemperatureSupported) |
    (uint16_t)(mCtOnly ? 0 : to_underlying(ColorControl::ColorCapabilities::HueSaturationSupported)|to_underlying(ColorControl::ColorCapabilities::XYAttributesSupported))
  );
  Attributes::CoupleColorTempToLevelMinMireds::Set(endpointId(), COLOR_TEMP_PHYSICAL_MIN);
  Attributes::NumberOfPrimaries::Set(endpointId(), 0);
  Attributes::ColorTempPhysicalMinMireds::Set(endpointId(), COLOR_TEMP_PHYSICAL_MIN);
  Attributes::ColorTempPhysicalMaxMireds::Set(endpointId(), COLOR_TEMP_PHYSICAL_MAX);
  Attributes::StartUpColorTemperatureMireds::Set(endpointId(), COLOR_TEMP_DEFAULT);

  // call base class last
  inherited::didGetInstalled();
}


bool DeviceColorControl::updateCurrentColorMode(InternalColorMode aColorMode, UpdateMode aUpdateMode, uint16_t aTTimeDSorRate)
{
  bool changed = aColorMode!=mColorMode;
  if (
    !aUpdateMode.Has(UpdateFlags::noderive) &&
    (changed || (aUpdateMode.Has(UpdateFlags::forced) && !aUpdateMode.Has(UpdateFlags::chained)))
  ) {
    OLOG(LOG_INFO, "set color mode to 0x%02x (InternalColorMode) - updatemode=0x%x", (int)aColorMode, aUpdateMode.Raw());
    mColorMode = aColorMode;
    bool changeApplied = true;
    if (aUpdateMode.Has(UpdateFlags::bridged)) {
      const UpdateMode prepMode = UpdateMode(UpdateFlags::chained, UpdateFlags::forced, UpdateFlags::bridged, UpdateFlags::noapply);
      UpdateMode finalizeMode = prepMode;
      if (aUpdateMode.Has(UpdateFlags::move) || aUpdateMode.Has(UpdateFlags::noapply)) {
        // when start moving with a new color mode, or when this change is one of multiple changes (noapply),
        // we need to change the current mode by setting new mode's parameters silently and immediately first,
        // but not apply it yet. Only after, we can start moving with a rate or adding the other params
        // of the change and THEN apply
        aTTimeDSorRate = 0;
        changeApplied = false;
      }
      else {
        // not moving, we can apply right now
        finalizeMode.Clear(UpdateFlags::noapply);
      }
      switch (mColorMode) {
        case InternalColorMode::hs:
        case InternalColorMode::enhanced_hs: // TODO: separate when we actually have EnhancedHue
          FOCUSOLOG("changing colormode to HS");
          updateCurrentHue(mHue, prepMode, aTTimeDSorRate);
          updateCurrentSaturation(mSaturation, finalizeMode, aTTimeDSorRate);
          break;
        case InternalColorMode::xy:
          FOCUSOLOG("changing colormode to XY");
          updateCurrentX(mX, prepMode, aTTimeDSorRate);
          updateCurrentY(mY, finalizeMode, aTTimeDSorRate);
          break;
        default:
        case InternalColorMode::ct:
          FOCUSOLOG("changing colormode to CT");
          updateCurrentColortemp(mColorTemp, finalizeMode, aTTimeDSorRate);
          break;
      }
    }
    if (changed && aUpdateMode.Has(UpdateFlags::matter)) {
      FOCUSOLOG("reporting colormode attribute change to matter");
      reportAttributeChange(ColorControl::Id, ColorControl::Attributes::ColorMode::Id);
      reportAttributeChange(ColorControl::Id, ColorControl::Attributes::EnhancedColorMode::Id);
    }
    return changeApplied;
  }
  return false;
}


bool DeviceColorControl::adaptParamsImpl(int aInpValue, int &aAbsValue, UpdateMode aUpdateMode, int aMin, int aMax, bool aWrap)
{
  if (aUpdateMode.Has(UpdateFlags::move)) {
    // movement command always means change (but aAbsValue remains unchanged)
    return true;
  }
  bool changed = false;
  int v = aAbsValue; // current value
  if (aUpdateMode.Has(UpdateFlags::relative)) {
    // relative
    if (aUpdateMode.Has(UpdateFlags::down)) v -= aInpValue;
    else v += aInpValue;
  }
  else {
    // absolute
    v = aInpValue;
  }
  // clip or wrap
  if (aWrap) {
    int dist = aMax-aMin;
    while (v>aMax) v -= dist;
    while (v<aMin) v += dist;
  }
  else {
    if (v>aMax) v = aMax;
    else if (v<aMin) v = aMin;
  }
  changed = v!=aAbsValue;
  aAbsValue = v;
  return changed;
}


bool DeviceColorControl::updateCurrentHue(uint8_t aHue, UpdateMode aUpdateMode, uint16_t aTTimeDSorRate)
{
  bool changed = adaptParams(aHue, mHue, aUpdateMode, 0, 0xFE, true); // wraparound when relative
  if (changed || aUpdateMode.Has(UpdateFlags::forced)) {
    OLOG(LOG_INFO, "set hue to 0x%02x (matter-units) with time/rate = %hu - updatemode=0x%x", aHue, aTTimeDSorRate, aUpdateMode.Raw());
    aUpdateMode.Clear(UpdateFlags::forced); // do not force color mode changes
    if (!updateCurrentColorMode(InternalColorMode::hs, aUpdateMode, aTTimeDSorRate)) {
      // color mode change has not yet applied the new value, must separately update (otherwise, color mode change already sends H+S)
      if (aUpdateMode.Has(UpdateFlags::bridged)) {
        // Note: we do not need to check isOn() here, as color change does NOT imply brightness
        //   so cannot affect on/off state. It's up to the adapter to decide if/when to propagate
        mColorControlDelegate.changeHue(mHue, aTTimeDSorRate, aUpdateMode);
      }
    }
    if (changed && aUpdateMode.Has(UpdateFlags::matter)) {
      FOCUSOLOG("reporting hue attribute change to matter");
      reportAttributeChange(ColorControl::Id, ColorControl::Attributes::CurrentHue::Id);
    }
    return true; // changed
  }
  return false; // no change
}


bool DeviceColorControl::updateCurrentSaturation(uint8_t aSaturation, UpdateMode aUpdateMode, uint16_t aTTimeDSorRate)
{
  bool changed = adaptParams(aSaturation, mSaturation, aUpdateMode, 0, 0xFE, false);
  if (changed || aUpdateMode.Has(UpdateFlags::forced)) {
    OLOG(LOG_INFO, "set saturation to 0x%02x (matter-units) with time/rate = %hu - updatemode=0x%x", aSaturation, aTTimeDSorRate, aUpdateMode.Raw());
    aUpdateMode.Clear(UpdateFlags::forced); // do not force color mode changes
    if (!updateCurrentColorMode(InternalColorMode::hs, aUpdateMode, aTTimeDSorRate)) {
      // color mode change has not yet applied the new value, must separately update (otherwise, color mode change already sends H+S)
      if (aUpdateMode.Has(UpdateFlags::bridged)) {
        // Note: we do not need to check isOn() here, as color change does NOT imply brightness
        //   so cannot affect on/off state. It's up to the adapter to decide if/when to propagate
        mColorControlDelegate.changeSaturation(mSaturation, aTTimeDSorRate, aUpdateMode);
      }
    }
    if (changed && aUpdateMode.Has(UpdateFlags::matter)) {
      FOCUSOLOG("reporting saturation attribute change to matter");
      reportAttributeChange(ColorControl::Id, ColorControl::Attributes::CurrentSaturation::Id);
    }
    return true; // changed
  }
  return false; // no change
}


bool DeviceColorControl::updateCurrentColortemp(uint16_t aColortemp, UpdateMode aUpdateMode, uint16_t aTTimeDSorRate, uint16_t aCTMin, uint16_t aCTMax)
{
  bool changed = adaptParams(aColortemp, mColorTemp, aUpdateMode, aCTMin>0 ? aCTMin : COLOR_TEMP_PHYSICAL_MIN, aCTMax>0 ? aCTMax : COLOR_TEMP_PHYSICAL_MAX, false);
  if (changed || aUpdateMode.Has(UpdateFlags::forced)) {
    OLOG(LOG_INFO, "set colortemp to 0x%04x (matter-units) with time/rate = %hu - updatemode=0x%x", aColortemp, aTTimeDSorRate, aUpdateMode.Raw());
    aUpdateMode.Clear(UpdateFlags::forced); // do not force color mode changes
    if (!updateCurrentColorMode(InternalColorMode::ct, aUpdateMode, aTTimeDSorRate)) {
      // color mode change has not yet applied the new value, must separately update (otherwise, color mode change already sends CT)
      if (aUpdateMode.Has(UpdateFlags::bridged)) {
        // Note: we do not need to check isOn() here, as color change does NOT imply brightness
        //   so cannot affect on/off state. It's up to the adapter to decide if/when to propagate
        mColorControlDelegate.changeColortemp(mColorTemp, aTTimeDSorRate, aUpdateMode);
      }
    }
    if (changed && aUpdateMode.Has(UpdateFlags::matter)) {
      FOCUSOLOG("reporting colortemperature attribute change to matter");
      reportAttributeChange(ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id);
    }
    return true; // changed
  }
  return false; // no change
}


bool DeviceColorControl::updateCurrentX(uint16_t aX, UpdateMode aUpdateMode, uint16_t aTTimeDSorRate)
{
  bool changed = adaptParams(aX, mX, aUpdateMode, 0, 0xFFFE, false);
  if (changed || aUpdateMode.Has(UpdateFlags::forced)) {
    OLOG(LOG_INFO, "set X to 0x%04x (matter-units) with time/rate = %hu - updatemode=0x%x", aX, aTTimeDSorRate, aUpdateMode.Raw());
    aUpdateMode.Clear(UpdateFlags::forced); // do not force color mode changes
    if (!updateCurrentColorMode(InternalColorMode::xy, aUpdateMode, aTTimeDSorRate)) {
      // color mode change has not yet applied the new value, must separately update (otherwise, color mode change already sends X+Y)
      if (aUpdateMode.Has(UpdateFlags::bridged)) {
        // Note: we do not need to check isOn() here, as color change does NOT imply brightness
        //   so cannot affect on/off state. It's up to the adapter to decide if/when to propagate
        mColorControlDelegate.changeCieX(mX, aTTimeDSorRate, aUpdateMode);
      }
    }
    if (changed && aUpdateMode.Has(UpdateFlags::matter)) {
      FOCUSOLOG("reporting X attribute change to matter");
      reportAttributeChange(ColorControl::Id, ColorControl::Attributes::CurrentX::Id);
    }
    return true; // changed
  }
  return false; // no change
}


bool DeviceColorControl::updateCurrentY(uint16_t aY, UpdateMode aUpdateMode, uint16_t aTTimeDSorRate)
{
  bool changed = adaptParams(aY, mY, aUpdateMode, 0, 0xFFFE, false);
  if (changed || aUpdateMode.Has(UpdateFlags::forced)) {
    OLOG(LOG_INFO, "set Y to 0x%04x (matter-units) with time/rate = %hu - updatemode=0x%x", aY, aTTimeDSorRate, aUpdateMode.Raw());
    aUpdateMode.Clear(UpdateFlags::forced); // do not force color mode changes
    if (!updateCurrentColorMode(InternalColorMode::xy, aUpdateMode, aTTimeDSorRate)) {
      // color mode change has not yet applied the new value, must separately update (otherwise, color mode change already sends X+Y)
      if (aUpdateMode.Has(UpdateFlags::bridged)) {
        // Note: we do not need to check isOn() here, as color change does NOT imply brightness
        //   so cannot affect on/off state. It's up to the adapter to decide if/when to propagate
        mColorControlDelegate.changeCieY(mY, aTTimeDSorRate, aUpdateMode);
      }
    }
    if (changed && aUpdateMode.Has(UpdateFlags::matter)) {
      FOCUSOLOG("reporting Y attribute change to matter");
      reportAttributeChange(ColorControl::Id, ColorControl::Attributes::CurrentY::Id);
    }
    return true; // changed
  }
  return false; // no change
}


void DeviceColorControl::stopColorMovements()
{
  mColorControlDelegate.stopMovements();
}


// MARK: color control cluster command implementation callbacks


DeviceColorControl::ColorControlOptionsType DeviceColorControl::tempOptions(ColorControlOptionsType aOptionMask, ColorControlOptionsType aOptionOverride)
{
  ColorControlOptionsType opts;
  Attributes::Options::Get(endpointId(), &opts); // persistent options
  opts.Clear(aOptionMask); // clear those set in the mask
  aOptionOverride.Clear(ColorControlOptionsType(~aOptionMask.Raw())); // clear those NOT set in the mask (must not have influence)
  opts.Set(aOptionOverride); // now apply override to the masked bits in the output opts
  return opts;
}


bool DeviceColorControl::shouldExecuteColorChange(ColorControlOptionsType aOptionMask, ColorControlOptionsType aOptionOverride)
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
  return tempOptions(aOptionMask, aOptionOverride).Has(OptionsBitmap::kExecuteIfOff);
}


static UpdateMode updateModeForCommand(UpdateMode aMoreFlags = UpdateMode())
{
  aMoreFlags.Set(UpdateFlags::bridged);
  aMoreFlags.Set(UpdateFlags::matter);
  return aMoreFlags;
}

static UpdateMode updateModeForStep(StepModeEnum aStepMode)
{
  UpdateMode u = updateModeForCommand();
  u.Set(UpdateFlags::relative);
  if (aStepMode==StepModeEnum::kDown) u.Set(UpdateFlags::down);
  return u;
}


static UpdateMode updateModeForMove(MoveModeEnum aMoveMode)
{
  UpdateMode u = updateModeForCommand();
  u.Set(UpdateFlags::move);
  if (aMoveMode==MoveModeEnum::kUp) u.Set(UpdateFlags::up);
  else if (aMoveMode==MoveModeEnum::kDown) u.Set(UpdateFlags::down);
  // neither up or down is considered stop
  return u;
}


static UpdateMode updateModeForCommandWithDirection(DirectionEnum aDirection)
{
  UpdateMode u = updateModeForCommand();
  if (aDirection==DirectionEnum::kUp) u.Set(UpdateFlags::up);
  else if (aDirection==DirectionEnum::kDown) u.Set(UpdateFlags::down);
  else if (aDirection==DirectionEnum::kShortest) u.Set(UpdateFlags::shortest);
  else if (aDirection==DirectionEnum::kLongest) u.Set(UpdateFlags::longest);
  return u;
}


static UpdateMode updateModeForDirectionalXY(int16_t aRateOrStep, UpdateMode aMoreFlags)
{
  UpdateMode u = updateModeForCommand(aMoreFlags);
  if (aRateOrStep>0) u.Set(UpdateFlags::up);
  else if (aRateOrStep<0) u.Set(UpdateFlags::down);
  // neither up or down is considered stop
  return u;
}

static bool moveCheck(MoveModeEnum aMoveMode, uint16_t aRate)
{
  return aMoveMode==MoveModeEnum::kStop || aRate>0;
}



#ifdef MATTER_DM_PLUGIN_COLOR_CONTROL_SERVER_HSV

// MARK: hue

bool emberAfColorControlClusterMoveHueCallback(app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
                                               const Commands::MoveHue::DecodableType & commandData)
{
  FOCUSLOG("=== Received MoveHue Command");
  auto dev = DeviceEndpoints::getDevice<DeviceColorControl>(commandPath.mEndpointId);
  if (!dev) return false;
  if (!moveCheck(commandData.moveMode, commandData.rate)) return false; // cannot move, InvalidCommand
  if (dev->shouldExecuteColorChange(commandData.optionsMask, commandData.optionsOverride)) {
    dev->updateCurrentHue(0, updateModeForMove(commandData.moveMode), commandData.rate);
  }
  commandObj->AddStatus(commandPath, Status::Success);
  return true;
}


bool emberAfColorControlClusterMoveToHueCallback(app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
                                                 const Commands::MoveToHue::DecodableType & commandData)
{
  FOCUSLOG("=== Received MoveToHue Command");
  auto dev = DeviceEndpoints::getDevice<DeviceColorControl>(commandPath.mEndpointId);
  if (!dev) return false;
  if (dev->shouldExecuteColorChange(commandData.optionsMask, commandData.optionsOverride)) {
    dev->updateCurrentHue(commandData.hue, updateModeForCommandWithDirection(commandData.direction), commandData.transitionTime);
  }
  commandObj->AddStatus(commandPath, Status::Success);
  return true;
}


bool emberAfColorControlClusterStepHueCallback(app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
                                               const Commands::StepHue::DecodableType & commandData)
{
  FOCUSLOG("=== Received StepHue Command");
  auto dev = DeviceEndpoints::getDevice<DeviceColorControl>(commandPath.mEndpointId);
  if (!dev) return false;
  if (commandData.stepSize==0) return false; // cannot step, InvalidCommand
  if (dev->shouldExecuteColorChange(commandData.optionsMask, commandData.optionsOverride)) {
    dev->updateCurrentHue(commandData.stepSize, updateModeForStep(commandData.stepMode), commandData.transitionTime);
  }
  commandObj->AddStatus(commandPath, Status::Success);
  return true;
}


#if ENHANCED_HUE

bool emberAfColorControlClusterEnhancedMoveHueCallback(app::CommandHandler * commandObj,
                                                       const app::ConcreteCommandPath & commandPath,
                                                       const Commands::EnhancedMoveHue::DecodableType & commandData)
{
  // FIXME: implement
  FOCUSLOG("=== Received EnhancedMoveHue Command - NOT IMPLEMENTED YET");
  return false;
}


bool emberAfColorControlClusterEnhancedMoveToHueCallback(app::CommandHandler * commandObj,
                                                         const app::ConcreteCommandPath & commandPath,
                                                         const Commands::EnhancedMoveToHue::DecodableType & commandData)
{
  // FIXME: implement
  FOCUSLOG("=== Received EnhancedMoveToHue Command - NOT IMPLEMENTED YET");
  return false;
}


bool emberAfColorControlClusterEnhancedStepHueCallback(app::CommandHandler * commandObj,
                                                       const app::ConcreteCommandPath & commandPath,
                                                       const Commands::EnhancedStepHue::DecodableType & commandData)
{
  // FIXME: implement
  FOCUSLOG("=== Received EnhancedStepHue Command - NOT IMPLEMENTED YET");
  return false;
}

#endif // ENHANCED_HUE


// MARK: saturation

bool emberAfColorControlClusterMoveSaturationCallback(app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
                                                      const Commands::MoveSaturation::DecodableType & commandData)
{
  FOCUSLOG("=== Received MoveSaturation Command");
  auto dev = DeviceEndpoints::getDevice<DeviceColorControl>(commandPath.mEndpointId);
  if (!dev) return false;
  if (!moveCheck(commandData.moveMode, commandData.rate)) return false; // cannot move, InvalidCommand
  if (dev->shouldExecuteColorChange(commandData.optionsMask, commandData.optionsOverride)) {
    dev->updateCurrentSaturation(0, updateModeForMove(commandData.moveMode), commandData.rate);
  }
  commandObj->AddStatus(commandPath, Status::Success);
  return true;
}


bool emberAfColorControlClusterMoveToSaturationCallback(app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
                                                        const Commands::MoveToSaturation::DecodableType & commandData)
{
  FOCUSLOG("=== Received MoveToSaturation Command");
  auto dev = DeviceEndpoints::getDevice<DeviceColorControl>(commandPath.mEndpointId);
  if (!dev) return false;
  if (dev->shouldExecuteColorChange(commandData.optionsMask, commandData.optionsOverride)) {
    dev->updateCurrentSaturation(commandData.saturation, updateModeForCommand(), commandData.transitionTime);
  }
  commandObj->AddStatus(commandPath, Status::Success);
  return true;
}


bool emberAfColorControlClusterStepSaturationCallback(app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
                                                      const Commands::StepSaturation::DecodableType & commandData)
{
  FOCUSLOG("=== Received StepSaturation Command");
  auto dev = DeviceEndpoints::getDevice<DeviceColorControl>(commandPath.mEndpointId);
  if (!dev) return false;
  if (commandData.stepSize==0) return false; // cannot step, InvalidCommand
  if (dev->shouldExecuteColorChange(commandData.optionsMask, commandData.optionsOverride)) {
    dev->updateCurrentSaturation(commandData.stepSize, updateModeForStep(commandData.stepMode), commandData.transitionTime);
  }
  commandObj->AddStatus(commandPath, Status::Success);
  return true;
}


// MARK: hue+saturation together

bool emberAfColorControlClusterMoveToHueAndSaturationCallback(app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
                                                              const Commands::MoveToHueAndSaturation::DecodableType & commandData)
{
  FOCUSLOG("=== Received MoveToHueAndSaturation Command");
  auto dev = DeviceEndpoints::getDevice<DeviceColorControl>(commandPath.mEndpointId);
  if (!dev) return false;
  if (dev->shouldExecuteColorChange(commandData.optionsMask, commandData.optionsOverride)) {
    dev->updateCurrentSaturation(commandData.saturation, updateModeForCommand(UpdateMode(UpdateFlags::forced, UpdateFlags::noapply)), commandData.transitionTime);
    dev->updateCurrentHue(commandData.hue, updateModeForCommand(UpdateMode(UpdateFlags::forced)), commandData.transitionTime);
  }
  commandObj->AddStatus(commandPath, Status::Success);
  return true;
}


#if ENHANCED_HUE

bool emberAfColorControlClusterEnhancedMoveToHueAndSaturationCallback(
    app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
    const Commands::EnhancedMoveToHueAndSaturation::DecodableType & commandData)
{
  // FIXME: implement
  FOCUSLOG("=== Received EnhancedMoveToHueAndSaturation Command - NOT IMPLEMENTED YET");
  return false;
}

#endif // ENHANCED_HUE


// MARK: color loop

bool emberAfColorControlClusterColorLoopSetCallback(app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
                                                    const Commands::ColorLoopSet::DecodableType & commandData)
{
  // TODO: maybe implement (is optional feature)
  FOCUSLOG("=== Received ColorLoopSet Command - NOT SUPPORTED");
  return false;
}

#endif // MATTER_DM_PLUGIN_COLOR_CONTROL_SERVER_HSV


#ifdef MATTER_DM_PLUGIN_COLOR_CONTROL_SERVER_XY

// MARK: X/Y color

bool emberAfColorControlClusterMoveToColorCallback(app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
                                                   const Commands::MoveToColor::DecodableType & commandData)
{
  FOCUSLOG("=== Received MoveToColor Command");
  auto dev = DeviceEndpoints::getDevice<DeviceColorControl>(commandPath.mEndpointId);
  if (!dev) return false;
  if (dev->shouldExecuteColorChange(commandData.optionsMask, commandData.optionsOverride)) {
    dev->updateCurrentX(commandData.colorX, updateModeForCommand(UpdateMode(UpdateFlags::forced, UpdateFlags::noapply)), commandData.transitionTime);
    dev->updateCurrentY(commandData.colorY, updateModeForCommand(UpdateMode(UpdateFlags::forced)), commandData.transitionTime);
  }
  commandObj->AddStatus(commandPath, Status::Success);
  return true;
}


bool emberAfColorControlClusterMoveColorCallback(app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
                                                 const Commands::MoveColor::DecodableType & commandData)
{
  FOCUSLOG("=== Received MoveColor Command");
  auto dev = DeviceEndpoints::getDevice<DeviceColorControl>(commandPath.mEndpointId);
  if (commandData.rateX==0 && commandData.rateY==0) return false; // cannot move, InvalidCommand
  if (!dev) return false;
  if (dev->shouldExecuteColorChange(commandData.optionsMask, commandData.optionsOverride)) {
    dev->updateCurrentX(0, updateModeForDirectionalXY(commandData.rateX, UpdateMode(UpdateFlags::move, UpdateFlags::forced, UpdateFlags::noapply)), abs(commandData.rateX));
    dev->updateCurrentY(0, updateModeForDirectionalXY(commandData.rateY, UpdateMode(UpdateFlags::move, UpdateFlags::forced)), abs(commandData.rateY));
  }
  commandObj->AddStatus(commandPath, Status::Success);
  return true;
}


bool emberAfColorControlClusterStepColorCallback(app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
                                                 const Commands::StepColor::DecodableType & commandData)
{
  FOCUSLOG("=== Received StepColor Command");
  auto dev = DeviceEndpoints::getDevice<DeviceColorControl>(commandPath.mEndpointId);
  if (!dev) return false;
  if (commandData.stepX==0 && commandData.stepY==0) return false; // cannot step, InvalidCommand
  if (dev->shouldExecuteColorChange(commandData.optionsMask, commandData.optionsOverride)) {
    dev->updateCurrentX(0, updateModeForDirectionalXY(commandData.stepX, UpdateMode(UpdateFlags::relative, UpdateFlags::forced, UpdateFlags::noapply)), commandData.transitionTime);
    dev->updateCurrentY(0, updateModeForDirectionalXY(commandData.stepY, UpdateMode(UpdateFlags::relative, UpdateFlags::forced)), commandData.transitionTime);
  }
  commandObj->AddStatus(commandPath, Status::Success);
  return true;
}

#endif // MATTER_DM_PLUGIN_COLOR_CONTROL_SERVER_XY


#ifdef MATTER_DM_PLUGIN_COLOR_CONTROL_SERVER_TEMP

bool emberAfColorControlClusterMoveToColorTemperatureCallback(app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
                                                              const Commands::MoveToColorTemperature::DecodableType & commandData)
{
  FOCUSLOG("=== Received MoveToColorTemperature Command");
  auto dev = DeviceEndpoints::getDevice<DeviceColorControl>(commandPath.mEndpointId);
  if (!dev) return false;
  if (dev->shouldExecuteColorChange(commandData.optionsMask, commandData.optionsOverride)) {
    dev->updateCurrentColortemp(commandData.colorTemperatureMireds, updateModeForCommand(), commandData.transitionTime);
  }
  commandObj->AddStatus(commandPath, Status::Success);
  return true;
}

bool emberAfColorControlClusterMoveColorTemperatureCallback(app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
                                                            const Commands::MoveColorTemperature::DecodableType & commandData)
{
  FOCUSLOG("=== Received MoveColorTemperature Command");
  auto dev = DeviceEndpoints::getDevice<DeviceColorControl>(commandPath.mEndpointId);
  if (!dev) return false;
  if (!moveCheck(commandData.moveMode, commandData.rate)) return false; // cannot move, InvalidCommand
  if (dev->shouldExecuteColorChange(commandData.optionsMask, commandData.optionsOverride)) {
    dev->updateCurrentColortemp(0, updateModeForMove(commandData.moveMode), commandData.rate);
  }
  commandObj->AddStatus(commandPath, Status::Success);
  return true;
}

bool emberAfColorControlClusterStepColorTemperatureCallback(app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
                                                            const Commands::StepColorTemperature::DecodableType & commandData)
{
  FOCUSLOG("=== Received StepColorTemperature Command");
  auto dev = DeviceEndpoints::getDevice<DeviceColorControl>(commandPath.mEndpointId);
  if (!dev) return false;
  if (commandData.stepSize==0) return false; // cannot step, InvalidCommand
  if (dev->shouldExecuteColorChange(commandData.optionsMask, commandData.optionsOverride)) {
    dev->updateCurrentColortemp(
      commandData.stepSize, updateModeForStep(commandData.stepMode), commandData.transitionTime,
      commandData.colorTemperatureMinimumMireds, commandData.colorTemperatureMaximumMireds
    );
  }
  commandObj->AddStatus(commandPath, Status::Success);
  return true;
}

void emberAfPluginLevelControlCoupledColorTempChangeCallback(EndpointId endpoint)
{
  // FIXME: implement
  FOCUSLOG("=== Received emberAfPluginLevelControlCoupledColorTempChangeCallback - NOT IMPLEMENTED YET");
  // TODO: implement
//    ColorControlServer::Instance().levelControlColorTempChangeCommand(endpoint);
}

#endif // MATTER_DM_PLUGIN_COLOR_CONTROL_SERVER_TEMP

bool emberAfColorControlClusterStopMoveStepCallback(app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
                                                    const Commands::StopMoveStep::DecodableType & commandData)
{
  FOCUSLOG("=== Received StopMoveStep Command");
  auto dev = DeviceEndpoints::getDevice<DeviceColorControl>(commandPath.mEndpointId);
  if (!dev) return false;
  if (dev->shouldExecuteColorChange(commandData.optionsMask, commandData.optionsOverride)) {
    dev->stopColorMovements();
  }
  commandObj->AddStatus(commandPath, Status::Success);
  return true;
}


void emberAfColorControlClusterServerInitCallback(EndpointId endpoint)
{
  #ifdef MATTER_DM_PLUGIN_SCENES
  // Registers Scene handlers for the color control cluster on the server
  app::Clusters::Scenes::ScenesServer::Instance().RegisterSceneHandler(endpoint, ColorControlServer::GetSceneHandler());
  #endif // MATTER_DM_PLUGIN_SCENES
}


#ifdef MATTER_DM_PLUGIN_COLOR_CONTROL_SERVER_TEMP
/**
 * @brief Callback for temperature update when timer is finished
 *
 * @param endpoint endpointId
 */
void emberAfPluginColorControlServerTempTransitionEventHandler(EndpointId endpoint)
{
  // TODO: check if we need this
}
#endif // MATTER_DM_PLUGIN_COLOR_CONTROL_SERVER_TEMP

#ifdef MATTER_DM_PLUGIN_COLOR_CONTROL_SERVER_XY
/**
 * @brief Callback for color update when timer is finished
 *
 * @param endpoint endpointId
 */
void emberAfPluginColorControlServerXyTransitionEventHandler(EndpointId endpoint)
{
  // TODO: check if we need this
}
#endif // MATTER_DM_PLUGIN_COLOR_CONTROL_SERVER_XY

#ifdef MATTER_DM_PLUGIN_COLOR_CONTROL_SERVER_HSV
/**
 * @brief Callback for color hue and saturation update when timer is finished
 *
 * @param endpoint endpointId
 */
void emberAfPluginColorControlServerHueSatTransitionEventHandler(EndpointId endpoint)
{
  // TODO: check if we need this
}
#endif // MATTER_DM_PLUGIN_COLOR_CONTROL_SERVER_HSV

void MatterColorControlPluginServerInitCallback()
{
  // NOP for now
}

void MatterColorControlClusterServerShutdownCallback(EndpointId endpoint)
{
  // NOP for now
}



// MARK: Attribute access

Status DeviceColorControl::handleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength)
{
  if (clusterId==ColorControl::Id) {
    // create non-unknown default if needed
    InternalColorMode cm = mColorMode==InternalColorMode::unknown_mode ? (ctOnly() ? InternalColorMode::ct : InternalColorMode::hs) : mColorMode;
    if (attributeId == ColorControl::Attributes::ColorMode::Id) {
      // color mode: 0=Hue+Sat (normal and enhanced!), 1=XY, 2=Colortemp
      return getAttr<uint8_t>(buffer, maxReadLength, to_underlying(mColorMode==InternalColorMode::enhanced_hs ? InternalColorMode::hs : cm));
    }
    if (attributeId == ColorControl::Attributes::EnhancedColorMode::Id) {
      // TODO: this is already prepared for EnhancedHue, which is not yet implemented itself
      // color mode: 0=Hue+Sat, 1=XY, 2=Colortemp, 3=EnhancedHue+Sat
      return getAttr<uint8_t>(buffer, maxReadLength, to_underlying(cm));
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
  }
  // let base class try
  return inherited::handleReadAttribute(clusterId, attributeId, buffer, maxReadLength);
}


Status DeviceColorControl::handleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer)
{
  if (clusterId==ColorControl::Id) {
    /* NOP */
  }
  // let base class try
  return inherited::handleWriteAttribute(clusterId, attributeId, buffer);
}


string DeviceColorControl::description()
{
  string s = inherited::description();
  string_format_append(s, "\n- colormode: %d", to_underlying(mColorMode));
  string_format_append(s, "\n- hue: %d", mHue);
  string_format_append(s, "\n- saturation: %d", mSaturation);
  string_format_append(s, "\n- ct: %d", mColorTemp);
  string_format_append(s, "\n- X: %d", mX);
  string_format_append(s, "\n- Y: %d", mY);
  return s;
}


#ifdef MATTER_DM_PLUGIN_SCENES

// MARK: - Scene control
// TODO: Modularize color-control server
// - for now this is extracted from app/clusters/color-control as the original
//   cluster does not allow overriding the lower level (actual transition stepping) parts of
//   the cluster, which are NOT suitable for remote hardware control in bridging apps

// MARK: Adapter from scene mechanics to our own implementation of Color-Control

/// This implementation provides the same signature as the static function in level-control.cpp
/// to the scene handler implementation


// MARK: copied from app/clusters/color-control-server

#include <app/clusters/scenes-server/scenes-server.h>

class DefaultColorControlSceneHandler : public scenes::DefaultSceneHandlerImpl
{
public:
    // As per spec, 9 attributes are scenable in the color control cluster, if new scenables attributes are added, this value should
    // be updated.
    static constexpr uint8_t kColorControlScenableAttributesCount = 9;

    DefaultColorControlSceneHandler() = default;
    ~DefaultColorControlSceneHandler() override {}

    // Default function for ColorControl cluster, only puts the ColorControl cluster ID in the span if supported on the caller
    // endpoint
    void GetSupportedClusters(EndpointId endpoint, Span<ClusterId> & clusterBuffer) override
    {
        ClusterId * buffer = clusterBuffer.data();
        if (emberAfContainsServer(endpoint, ColorControl::Id) && clusterBuffer.size() >= 1)
        {
            buffer[0] = ColorControl::Id;
            clusterBuffer.reduce_size(1);
        }
        else
        {
            clusterBuffer.reduce_size(0);
        }
    }

    // Default function for ColorControl cluster, only checks if ColorControl is enabled on the endpoint
    bool SupportsCluster(EndpointId endpoint, ClusterId cluster) override
    {
        return (cluster == ColorControl::Id) && (emberAfContainsServer(endpoint, ColorControl::Id));
    }

    /// @brief Serialize the Cluster's EFS value
    /// @param endpoint target endpoint
    /// @param cluster  target cluster
    /// @param serializedBytes data to serialize into EFS
    /// @return CHIP_NO_ERROR if successfully serialized the data, CHIP_ERROR_INVALID_ARGUMENT otherwise
    CHIP_ERROR SerializeSave(EndpointId endpoint, ClusterId cluster, MutableByteSpan & serializedBytes) override
    {
        using AttributeValuePair = Scenes::Structs::AttributeValuePair::Type;

        AttributeValuePair pairs[kColorControlScenableAttributesCount];

        size_t attributeCount = 0;

        // obtain p44mbrd device
        auto dev = DeviceEndpoints::getDevice<DeviceColorControl>(endpoint);
        if (!dev) return CHIP_ERROR_INTERNAL;

        if (dev->hasFeature(ColorControl::Feature::kXy))
        {
            uint16_t xValue;
            if (Status::Success != Attributes::CurrentX::Get(endpoint, &xValue))
            {
                xValue = 0x616B; // Default X value according to spec
            }
            AddAttributeValuePair(pairs, Attributes::CurrentX::Id, xValue, attributeCount);

            uint16_t yValue;
            if (Status::Success != Attributes::CurrentY::Get(endpoint, &yValue))
            {
                yValue = 0x607D; // Default Y value according to spec
            }
            AddAttributeValuePair(pairs, Attributes::CurrentY::Id, yValue, attributeCount);
        }

        if (dev->hasFeature(ColorControl::Feature::kEnhancedHue))
        {
            uint16_t hueValue = 0x0000;
            Attributes::EnhancedCurrentHue::Get(endpoint, &hueValue);
            AddAttributeValuePair(pairs, Attributes::EnhancedCurrentHue::Id, hueValue, attributeCount);
        }

        if (dev->hasFeature(ColorControl::Feature::kHueAndSaturation))
        {
            uint8_t saturationValue;
            if (Status::Success != Attributes::CurrentSaturation::Get(endpoint, &saturationValue))
            {
                saturationValue = 0x00;
            }
            AddAttributeValuePair(pairs, Attributes::CurrentSaturation::Id, saturationValue, attributeCount);
        }

        if (dev->hasFeature(ColorControl::Feature::kColorLoop))
        {
            uint8_t loopActiveValue;
            if (Status::Success != Attributes::ColorLoopActive::Get(endpoint, &loopActiveValue))
            {
                loopActiveValue = 0x00;
            }
            AddAttributeValuePair(pairs, Attributes::ColorLoopActive::Id, loopActiveValue, attributeCount);

            uint8_t loopDirectionValue;
            if (Status::Success != Attributes::ColorLoopDirection::Get(endpoint, &loopDirectionValue))
            {
                loopDirectionValue = 0x00;
            }
            AddAttributeValuePair(pairs, Attributes::ColorLoopDirection::Id, loopDirectionValue, attributeCount);

            uint16_t loopTimeValue;
            if (Status::Success != Attributes::ColorLoopTime::Get(endpoint, &loopTimeValue))
            {
                loopTimeValue = 0x0019; // Default loop time value according to spec
            }
            AddAttributeValuePair(pairs, Attributes::ColorLoopTime::Id, loopTimeValue, attributeCount);
        }

        if (dev->hasFeature(ColorControl::Feature::kColorTemperature))
        {
            uint16_t temperatureValue;
            if (Status::Success != Attributes::ColorTemperatureMireds::Get(endpoint, &temperatureValue))
            {
                temperatureValue = 0x00FA; // Default temperature value according to spec
            }
            AddAttributeValuePair(pairs, Attributes::ColorTemperatureMireds::Id, temperatureValue, attributeCount);
        }

        uint8_t modeValue;
        if (Status::Success != Attributes::EnhancedColorMode::Get(endpoint, &modeValue))
        {
          modeValue = to_underlying(DeviceColorControl::InternalColorMode::xy); // Default mode value according to spec
        }
        AddAttributeValuePair(pairs, Attributes::EnhancedColorMode::Id, modeValue, attributeCount);

        app::DataModel::List<AttributeValuePair> attributeValueList(pairs, attributeCount);

        return EncodeAttributeValueList(attributeValueList, serializedBytes);
    }

    /// @brief Default EFS interaction when applying scene to the ColorControl Cluster
    /// @param endpoint target endpoint
    /// @param cluster  target cluster
    /// @param serializedBytes Data from nvm
    /// @param timeMs transition time in ms
    /// @return CHIP_NO_ERROR if value as expected, CHIP_ERROR_INVALID_ARGUMENT otherwise
    CHIP_ERROR ApplyScene(EndpointId endpoint, ClusterId cluster, const ByteSpan & serializedBytes,
                          scenes::TransitionTimeMs timeMs) override
    {
        app::DataModel::DecodableList<Scenes::Structs::AttributeValuePair::DecodableType> attributeValueList;

        // obtain p44mbrd device
        auto dev = DeviceEndpoints::getDevice<DeviceColorControl>(endpoint);
        if (!dev) return CHIP_ERROR_INTERNAL;

        ReturnErrorOnFailure(DecodeAttributeValueList(serializedBytes, attributeValueList));

        size_t attributeCount = 0;
        auto pair_iterator    = attributeValueList.begin();

        // The color control cluster should have a maximum of 9 scenable attributes
        ReturnErrorOnFailure(attributeValueList.ComputeSize(&attributeCount));
        VerifyOrReturnError(attributeCount <= kColorControlScenableAttributesCount, CHIP_ERROR_BUFFER_TOO_SMALL);

        // Initialize action attributes to default values in case they are not in the scene
        DeviceColorControl::InternalColorMode targetColorMode = DeviceColorControl::InternalColorMode::unknown_mode; // default: imply color mode from props set
        // uint8_t loopActiveValue    = 0x00;
        // uint8_t loopDirectionValue = 0x00;
        // uint16_t loopTimeValue     = 0x0019; // Default loop time value according to spec

        while (pair_iterator.Next())
        {
            auto & decodePair = pair_iterator.GetValue();

            switch (decodePair.attributeID)
            {
            case Attributes::CurrentX::Id:
                if (dev->hasFeature(ColorControl::Feature::kXy)) {
                  if (decodePair.attributeValue) {
                    dev->updateCurrentX(static_cast<uint16_t>(decodePair.attributeValue), UpdateMode(UpdateFlags::noapply), 0);
                  }
                }
                break;
            case Attributes::CurrentY::Id:
                if (dev->hasFeature(ColorControl::Feature::kXy)) {
                  dev->updateCurrentY(static_cast<uint16_t>(decodePair.attributeValue), UpdateMode(UpdateFlags::noapply), 0);
                }
                break;
            case Attributes::EnhancedCurrentHue::Id:
                if (dev->hasFeature(ColorControl::Feature::kEnhancedHue)) {
                  // TODO: implement enhanced hue
                  //colorHueTransitionState->finalEnhancedHue = static_cast<uint16_t>(decodePair.attributeValue);
                }
                break;
            case Attributes::CurrentHue::Id:
                if (dev->hasFeature(ColorControl::Feature::kHueAndSaturation)) {
                  dev->updateCurrentHue(static_cast<uint8_t>(decodePair.attributeValue), UpdateMode(UpdateFlags::noapply), 0);
                }
                break;
            case Attributes::CurrentSaturation::Id:
                if (dev->hasFeature(ColorControl::Feature::kHueAndSaturation)) {
                  dev->updateCurrentSaturation(static_cast<uint8_t>(decodePair.attributeValue), UpdateMode(UpdateFlags::noapply), 0);
                }
                break;
            case Attributes::ColorLoopActive::Id:
                if (dev->hasFeature(ColorControl::Feature::kColorLoop)) {
                  // TODO: implement color loops, maybe
                  // loopActiveValue = static_cast<uint8_t>(decodePair.attributeValue);
                }
                break;
            case Attributes::ColorLoopDirection::Id:
                if (dev->hasFeature(ColorControl::Feature::kColorLoop)) {
                  // TODO: implement color loops, maybe
                  // loopDirectionValue = static_cast<uint8_t>(decodePair.attributeValue);
                }
                break;
            case Attributes::ColorLoopTime::Id:
                if (dev->hasFeature(ColorControl::Feature::kColorLoop)) {
                  // TODO: implement color loops, maybe
                  // loopTimeValue = static_cast<uint16_t>(decodePair.attributeValue);
                }
                break;
            case Attributes::ColorTemperatureMireds::Id:
                if (dev->hasFeature(ColorControl::Feature::kColorTemperature)) {
                  dev->updateCurrentColortemp(static_cast<uint16_t>(decodePair.attributeValue), UpdateMode(UpdateFlags::noapply), 0);
                }
                break;
            case Attributes::EnhancedColorMode::Id:
                if (decodePair.attributeValue <= static_cast<uint8_t>(DeviceColorControl::InternalColorMode::ct))
                {
                  targetColorMode = static_cast<DeviceColorControl::InternalColorMode>(decodePair.attributeValue);
                }
                break;
            default:
                return CHIP_ERROR_INVALID_ARGUMENT;
                break;
            }
        }
        ReturnErrorOnFailure(pair_iterator.GetStatus());

        // Determine the final color mode
        if (targetColorMode==DeviceColorControl::InternalColorMode::unknown_mode) {
          // use implied
          targetColorMode = dev->currentColorMode();
        }

        uint16_t transitionTime10th = static_cast<uint16_t>(timeMs / 100);

        // apply (forced to make sure all components are updated to the bridge)
        dev->updateCurrentColorMode(
          targetColorMode,
          UpdateMode(UpdateFlags::bridged, UpdateFlags::matter, UpdateFlags::forced),
          transitionTime10th
        );

        return CHIP_NO_ERROR;
    }

private:

    void AddAttributeValuePair(Scenes::Structs::AttributeValuePair::Type * pairs, AttributeId id, uint32_t value,
                               size_t & attributeCount)
    {
        pairs[attributeCount].attributeID    = id;
        pairs[attributeCount].attributeValue = value;
        attributeCount++;
    }
};


static DefaultColorControlSceneHandler sColorControlSceneHandler;

namespace ColorControlServer {

  chip::scenes::SceneHandler * GetSceneHandler()
  {
    #ifdef MATTER_DM_PLUGIN_SCENES
    return &sColorControlSceneHandler;
    #else
    return nullptr;
    #endif // MATTER_DM_PLUGIN_SCENES
  }

} // namespace ColorControlServer

#endif // MATTER_DM_PLUGIN_SCENES
