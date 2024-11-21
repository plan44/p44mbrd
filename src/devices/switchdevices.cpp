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
#include "switchdevices.h"


using namespace Clusters;

// MARK: - SwitchDevice

static const EmberAfDeviceType gGenericSwitchTypes[] = {
  { DEVICE_TYPE_MA_GENERIC_SWITCH, DEVICE_VERSION_DEFAULT },
  { DEVICE_TYPE_MA_BRIDGED_DEVICE, DEVICE_VERSION_DEFAULT }
};

static EmberAfClusterSpec gGenericSwitchClusters[] = { { Switch::Id, CLUSTER_MASK_SERVER } };


SwitchDevice::SwitchDevice(IdentifyDelegate* aIdentifyDelegateP, DeviceInfoDelegate& aDeviceInfoDelegate) :
  inherited(aIdentifyDelegateP, aDeviceInfoDelegate)
{
  // - declare device specific clusters
  useClusterTemplates(Span<EmberAfClusterSpec>(gGenericSwitchClusters));
}


void SwitchDevice::setActivePosition(int aPosition, const string& aPositionName)
{
  mActivePositions[aPosition] = aPositionName;
}


// MARK: - DevicePushbutton


bool DevicePushbutton::finalizeDeviceDeclaration()
{
  return finalizeDeviceDeclarationWithTypes(Span<const EmberAfDeviceType>(gGenericSwitchTypes));
}

