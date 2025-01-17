//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
//  based on Apache v2 licensed bridge-app example code (c) 2021 Project CHIP Authors
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

#include "adapters.h"

// MARK: - BridgeAdapter

void BridgeAdapter::startup(BridgeMainDelegate& aBridgeMainDelegate)
{
  mBridgeMainDelegateP = &aBridgeMainDelegate;
  startup();
}


void BridgeAdapter::startupComplete(ErrorPtr aError)
{
  mBridgeMainDelegateP->adapterStartupComplete(aError, *this);
}


void BridgeAdapter::installInitialDevices(CHIP_ERROR& aChipErr)
{
  aChipErr = CHIP_NO_ERROR;
  for (BridgeAdapter::DeviceUIDMap::iterator pos = mDeviceUIDMap.begin(); pos!=mDeviceUIDMap.end(); ++pos) {
    DevicePtr dev = pos->second;
    // install the device
    ChipError err = mBridgeMainDelegateP->installDevice(dev, *this);
    if (err != CHIP_NO_ERROR) aChipErr = err;
  }
}


bool BridgeAdapter::hasBridgeableDevices()
{
  return !mDeviceUIDMap.empty();
}


void BridgeAdapter::cleanup()
{
}


void BridgeAdapter::registerInitialDevice(DevicePtr aDevice)
{
  mDeviceUIDMap[aDevice->deviceInfoDelegate().endpointUID()] = aDevice;
}


void BridgeAdapter::bridgeAdditionalDevice(DevicePtr aDevice)
{
  // check if we had that device before and it was only disabled
  DeviceUIDMap::iterator pos = mDeviceUIDMap.find(aDevice->deviceInfoDelegate().endpointUID());
  if (pos!=mDeviceUIDMap.end()) {
    DevicePtr dev = pos->second;
    EndpointId previousEndpoint = dev->endpointId();
    if (previousEndpoint!=kInvalidEndpointId) {
      if (emberAfEndpointIsEnabled(previousEndpoint)) {
        // already enabled - should not happen
        POLOG(dev, LOG_ERR, "is already bridged and operational, cannot be added again!")
        return;
      }
      else {
        // exists, but is disabled - re-enable
        // - use newer definition of the device
        mDeviceUIDMap[aDevice->deviceInfoDelegate().endpointUID()] = aDevice;
        mBridgeMainDelegateP->reEnableDevice(aDevice, *this);
        return;
      }
    }
  }
  // is new, or previous device with same endpointUID had no endpoint assigned yet -> add it
  mDeviceUIDMap[aDevice->deviceInfoDelegate().endpointUID()] = aDevice;
  ErrorPtr err = mBridgeMainDelegateP->addAdditionalDevice(aDevice, *this);
  if (Error::notOK(err)) {
    POLOG(aDevice, LOG_ERR, "cannot add device: %s", err->text());
  }
}


void BridgeAdapter::removeDevice(DevicePtr aDevice)
{
  return mBridgeMainDelegateP->disableDevice(aDevice, *this);
}


ErrorPtr BridgeAdapter::requestCommissioning(bool aCommissionable)
{
  return mBridgeMainDelegateP->makeCommissionable(aCommissionable, *this);
}


void BridgeAdapter::addOrReplaceAction(ActionPtr aAction, UpdateMode aUpdateMode)
{
  return mBridgeMainDelegateP->addOrReplaceAction(aAction, aUpdateMode, *this);
}


void BridgeAdapter::addOrReplaceEndpointsList(EndpointListInfoPtr aEndPointList, UpdateMode aUpdateMode)
{
  return mBridgeMainDelegateP->addOrReplaceEndpointsList(aEndPointList, aUpdateMode, *this);
}
