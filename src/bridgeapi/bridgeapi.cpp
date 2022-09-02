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

#include "bridgeapi.h"

using namespace p44;


BridgeApi* gSharedBridgeApiP = nullptr;

BridgeApi& BridgeApi::api()
{
  if (gSharedBridgeApiP==nullptr) {
    gSharedBridgeApiP = new BridgeApi;
    assert(gSharedBridgeApiP);
    gSharedBridgeApiP->isMemberVariable();
  }
  return *gSharedBridgeApiP;
}


BridgeApi::BridgeApi() :
  mBridgeCallCounter(0)
{
}
  
void BridgeApi::connectBridgeApi(StatusCB aConnectedCB)
{
  mConnectedCB = aConnectedCB;
  tryConnection();
}


void BridgeApi::tryConnection()
{
  setConnectionStatusHandler(boost::bind(&BridgeApi::connectionStatusHandler, this, _2));
  setMessageHandler(boost::bind(&BridgeApi::messageHandler, this, _1, _2));
  initiateConnection();
}

void BridgeApi::connectionStatusHandler(ErrorPtr aStatus)
{
  if (Error::notOK(aStatus)) {
    LOG(LOG_WARNING, "Could not reach bridge API: %s -> trying again in 5 seconds", aStatus->text());
    mApiRetryTicket.executeOnce(boost::bind(&BridgeApi::tryConnection, this), 5*Second);
    return;
  }
  else {
    // connection ok
    if (mConnectedCB) {
      StatusCB cb = mConnectedCB;
      mConnectedCB = NoOP;
      cb(aStatus);
    }
    return;
  }
}

void BridgeApi::messageHandler(ErrorPtr aError, JsonObjectPtr aJsonObject)
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


void BridgeApi::call(const string aMethod, JsonObjectPtr aParams, JSonMessageCB aResponseCB)
{
  if (!aParams) aParams = JsonObject::newObj();
  aParams->add("method", JsonObject::newString(aMethod));
  PendingBridgeCall call;
  call.mCallId = string_format("%ld", ++mBridgeCallCounter);
  call.mCallback = aResponseCB;
  aParams->add("id", JsonObject::newString(call.mCallId));
  ErrorPtr err = sendMessage(aParams);
  if (Error::isOK(err)) {
    mPendingBridgeCalls.push_back(call);
  }
  else {
    LOG(LOG_ERR, "bridge API: sending method '%s' failed: %s", aMethod.c_str(), err->text());
    if (aResponseCB) aResponseCB(err, JsonObjectPtr());
  }
}


ErrorPtr BridgeApi::notify(const string aNotification, JsonObjectPtr aParams)
{
  if (!aParams) aParams = JsonObject::newObj();
  aParams->add("notification", JsonObject::newString(aNotification));
  ErrorPtr err = sendMessage(aParams);
  if (Error::notOK(err)) {
    LOG(LOG_ERR, "bridge API: sending notification '%s' failed: %s", aNotification.c_str(), err->text());
  }
  return err;
}