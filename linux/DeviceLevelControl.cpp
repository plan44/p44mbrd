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


bool DeviceLevelControl::setCurrentLevel(uint8_t aNewLevel)
{
  ChipLogProgress(DeviceLayer, "Device[%s]: set level to %d", mName, aNewLevel);
  if (aNewLevel!=mLevel) {
    mLevel = aNewLevel;
    // adjust default channel
    JsonObjectPtr params = JsonObject::newObj();
    params->add("dSUID", JsonObject::newString(mBridgedDSUID));
    params->add("channel", JsonObject::newInt32(0)); // default channel
    params->add("value", JsonObject::newDouble((double)mLevel*100/0xFE));
    params->add("apply_now", JsonObject::newBool(true));
    BridgeApi::sharedBridgeApi().notify("setOutputChannelValue", params);
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
      setCurrentLevel(*buffer);
      return EMBER_ZCL_STATUS_SUCCESS;
    }
  }
  // let base class try
  return inherited::HandleWriteAttribute(clusterId, attributeId, buffer);
}
