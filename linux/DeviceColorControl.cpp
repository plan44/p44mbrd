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

#include "devicecolorcontrol.h"
#include "device_impl.h"

// MARK: - LevelControl Device specific declarations

// REVISION DEFINITIONS:
// TODO: move these to a better place, probably into the devices that actually handle them, or
//   try to extract them from ZAP-generated defs
// =================================================================================

#define ZCL_COLOR_CONTROL_CLUSTER_REVISION (5u)

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(colorControlAttrs)
  DECLARE_DYNAMIC_ATTRIBUTE(ZCL_COLOR_CONTROL_CURRENT_HUE_ATTRIBUTE_ID, INT8U, 1, 0), /* current hue */
  DECLARE_DYNAMIC_ATTRIBUTE(ZCL_COLOR_CONTROL_CURRENT_SATURATION_ATTRIBUTE_ID, INT8U, 1, 0), /* current saturation */
  DECLARE_DYNAMIC_ATTRIBUTE(ZCL_COLOR_CONTROL_COLOR_TEMPERATURE_ATTRIBUTE_ID, INT16U, 2, 0), /* current color temperature */
  DECLARE_DYNAMIC_ATTRIBUTE(ZCL_COLOR_CONTROL_CURRENT_X_ATTRIBUTE_ID, INT16U, 2, 0), /* current X */
  DECLARE_DYNAMIC_ATTRIBUTE(ZCL_COLOR_CONTROL_CURRENT_Y_ATTRIBUTE_ID, INT16U, 2, 0), /* current Y */
  DECLARE_DYNAMIC_ATTRIBUTE(ZCL_COLOR_CONTROL_COLOR_MODE_ATTRIBUTE_ID, ENUM8, 1, 0), /* current color mode: see ColorMode enum */
//    DECLARE_DYNAMIC_ATTRIBUTE(ZCL_COLOR_CONTROL_ENHANCED_CURRENT_HUE_ATTRIBUTE_ID, ENUM8, 1, 0), /* current color mode: 0=HS, 1=XY, 2=Colortemp */
DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();
// TODO: other important capabilities
// ZCL_COLOR_CONTROL_COLOR_CAPABILITIES_ATTRIBUTE_ID type MAP16 (Bit0=HS, Bit1=EnhancedHS, Bit2=ColorLoop, Bit3=XY, Bit4=ColorTemp)

constexpr CommandId colorControlIncomingCommands[] = {
  app::Clusters::ColorControl::Commands::MoveToHue::Id,
  app::Clusters::ColorControl::Commands::MoveHue::Id,
  app::Clusters::ColorControl::Commands::StepHue::Id,
  app::Clusters::ColorControl::Commands::MoveToSaturation::Id,
  app::Clusters::ColorControl::Commands::MoveSaturation::Id,
  app::Clusters::ColorControl::Commands::StepSaturation::Id,
  app::Clusters::ColorControl::Commands::MoveToHueAndSaturation::Id,
  app::Clusters::ColorControl::Commands::MoveToColorTemperature::Id,
  app::Clusters::ColorControl::Commands::MoveColorTemperature::Id,
  app::Clusters::ColorControl::Commands::StepColorTemperature::Id,
  kInvalidCommandId,
};

// MARK: ct/color light
DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(colorLightClusters)
  DECLARE_DYNAMIC_CLUSTER(ZCL_COLOR_CONTROL_CLUSTER_ID, colorControlAttrs, colorControlIncomingCommands, nullptr),
DECLARE_DYNAMIC_CLUSTER_LIST_END;

const EmberAfDeviceType gColorLightTypes[] = {
  { DEVICE_TYPE_MA_COLOR_LIGHT, DEVICE_VERSION_DEFAULT },
  { DEVICE_TYPE_BRIDGED_NODE, DEVICE_VERSION_DEFAULT }
};


// MARK: - DeviceColorControl

DeviceColorControl::DeviceColorControl(bool aCTOnly) :
  mCtOnly(aCTOnly),
  mColorMode(aCTOnly ? colormode_ct : colormode_hs),
  mHue(0),
  mSaturation(0),
  mColorTemp(0)
{
  // - declare specific clusters
  addClusterDeclarations(Span<EmberAfCluster>(colorLightClusters));
}

void DeviceColorControl::finalizeDeviceDeclaration()
{
  finalizeDeviceDeclarationWithTypes(Span<const EmberAfDeviceType>(gColorLightTypes));
}

