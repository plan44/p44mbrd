//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2022 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#pragma once

#include "p44mbrd_common.h"

#if P44_ADAPTERS

#include "jsoncomm.hpp"
#include "adapters/p44/p44bridgeapi_defs.h"

using namespace p44;

class P44BridgeApi : public JsonComm
{
  MLTicket mApiRetryTicket;
  long mBridgeCallCounter;
  typedef struct {
    string mCallId;
    JSonMessageCB mCallback;
  } PendingBridgeCall;
  typedef std::list<PendingBridgeCall> PendingBridgeCalls;
  PendingBridgeCalls mPendingBridgeCalls;
  StatusCB mConnectedCB;
  JSonMessageCB mNotificationCB;


public:

  P44BridgeApi();

  /// connect to the bridge API
  /// @param aConnectedCB will be called when connection is established or error occurs
  void connectBridgeApi(StatusCB aConnectedCB);

  /// set a handler to be called when a notification arrives via bridge API
  void setNotificationHandler(JSonMessageCB aNotificationCB) { mNotificationCB = aNotificationCB; };

  /// call method via bridge API
  /// @param aMethod the method name
  /// @param aParams method parameters
  /// @param aResponseCB will be called with method response or transport/encoding level error
  void call(const string aMethod, JsonObjectPtr aParams, JSonMessageCB aResponseCB);

  /// convenience method to set properties
  /// @param aDSUID the dsuid
  /// @param aProperties the property value to set, or if aPropName is empty, the object containing all properties to set.
  void setProperties(const string aDSUID, JsonObjectPtr aProperties);

  /// convenience method to set single property
  /// @param aDSUID the dsuid
  /// @param aPropertyPath the property path (dot separated)
  /// @param aValue the property value to set, or if aPropName is empty, the object containing all properties to set.
  void setProperty(const string aDSUID, const string aPropertyPath, JsonObjectPtr aValue);

  /// send notification via bridge API
  /// @param aNotification the notification name
  /// @param aParams method parameters
  /// @return ok or error when sending fails
  ErrorPtr notify(const string aNotification, JsonObjectPtr aParams);

private:

  void tryConnection();
  void connectionStatusHandler(ErrorPtr aStatus);
  void messageHandler(ErrorPtr aError, JsonObjectPtr aJsonObject);

};

#endif // P44_ADAPTERS
