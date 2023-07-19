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

#include "cc51devices.h"

#if CC51_ADAPTERS

#include "cc51bridge.h"

using namespace p44;


// MARK: - CC51_DeviceImpl

CC51_DeviceImpl::CC51_DeviceImpl()
{
}


// MARK: DeviceInfoDelegate implementation in CC51_DeviceImpl base class


const string CC51_DeviceImpl::endpointUID() const
{
  // TODO: return a sensible value
  return "THIS_MUST_BE_A_UUID";
}


string CC51_DeviceImpl::vendorName() const
{
  // TODO: return a sensible value
  return "Becker Antriebe GmbH";
}


string CC51_DeviceImpl::modelName() const
{
  // TODO: return a sensible value
  return "Fehlender Modellname";
}


string CC51_DeviceImpl::configUrl() const
{
  // TODO: maybe return a web-UI URL
  return "";
}


string CC51_DeviceImpl::serialNo() const
{
  // TODO: return a sensible value
  return "THIS_MUST_BE_A_SERIAL_NO";
}


bool CC51_DeviceImpl::isReachable() const
{
  // TODO: return actual reachability status
  return true;
}


string CC51_DeviceImpl::name() const
{
  return mName;
}


bool CC51_DeviceImpl::changeName(const string aNewName)
{
  if (aNewName!=mName) {
    // TODO: forward new name

    // probably something like
    // {"jsonrpc":"2.0","id":"26", "method":"deviced_get_group_names","params":{"room_id":1}}
    JsonObjectPtr params = JsonObject::newObj();
    params->add("item_id", JsonObject::newInt32(42));
    params->add("name", JsonObject::newString(aNewName));
    CC51_BridgeImpl::adapter().api().sendRequest("item_set_name", params);

  }
  return true; // new name propagated
}


string CC51_DeviceImpl::zone() const
{
  return mZone;
}


// MARK: - CC51_IdentifiableImpl

// MARK: IdentityDelegate implementation

void CC51_IdentifiableImpl::identify(int aDurationS)
{
  // (re)start or stop identify in the bridged device
  // <0 = stop, >0 = duration (duration==0 would mean default duration, not used here)

  // TODO: make API calls to the device to draw user's attention to it...

  // probably something like
  JsonObjectPtr params = JsonObject::newObj();
  params->add("someting", JsonObject::newInt32(3));
  CC51_BridgeImpl::adapter().api().sendRequest("some_method", params);
}


// MARK: - CC51_OnOffImpl

// MARK: OnOffDelegate implementation

void CC51_OnOffImpl::setOnOffState(bool aOn)
{

  // TODO: make API call to set the output state

  // probably something like
  JsonObjectPtr params = JsonObject::newObj();
  params->add("someting", JsonObject::newInt32(3));
  CC51_BridgeImpl::adapter().api().sendRequest("some_method", params);

}

#endif // P44_ADAPTERS