void DeviceColorControl::initBridgedInfo(JsonObjectPtr aDeviceInfo)
{
  inherited::initBridgedInfo(aDeviceInfo);
  // no extra info at this level so far -> NOP
}


void DeviceColorControl::parseChannelStates(JsonObjectPtr aChannelStates, UpdateMode aUpdateMode)
{
  inherited::DeviceOnOff::parseChannelStates(aChannelStates, aUpdateMode);
  JsonObjectPtr o;
  // need to determine color mode
  ColorMode colorMode = colormode_unknown;
  if (aChannelStates->get("colortemp", o)) {
    JsonObjectPtr vo;
    if (o->get("age", vo, true)) colorMode = colormode_ct; // age is non-null -> component detemines colormode
    if (o->get("value", vo, true)) {
      updateCurrentColortemp(vo->doubleValue()/100*0xFE, colorMode==colormode_ct ? aUpdateMode : UpdateMode());
    }
  }
  if (!mCtOnly) {
    if (aChannelStates->get("hue", o)) {
      JsonObjectPtr vo;
      if (o->get("age", vo, true)) colorMode = colormode_hs; // age is non-null -> component detemines colormode
      if (o->get("value", vo, true)) {
        // update only cache if not actually in hs mode
        updateCurrentHue(vo->doubleValue()/360*0xFE, colorMode==colormode_hs ? aUpdateMode : UpdateMode());
      }
    }
    if (aChannelStates->get("saturation", o)) {
      JsonObjectPtr vo;
      if (o->get("value", vo, true)) {
        // update only cache if not actually in hs mode
        updateCurrentSaturation(vo->doubleValue()/100*0xFE, colorMode==colormode_hs ? aUpdateMode : UpdateMode());
      }
    }
    if (aChannelStates->get("x", o)) {
      JsonObjectPtr vo;
      if (o->get("age", vo, true)) colorMode = colormode_xy; // age is non-null -> component detemines colormode
      if (o->get("value", vo, true)) {
        // update only cache if not actually in hs mode
        updateCurrentX(vo->doubleValue()*0xFFFE, colorMode==colormode_xy ? aUpdateMode : UpdateMode());
      }
    }
    if (aChannelStates->get("y", o)) {
      JsonObjectPtr vo;
      if (o->get("value", vo, true)) {
        // update only cache if not actually in hs mode
        updateCurrentY(vo->doubleValue()*0xFFFE, colorMode==colormode_xy ? aUpdateMode : UpdateMode());
      }
    }
  }
  // now update color mode
  updateCurrentColorMode(colorMode, aUpdateMode);
}



