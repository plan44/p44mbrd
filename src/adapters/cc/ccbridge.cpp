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

#include "ccbridge.h"

#if CC_ADAPTERS

#include "ccdevices.h"

using namespace p44;


// MARK: - CC_BridgeImpl

CC_BridgeImpl* gSharedCCBridgeP = nullptr;

CC_BridgeImpl& CC_BridgeImpl::adapter()
{
  if (gSharedCCBridgeP==nullptr) {
    gSharedCCBridgeP = new CC_BridgeImpl;
    assert(gSharedCCBridgeP);
    gSharedCCBridgeP->isMemberVariable();
  }
  return *gSharedCCBridgeP;
}

// MARK: BridgeAdapter API implementation

void CC_BridgeImpl::startup()
{
  // start the socket connection
  // - install connection status callback
  mJsonRpcAPI.setConnectionStatusHandler(boost::bind(&CC_BridgeImpl::jsonRpcConnectionStatusHandler, this, _2));
  // - initiate the connection
  jsonRpcConnectionOpen();
}


void CC_BridgeImpl::reportCommissionable(bool aIsCommissionable)
{
  // TODO: maybe inform the gateway about commissionable status
}


void CC_BridgeImpl::updateCommissioningInfo(const string aQRCodeData, const string aManualPairingCode)
{
  // TODO: maybe inform the gateway about commissioning information (pairing QRcode and manual code)
}


void CC_BridgeImpl::setBridgeRunning(bool aRunning)
{
  // TODO: maybe inform the gateway about bridge running status
}


void CC_BridgeImpl::cleanup()
{
  // TODO: maybe other cleanup required before or after closing connection
  mJsonRpcAPI.closeConnection();
  inherited::cleanup();
}


// MARK: CC_BridgeImpl internals

CC_BridgeImpl::CC_BridgeImpl()
{
  // Note: isMemberVariable() MUST be called on P44Obj based objects that are instantiated
  //   as C++ member variables (instead of allocated via new and managed by refcount),
  //   preferably in the ctor of the containing object (= here).
  mJsonRpcAPI.isMemberVariable();
}


void CC_BridgeImpl::setAPIParams(const string aApiHost, const string aApiService)
{
  // End-of-Message is 0 in the CC JsonRPC socket stream
  mJsonRpcAPI.setEndOfMessageChar('\x00');
  // set up connection parameters
  mJsonRpcAPI.setConnectionParams(aApiHost.c_str(), aApiService.c_str(), SOCK_STREAM);
  // install method/notification request handler
  mJsonRpcAPI.setRequestHandler(boost::bind(&CC_BridgeImpl::jsonRpcRequestHandler, this, _1, _2, _3));
}


void CC_BridgeImpl::jsonRpcConnectionOpen()
{
  // initiate API connection, will call back jsonRpcConnectionStatusHandler()
  mJsonRpcAPI.initiateConnection();
}


void CC_BridgeImpl::jsonRpcConnectionStatusHandler(ErrorPtr aStatus)
{
  if (Error::isOK(aStatus)) {
    // connection established ok
    mApiRetryTicket.cancel();

    // initiate stuff, in particular:
    // - query the API to discover devices that need to be bridged
    // - instantiate device objects derived from matter-device type specific leaf classes
    //   of the `Device` hierarchy and pass them references to the required "delegate"
    //   implementations.
    // - if the API side device needs to be represented as multiple matter side devices:
    //   - instantiate a generic ComposedDevice
    //   - use `addSubDevice()` to add the components of the device
    // - call `registerInitialDevice()` for the (possibly composed) device

    // TODO: implement

    // Query the API for device discovery

    // probably something like
    // {"jsonrpc":"2.0","id":"26", "method":"deviced_get_group_names","params":{"room_id":1}}
    JsonObjectPtr params = JsonObject::newObj();
    params->add("name", JsonObject::newString("p44mbrd"));
    mJsonRpcAPI.sendRequest("rpc_client_register", params, boost::bind(&CC_BridgeImpl::client_registered, this, _1, _2, _3));
    // processing will continue at deviceListReceived via callback
    return;
  }
  else {
    // connection error
    OLOG(LOG_WARNING, "JSON RPC API connection failed: %s", aStatus->text());
    // TODO: better recovery
    // for now: just retry connecting in 10 seconds
    mApiRetryTicket.executeOnce(boost::bind(&CC_BridgeImpl::jsonRpcConnectionOpen, this), 10*Second);
  }
}


void CC_BridgeImpl::client_registered(int32_t aResponseId, ErrorPtr &aStatus, JsonObjectPtr aResultOrErrorData)
{
  if (Error::isOK(aStatus)) {
    // request ok

    /* start discovery */

    JsonObjectPtr params = JsonObject::newObj();
    mJsonRpcAPI.sendRequest("deviced.deviced_get_items_info", params, boost::bind(&CC_BridgeImpl::deviceListReceived, this, _1, _2, _3));
    return;
  }
  OLOG(LOG_ERR, "error from deviced_get_group_names: %s", aStatus->text());
  // startup failed, report back to main app
  startupComplete(aStatus);
}

void CC_BridgeImpl::deviceListReceived(int32_t aResponseId, ErrorPtr &aStatus, JsonObjectPtr aResultOrErrorData)
{
  JsonObjectPtr ilist;

  if (Error::isOK(aStatus)) {
    // request ok

    // assuming we have a list of devices to be bridged here:
    // TODO: implement device instantiation

    ilist = aResultOrErrorData->get ("item_list");
    if (ilist && ilist->arrayLength() > 0) {
      int i;

      for (i = 0; i < ilist->arrayLength(); i++)
        {
          JsonObjectPtr item, item_id;

          item = ilist->arrayGet (i);
          item_id = item->get ("id");

          OLOG(LOG_INFO, "item: %s", item->getCString ("name"));

          if (item_id && item_id->int32Value() > 0 &&
              !strcmp (item->getCString ("type"), "group") &&
              !strcmp (item->getCString ("device_type"), "switch"))
            {
              OLOG (LOG_NOTICE, "... registering onoff device for switch");

              DevicePtr dev = new CC_OnOffPluginUnitDevice(item_id->int32Value());
              // register it
              registerInitialDevice(dev);
            }
        }
    }

  }
  else {
    OLOG(LOG_ERR, "error from deviced_get_group_names: %s", aStatus->text());
  }
  // Assume discovery done at this point, so erport back to main app
  startupComplete(aStatus);
}


void CC_BridgeImpl::jsonRpcRequestHandler(const char *aMethod, const char *aJsonRpcId, JsonObjectPtr aParams)
{
  // JSON RPC request coming FROM bridge

  // TODO: analyze and possibly distribute to `Device` instance that can handle it

  // For now, we just reject all request with error
  mJsonRpcAPI.sendError(aJsonRpcId, JsonRpcError::InvalidRequest, "TODO: implement methods");

}


#endif // CC_ADAPTERS
