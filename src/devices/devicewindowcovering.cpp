//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#include "devicewindowcovering.h"

#include <app/clusters/window-covering-server/window-covering-server.h>

using namespace Clusters;

// MARK: - DeviceWindowCovering

ClusterId windowCoveringClusters[] = { WindowCovering::Id };

const EmberAfDeviceType gWindowCoveringTypes[] = {
  { DEVICE_TYPE_MA_WINDOW_COVERING, DEVICE_VERSION_DEFAULT },
  { DEVICE_TYPE_MA_BRIDGED_DEVICE, DEVICE_VERSION_DEFAULT }
};

DeviceWindowCovering::DeviceWindowCovering(WindowCoveringDelegate& aWindowCoveringDelegate, IdentifyDelegate& aIdentifyDelegate, DeviceInfoDelegate& aDeviceInfoDelegate) :
  inherited(aIdentifyDelegate, aDeviceInfoDelegate),
  mWindowCoveringDelegate(aWindowCoveringDelegate)
{
  // - declare onoff device specific clusters
  useClusterTemplates(Span<ClusterId>(windowCoveringClusters));
}


string DeviceWindowCovering::description()
{
  string s = inherited::description();
  // maybe add attributes
  return s;
}


void DeviceWindowCovering::finalizeDeviceDeclaration()
{
  finalizeDeviceDeclarationWithTypes(Span<const EmberAfDeviceType>(gWindowCoveringTypes));
}


void DeviceWindowCovering::didGetInstalled()
{
  // install delegate
  WindowCovering::SetDefaultDelegate(endpointId(), this);
  // call base class last
  inherited::didGetInstalled();
}


// MARK: WindowCovering::Delegate (matter cluster's delegate, not our adapter!) implementation

CHIP_ERROR DeviceWindowCovering::HandleMovement(WindowCovering::WindowCoveringType type)
{
  OLOG(LOG_INFO, "WindowCoveringDelegate::HandleMovement: start moving");
  mWindowCoveringDelegate.startMovement(type);
  return CHIP_NO_ERROR;
}


CHIP_ERROR DeviceWindowCovering::HandleStopMotion()
{
  OLOG(LOG_INFO, "WindowCoveringDelegate::HandleStopMotion: stop moving");
  mWindowCoveringDelegate.stopMovement();
  return CHIP_NO_ERROR;
}
