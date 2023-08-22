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

#include "p44bridgeapi.h"

#if P44_ADAPTERS

using namespace p44;

P44BridgeApi::P44BridgeApi() :
  mBridgeCallCounter(0)
{
}
  
void P44BridgeApi::connectBridgeApi(StatusCB aConnectedCB)
{
  mConnectedCB = aConnectedCB;
  tryConnection();
}


void P44BridgeApi::tryConnection()
{
  setConnectionStatusHandler(boost::bind(&P44BridgeApi::connectionStatusHandler, this, _2));
  setMessageHandler(boost::bind(&P44BridgeApi::messageHandler, this, _1, _2));
  initiateConnection();
}

void P44BridgeApi::connectionStatusHandler(ErrorPtr aStatus)
{
  if (Error::notOK(aStatus)) {
    LOG(LOG_WARNING, "Could not reach bridge API: %s -> trying again in 5 seconds", aStatus->text());
    mApiRetryTicket.executeOnce(boost::bind(&P44BridgeApi::tryConnection, this), 5*Second);
    return;
  }
  else {
    // connection ok
    if (mConnectedCB) {
      StatusCB cb = mConnectedCB;
      cb(aStatus);
    }
    return;
  }
}

void P44BridgeApi::messageHandler(ErrorPtr aError, JsonObjectPtr aJsonObject)
{
  if (Error::isOK(aError)) {
    //LOG(LOG_DEBUG, "msg = %s", aJsonObject->json_c_str());
    JsonObjectPtr o;
    if (aJsonObject && aJsonObject->get("id", o)) {
      // this IS a method answer
      string callid = o->stringValue();
      JSonMessageCB cb;
      for (PendingBridgeCalls::iterator pos = mPendingBridgeCalls.begin(); pos!=mPendingBridgeCalls.end(); ++pos) {
        if (pos->mCallId==callid) {
          // answer matching pending call
          cb = pos->mCallback;
          mPendingBridgeCalls.erase(pos);
          break;
        }
      }
      if (cb) cb(ErrorPtr(), aJsonObject);
    }
    else {
      // must be notification
      if (mNotificationCB) mNotificationCB(ErrorPtr(), aJsonObject);
    }
  }
  else {
    LOG(LOG_ERR, "Bridge API data error: %s", aError->text());
    if (mNotificationCB) mNotificationCB(aError, JsonObjectPtr());
  }
}


void P44BridgeApi::call(const string aMethod, JsonObjectPtr aParams, JSonMessageCB aResponseCB)
{
  if (!aParams) aParams = JsonObject::newObj();
  aParams->add("method", JsonObject::newString(aMethod));
  PendingBridgeCall call;
  call.mCallId = string_format("%ld", ++mBridgeCallCounter);
  call.mCallback = aResponseCB;
  aParams->add("id", JsonObject::newString(call.mCallId));
  LOG(LOG_DEBUG, "Calling method '%s' in bridge, params:\n%s", aMethod.c_str(), JsonObject::text(aParams));
  ErrorPtr err = sendMessage(aParams);
  if (Error::isOK(err)) {
    mPendingBridgeCalls.push_back(call);
  }
  else {
    LOG(LOG_ERR, "bridge API: sending method '%s' failed: %s", aMethod.c_str(), err->text());
    if (aResponseCB) aResponseCB(err, JsonObjectPtr());
  }
}


void P44BridgeApi::setProperties(const string aDSUID, JsonObjectPtr aProperties)
{
  JsonObjectPtr params = JsonObject::newObj();
  params->add("dSUID", JsonObject::newString(aDSUID));
  params->add("properties", aProperties);
  call("setProperty", params, NoOP);
}


void P44BridgeApi::setProperty(const string aDSUID, const string aPropertyPath, JsonObjectPtr aValue)
{
  string path = aPropertyPath;
  do {
    size_t p = path.rfind(".");
    size_t n = p==string::npos ? 0 : p+1;
    JsonObjectPtr prop = JsonObject::newObj();
    prop->add(path.substr(n).c_str(), aValue);
    aValue = prop;
    if (p==string::npos) break;
    path.erase(p);
  } while(true);
  setProperties(aDSUID, aValue);
}


ErrorPtr P44BridgeApi::notify(const string aNotification, JsonObjectPtr aParams)
{
  if (!aParams) aParams = JsonObject::newObj();
  aParams->add("notification", JsonObject::newString(aNotification));
  LOG(LOG_DEBUG, "Sending notification '%s' to bridge, params:\n%s", aNotification.c_str(), JsonObject::text(aParams));
  ErrorPtr err = sendMessage(aParams);
  if (Error::notOK(err)) {
    LOG(LOG_ERR, "bridge API: sending notification '%s' failed: %s", aNotification.c_str(), err->text());
  }
  return err;
}

#endif // P44_ADAPTERS