EmberAfStatus DeviceColorControl::HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength)
{
  if (clusterId==ZCL_COLOR_CONTROL_CLUSTER_ID) {
    if ((attributeId == ZCL_CLUSTER_REVISION_SERVER_ATTRIBUTE_ID) && (maxReadLength == 2)) {
      *((uint16_t *)buffer) = ZCL_COLOR_CONTROL_CLUSTER_REVISION;
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if ((attributeId == ZCL_COLOR_CONTROL_COLOR_CAPABILITIES_ATTRIBUTE_ID) && (maxReadLength == 1)) {
      // color capabilities: Bit0=HS, Bit1=EnhancedHS, Bit2=ColorLoop, Bit3=XY, Bit4=ColorTemp
      *buffer = (1<<4) | (mCtOnly ? 0 : (1<<0));
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if ((attributeId == ZCL_COLOR_CONTROL_COLOR_MODE_ATTRIBUTE_ID) && (maxReadLength == 1)) {
      // color mode: 0=HS, 1=XY, 2=Colortemp
      *buffer = mColorMode;
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if ((attributeId == ZCL_COLOR_CONTROL_CURRENT_HUE_ATTRIBUTE_ID) && (maxReadLength == 1)) {
      *buffer = currentHue();
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if ((attributeId == ZCL_COLOR_CONTROL_CURRENT_SATURATION_ATTRIBUTE_ID) && (maxReadLength == 1)) {
      *buffer = currentSaturation();
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    if (attributeId == ZCL_COLOR_CONTROL_COLOR_TEMPERATURE_ATTRIBUTE_ID) {
      if (maxReadLength == 2) {
        *((uint16_t *)buffer) = currentColortemp();
      }
      else if (maxReadLength == 1) {
        *((uint8_t *)buffer) = (uint8_t)currentColortemp();
      }
      return EMBER_ZCL_STATUS_SUCCESS;
    }
  }
  // let base class try
  return inherited::HandleReadAttribute(clusterId, attributeId, buffer, maxReadLength);
}


bool DeviceColorControl::updateCurrentColorMode(ColorMode aColorMode, UpdateMode aUpdateMode)
{
  if (aColorMode!=mColorMode || aUpdateMode.Has(UpdateFlags::forced)) {
    ChipLogProgress(DeviceLayer, "Device[%s]: color mode set to %d", GetName().c_str(), (int)aColorMode);
    mColorMode = aColorMode;
    if (aUpdateMode.Has(UpdateFlags::bridged)) {
      JsonObjectPtr params = JsonObject::newObj();
      switch (mColorMode) {
        case colormode_hs:
          updateCurrentHue(mHue, UpdateMode(UpdateFlags::forced, UpdateFlags::bridged, UpdateFlags::noapply));
          updateCurrentSaturation(mSaturation, UpdateMode(UpdateFlags::forced, UpdateFlags::bridged));
          break;
        case colormode_xy:
          updateCurrentX(mX, UpdateMode(UpdateFlags::forced, UpdateFlags::bridged, UpdateFlags::noapply));
          updateCurrentY(mY, UpdateMode(UpdateFlags::forced, UpdateFlags::bridged));
          break;
        default:
        case colormode_ct:
          updateCurrentColortemp(mColorTemp, UpdateMode(UpdateFlags::forced, UpdateFlags::bridged));
          break;
      }
    }
    if (aUpdateMode.Has(UpdateFlags::matter)) {
      MatterReportingAttributeChangeCallback(GetEndpointId(), ZCL_COLOR_CONTROL_CLUSTER_ID, ZCL_COLOR_CONTROL_COLOR_MODE_ATTRIBUTE_ID);
    }
    return true;
  }
  return false;
}


bool DeviceColorControl::updateCurrentHue(uint8_t aHue, UpdateMode aUpdateMode)
{
  if (aHue!=mHue || aUpdateMode.Has(UpdateFlags::forced)) {
    ChipLogProgress(DeviceLayer, "Device[%s]: set hue to %d", GetName().c_str(), aHue);
    mHue = aHue;
    if (!updateCurrentColorMode(colormode_hs, aUpdateMode)) {
      // color mode has not changed, must separately update hue (otherwise, color mode change already sends H+S)
      if (aUpdateMode.Has(UpdateFlags::bridged)) {
        JsonObjectPtr params = JsonObject::newObj();
        params->add("channelId", JsonObject::newString("hue"));
        params->add("value", JsonObject::newDouble((double)mHue*360/0xFE));
        params->add("apply_now", JsonObject::newBool(!aUpdateMode.Has(UpdateFlags::noapply)));
        notify("setOutputChannelValue", params);
      }
    }
    if (aUpdateMode.Has(UpdateFlags::matter)) {
      MatterReportingAttributeChangeCallback(GetEndpointId(), ZCL_COLOR_CONTROL_CLUSTER_ID, ZCL_COLOR_CONTROL_CURRENT_HUE_ATTRIBUTE_ID);
    }
    return true; // changed
  }
  return false; // no change
}


bool DeviceColorControl::updateCurrentSaturation(uint8_t aSaturation, UpdateMode aUpdateMode)
{
  if (aSaturation!=mSaturation || aUpdateMode.Has(UpdateFlags::forced)) {
    ChipLogProgress(DeviceLayer, "Device[%s]: set saturation to %d", GetName().c_str(), aSaturation);
    mSaturation = aSaturation;
    if (!updateCurrentColorMode(colormode_hs, aUpdateMode)) {
      // color mode has not changed, must separately update saturation (otherwise, color mode change already sends H+S)
      if (aUpdateMode.Has(UpdateFlags::bridged)) {
        JsonObjectPtr params = JsonObject::newObj();
        params->add("channelId", JsonObject::newString("saturation"));
        params->add("value", JsonObject::newDouble((double)mSaturation*100/0xFE));
        params->add("apply_now", JsonObject::newBool(!aUpdateMode.Has(UpdateFlags::noapply)));
        notify("setOutputChannelValue", params);
      }
    }
    if (aUpdateMode.Has(UpdateFlags::matter)) {
      MatterReportingAttributeChangeCallback(GetEndpointId(), ZCL_COLOR_CONTROL_CLUSTER_ID, ZCL_COLOR_CONTROL_CURRENT_SATURATION_ATTRIBUTE_ID);
    }
    return true; // changed
  }
  return false; // no change
}


bool DeviceColorControl::updateCurrentColortemp(uint8_t aColortemp, UpdateMode aUpdateMode)
{
  if (aColortemp!=mColorTemp || aUpdateMode.Has(UpdateFlags::forced)) {
    ChipLogProgress(DeviceLayer, "Device[%s]: set colortemp to %d", GetName().c_str(), aColortemp);
    mColorTemp = aColortemp;
    if (!updateCurrentColorMode(colormode_ct, aUpdateMode)) {
      // color mode has not changed, must separately update colortemp (otherwise, color mode change already sends CT)
      JsonObjectPtr params = JsonObject::newObj();
      params->add("channelId", JsonObject::newString("colortemp"));
      params->add("value", JsonObject::newDouble(mColorTemp)); // is in mireds
      params->add("apply_now", JsonObject::newBool(!aUpdateMode.Has(UpdateFlags::noapply)));
      BridgeApi::sharedBridgeApi().notify("setOutputChannelValue", params);
    }
    if (aUpdateMode.Has(UpdateFlags::matter)) {
      MatterReportingAttributeChangeCallback(GetEndpointId(), ZCL_COLOR_CONTROL_CLUSTER_ID, ZCL_COLOR_CONTROL_COLOR_TEMPERATURE_ATTRIBUTE_ID);
    }
    return true; // changed
  }
  return false; // no change
}


bool DeviceColorControl::updateCurrentX(uint16_t aX, UpdateMode aUpdateMode)
{
  if (aX!=mX || aUpdateMode.Has(UpdateFlags::forced)) {
    ChipLogProgress(DeviceLayer, "Device[%s]: set X to %d", GetName().c_str(), aX);
    mX = aX;
    if (!updateCurrentColorMode(colormode_xy, aUpdateMode)) {
      // color mode has not changed, must separately update X (otherwise, color mode change already sends X+Y)
      JsonObjectPtr params = JsonObject::newObj();
      params->add("channelId", JsonObject::newString("x"));
      params->add("value", JsonObject::newDouble((double)mX/0xFFFE));
      params->add("apply_now", JsonObject::newBool(!aUpdateMode.Has(UpdateFlags::noapply)));
      BridgeApi::sharedBridgeApi().notify("setOutputChannelValue", params);
    }
    if (aUpdateMode.Has(UpdateFlags::matter)) {
      MatterReportingAttributeChangeCallback(GetEndpointId(), ZCL_COLOR_CONTROL_CLUSTER_ID, ZCL_COLOR_CONTROL_CURRENT_X_ATTRIBUTE_ID);
    }
    return true; // changed
  }
  return false; // no change
}


bool DeviceColorControl::updateCurrentY(uint16_t aY, UpdateMode aUpdateMode)
{
  if (aY!=mY || aUpdateMode.Has(UpdateFlags::forced)) {
    ChipLogProgress(DeviceLayer, "Device[%s]: set Y to %d", GetName().c_str(), aY);
    mY = aY;
    if (!updateCurrentColorMode(colormode_xy, aUpdateMode)) {
      // color mode has not changed, must separately update Y (otherwise, color mode change already sends X+Y)
      JsonObjectPtr params = JsonObject::newObj();
      params->add("channelId", JsonObject::newString("y"));
      params->add("value", JsonObject::newDouble((double)mY/0xFFFE));
      params->add("apply_now", JsonObject::newBool(!aUpdateMode.Has(UpdateFlags::noapply)));
      BridgeApi::sharedBridgeApi().notify("setOutputChannelValue", params);
    }
    if (aUpdateMode.Has(UpdateFlags::matter)) {
      MatterReportingAttributeChangeCallback(GetEndpointId(), ZCL_COLOR_CONTROL_CLUSTER_ID, ZCL_COLOR_CONTROL_CURRENT_Y_ATTRIBUTE_ID);
    }
    return true; // changed
  }
  return false; // no change
}


EmberAfStatus DeviceColorControl::HandleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer)
{
  // TODO: implement
  // let base class try
  return inherited::HandleWriteAttribute(clusterId, attributeId, buffer);
}


