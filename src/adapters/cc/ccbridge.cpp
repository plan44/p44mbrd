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
    gSharedCCBridgeP->mLabel = "mein Entwicklungs-Bastel-Gerät";
    gSharedCCBridgeP->mModel = "CentralControl CC41";
    gSharedCCBridgeP->mSerial = "9876543210";
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
  IsCommissionable = aIsCommissionable;

  JsonObjectPtr result = JsonObject::newObj();

  result->add("commissionable", JsonObject::newBool (IsCommissionable));

  if (IsCommissionable)
    {
      result->add ("qrcode", JsonObject::newString (QRCodeData));
      result->add ("pairingcode", JsonObject::newString (ManualPairingCode));
    }
  mJsonRpcAPI.sendRequest("matter_commissionable_status", result, NULL);
}


void CC_BridgeImpl::updateCommissioningInfo(const string aQRCodeData, const string aManualPairingCode)
{
  // TODO: maybe inform the gateway about commissioning information (pairing QRcode and manual code)
  QRCodeData = aQRCodeData;
  ManualPairingCode = aManualPairingCode;
}


void CC_BridgeImpl::identifyBridge(int aDurationS)
{
  // TODO: maybe inform the gateway to do some sort of self-identification (beep, blink...)
}


void CC_BridgeImpl::setBridgeRunning(bool aRunning)
{
  // TODO: maybe inform the gateway about bridge running status
  IsRunning = aRunning;
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
    // processing will continue at client_registered via callback
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

    /* subscribe to notifications */

    JsonObjectPtr params = JsonObject::newObj();
    params->add("pattern", JsonObject::newString("deviced.item_(config|state|vitals)_changed"));
    mJsonRpcAPI.sendRequest("rpc_client_subscribe", params, boost::bind(&CC_BridgeImpl::client_subscribed, this, _1, _2, _3));
    return;
  }
  OLOG(LOG_ERR, "error from rpc_client_subscribe: %s", aStatus->text());
  // startup failed, report back to main app
  startupComplete(aStatus);
}


void CC_BridgeImpl::client_subscribed(int32_t aResponseId, ErrorPtr &aStatus, JsonObjectPtr aResultOrErrorData)
{
  if (Error::isOK(aStatus)) {
    // request ok

    /* start discovery */

    JsonObjectPtr params = JsonObject::newObj();
    params->add("verbose", JsonObject::newBool (true));
    mJsonRpcAPI.sendRequest("deviced.deviced_get_items_info", params, boost::bind(&CC_BridgeImpl::deviceListReceived, this, _1, _2, _3));
    return;
  }
  OLOG(LOG_ERR, "error from deviced_get_group_names: %s", aStatus->text());
  // startup failed, report back to main app
  startupComplete(aStatus);
}

void CC_BridgeImpl::createDeviceForData(JsonObjectPtr item,
                                        bool          in_init)
{
  JsonObjectPtr item_id;
  DevicePtr dev = NULL;
  const char *device_type = NULL;
  bool feedback = 0;

  item_id = item->get ("id");

  OLOG(LOG_INFO, "item: %s", item->getCString ("name"));

  if (!item_id || item_id->int32Value() <= 0 ||
      strcmp (item->getCString ("type"), "group") != 0)
    return;

  /* ignore groups not backed with a backend (i.e. "real groups") */
  if (item->getCString ("backend") == NULL)
    return;

  device_type = item->getCString ("device_type");
  feedback = item->get("feedback") ? item->get("feedback")->boolValue() : false;

  if (strcmp (device_type, "switch") == 0)
    {
      OLOG (LOG_NOTICE, "... registering onoff device for switch");

      dev = new CC_OnOffPluginUnitDevice(item_id->int32Value());
    }
  else if (strcmp (device_type, "dimmer") == 0)
    {
      OLOG (LOG_NOTICE, "... registering dimmablelight device for dimmer");

      dev = new CC_DimmableLightDevice(item_id->int32Value());
    }
  else if (strcmp (device_type, "shutter") == 0)
    {
      OLOG (LOG_NOTICE, "... registering windowcovering device for shutter");

      dev = new CC_WindowCoveringDevice(item_id->int32Value(),
                                        WindowCovering::Type::kRollerShadeExterior,
                                        WindowCovering::EndProductType::kRollerShutter);
    }
  else if (strcmp (device_type, "awning") == 0)
    {
      OLOG (LOG_NOTICE, "... registering windowcovering device for awning");

      dev = new CC_WindowCoveringDevice(item_id->int32Value(),
                                        WindowCovering::Type::kAwning,
                                        WindowCovering::EndProductType::kAwningTerracePatio);
    }
  else if (strcmp (device_type, "screen") == 0)
    {
      OLOG (LOG_NOTICE, "... registering windowcovering device for screen");

      dev = new CC_WindowCoveringDevice(item_id->int32Value(),
                                        WindowCovering::Type::kRollerShade,
                                        WindowCovering::EndProductType::kAwningVerticalScreen);
    }
  else if (strcmp (device_type, "venetian") == 0)
    {
      OLOG (LOG_NOTICE, "... registering windowcovering device for venetian");

      dev = new CC_WindowCoveringDevice(item_id->int32Value(),
                                        WindowCovering::Type::kTiltBlindLiftAndTilt,
                                        WindowCovering::EndProductType::kExteriorVenetianBlind);
    }
  else
    {
      OLOG (LOG_NOTICE, "... device_type %s not supported yet", item->getCString ("device_type"));
    }

  if (dev)
    {
      CC_DeviceImpl::impl(dev)->initialize_name (item->getCString ("name"));
      CC_DeviceImpl::impl(dev)->initialize_feedback (feedback);
      CC_DeviceImpl::impl(dev)->handle_state_changed(item);

      // register it
      if (in_init)
        registerInitialDevice(DevicePtr (dev));
      else
        bridgeAdditionalDevice(DevicePtr (dev));
    }
}

