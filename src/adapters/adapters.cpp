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

void BridgeAdapter::startup(AdapterStartedCB aAdapterStartedCB, AddDeviceCB aAddDeviceCB)
{
  mAddDeviceCB = aAddDeviceCB; // needed for implementation of bridgeAdditionalDevice().
  adapterStartup(aAdapterStartedCB);
}


bool BridgeAdapter::hasBridgeableDevices()
{
  return !mDeviceUIDMap.empty();
}


void BridgeAdapter::cleanup()
{
  mAddDeviceCB = NoOP;
}


void BridgeAdapter::registerInitialDevice(DevicePtr aDevice)
{
  mDeviceUIDMap[aDevice->deviceInfoDelegate().endpointUID()] = aDevice;
}


void BridgeAdapter::bridgeAdditionalDevice(DevicePtr aDevice)
{
  // add to map
  mDeviceUIDMap[aDevice->deviceInfoDelegate().endpointUID()] = aDevice;
  assert(mAddDeviceCB);
  mAddDeviceCB(aDevice);
}