void CC_BridgeImpl::deviceListReceived(int32_t aResponseId, ErrorPtr &aStatus, JsonObjectPtr aResultOrErrorData)
{
  JsonObjectPtr ilist;

  if (Error::isOK(aStatus)) {
    // request ok

    // we have a list of devices to be bridged here:

    ilist = aResultOrErrorData->get ("item_list");
    if (ilist && ilist->arrayLength() > 0) {
      int i;

      for (i = 0; i < ilist->arrayLength(); i++)
      {
        createDeviceForData (ilist->arrayGet (i), true);
      }
    }
  }
  else {
    OLOG(LOG_ERR, "error from deviced_get_items_info: %s", aStatus->text());
  }

  JsonObjectPtr params = JsonObject::newObj();
  params->add("persistent", JsonObject::newBool (false));
  params->add("shown", JsonObject::newBool (false));
  params->add("domain", JsonObject::newString ("p44mbrd"));
  params->add("code", JsonObject::newInt32 (0));
  params->add("message", JsonObject::newString ("p44mbrd startup done"));

  mJsonRpcAPI.sendRequest("systemd.log_entry_dump", params, boost::bind(&CC_BridgeImpl::ignoreLogResponse, this, _1, _2, _3));

  // Assume discovery done at this point, so report back to main app
  startupComplete(aStatus);
}


void CC_BridgeImpl::itemInfoReceived(int32_t aResponseId, ErrorPtr &aStatus, JsonObjectPtr aResultOrErrorData)
{
  if (Error::isOK(aStatus)) {
    // request ok

    // we have an item info object of the device to be bridged here:

    createDeviceForData (aResultOrErrorData, false);
  }
  else {
    OLOG(LOG_ERR, "error from item_get_info: %s", aStatus->text());
  }
}


void CC_BridgeImpl::ignoreLogResponse(int32_t aResponseId, ErrorPtr &aStatus, JsonObjectPtr aResultOrErrorData)
{
  if (Error::isOK(aStatus)) {
    // request ok
  }
  else {
    OLOG(LOG_ERR, "error from systemd.log_entry_dump: %s", aStatus->text());
  }
}


void CC_BridgeImpl::jsonRpcRequestHandler(const char *aMethod, const JsonObjectPtr aJsonRpcId, JsonObjectPtr aParams)
{
  // JSON RPC request/notification coming FROM bridge

  // TODO: analyze and possibly distribute to `Device` instance that can handle it

  if (!aJsonRpcId)
    {
      OLOG (LOG_NOTICE, "Notification %s received: %s", aMethod, JsonObject::text(aParams));
      if (strcmp ("deviced.item_config_changed", aMethod) == 0)
        {
          // find device
          JsonObjectPtr o;
          if (aParams->get("item_id", o))
          {
            int item_id = o->int32Value();
            DeviceUIDMap::iterator dev = mDeviceUIDMap.find(CC_DeviceImpl::uid_string(item_id));
            if (dev!=mDeviceUIDMap.end())
            {
              CC_DeviceImpl::impl(dev->second)->handle_config_changed(aParams);
            }
          }
        }
      else if (strcmp ("deviced.item_state_changed", aMethod) == 0)
        {
          // find device
          JsonObjectPtr o;
          if (aParams->get("item_id", o))
          {
            int item_id = o->int32Value();
            DeviceUIDMap::iterator dev = mDeviceUIDMap.find(CC_DeviceImpl::uid_string(item_id));
            if (dev!=mDeviceUIDMap.end())
            {
              CC_DeviceImpl::impl(dev->second)->handle_state_changed(aParams);
            }
          }
        }
      else if (strcmp ("deviced.item_vitals_changed", aMethod) == 0)
        {
          // determine what happened
          JsonObjectPtr o1, o2;
          if (aParams->get("vitals", o1) &&
              aParams->get("item_id", o2))
          {
            if (strcmp (o1->c_strValue(), "created") == 0)
            {
              int item_id = o2->int32Value();
              DeviceUIDMap::iterator dev = mDeviceUIDMap.find(CC_DeviceImpl::uid_string(item_id));
              /* we might already have it due to a deviced restart */
              if (dev!=mDeviceUIDMap.end())
                return;

              JsonObjectPtr params = JsonObject::newObj();
              params->add("item_id", JsonObject::newInt32 (o2->int32Value()));
              mJsonRpcAPI.sendRequest("deviced.item_get_info", params, boost::bind(&CC_BridgeImpl::itemInfoReceived, this, _1, _2, _3));
            }
            else if (strcmp (o1->c_strValue(), "deleted") == 0)
            {
              int item_id = o2->int32Value();
              DeviceUIDMap::iterator dev = mDeviceUIDMap.find(CC_DeviceImpl::uid_string(item_id));
              if (dev!=mDeviceUIDMap.end())
              {
                removeDevice (DevicePtr (dev->second));
              }
            }
          }
#if 0
          "vitals" kann sein: "created", "deleted", "children-change"

          {
            "jsonwatch":  "2.0",
            "request-src":  "deviced",
            "method":  "deviced.item_vitals_changed",
            "params":  {
              "item_id":  143,
              "vitals":  "created"
            }
          }
#endif
        }

      return;
    }
  else if (strcmp ("matter_set_commissionable", aMethod) == 0)
    {
      JsonObjectPtr o;

      if (aParams &&
          aParams->isType (json_type_object) &&
          aParams->get("commissionable", o) &&
          o->isType (json_type_boolean))
        {
          bool commissionable = o->boolValue ();
          requestCommissioning (commissionable);
          mJsonRpcAPI.sendResult(aJsonRpcId, JsonObject::objFromText ("{\"success\": 1}"));
        }
      else
        {
          mJsonRpcAPI.sendError(aJsonRpcId, JsonRpcError::InvalidParams, "mandatory boolean parameter \"commissionable\" wrong or missing.");
        }

      return;
    }
  else if (strcmp ("matter_get_commissionable", aMethod) == 0)
    {
      JsonObjectPtr result = JsonObject::newObj();

      result->add("commissionable", JsonObject::newBool (IsCommissionable));

      if (IsCommissionable)
        {
          result->add ("qrcode", JsonObject::newString (QRCodeData));
          result->add ("pairingcode", JsonObject::newString (ManualPairingCode));
        }
      mJsonRpcAPI.sendResult(aJsonRpcId, result);
      return;
    }
  else if (strcmp ("matter_reset_credentials", aMethod) == 0)
    {
      JsonObjectPtr o;

      if (aParams &&
          aParams->isType (json_type_object) &&
          aParams->get("i_mean_it", o) &&
          o->isType (json_type_boolean) &&
          o->boolValue ())
        {
          mJsonRpcAPI.sendResult(aJsonRpcId, JsonObject::objFromText ("{\"success\": 1}"));
          exit (5);
        }
      else
        {
          mJsonRpcAPI.sendError(aJsonRpcId, JsonRpcError::InvalidParams, "mandatory boolean parameter \"i_mean_it\" wrong or missing.");
        }

      return;
    }

  // For now, we just reject all request with error
  mJsonRpcAPI.sendError(aJsonRpcId, JsonRpcError::InvalidRequest, "TODO: implement methods");

}




#endif // CC_ADAPTERS
