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

#pragma once

#include "adapters/adapters.h"

#if CC_ADAPTERS

#include "jsonrpccomm.hpp"

// MARK: - CC_BridgeImpl

/// @brief implements the bridge for the CC API
class CC_BridgeImpl : public BridgeAdapter, public P44LoggingObj
{
  typedef BridgeAdapter inherited;

  JsonRpcComm mJsonRpcAPI;
  MLTicket mApiRetryTicket;

  /// private constructor because we must use the adapter() singleton getter/factory
  CC_BridgeImpl();

  /// identification of this bridge
  string mUID;
  string mLabel;
  string mModel;
  string mSerial;

  bool   IsRunning;
  bool   IsCommissionable;
  string QRCodeData;
  string ManualPairingCode;

public:

  /// singleton getter / on demand constructor for a CC adapter
  static CC_BridgeImpl& adapter();

  /// @return a prefix string identifying the device for log messages issued via the OLOG macro
  virtual string logContextPrefix() override { return "CC Adapter"; }

  /// @return the CC JSON RPC API for this adapter
  JsonRpcComm& api() { return mJsonRpcAPI; };

  /// @brief Set up connection parameters for the CC bridge API
  /// @param aApiHost the host name of the CC bridge API server
  /// @param aApiService the "service name" (at this time: port number only) of the CC bridge API server
  void setAPIParams(const string aApiHost, const string aApiService);

  /// @name BridgeAdapter implementation
  /// @{

  /// @return UID of this adapter (or the device it bridges)
  virtual string UID() override { return mUID; };

  /// @return user-specified name of this bridge (or the device it bridges)
  virtual string label() override { return mLabel; }

  /// @return model name/number of this bridge (or the device it bridges)
  virtual string model() override { return mModel; }

  /// @return vendor name for this bridge (or the device it bridges)
  virtual string vendor() override { return "Becker-Antriebe GmbH"; };

  /// @return serial number of this bridge (or the device it bridges)
  virtual string serial() override { return mSerial; }

  /// @brief start the CC bridge adapter implementation
  /// The adapter should query its API, discover devices to bridge to matter, instantiate them,
  /// and add them via registerInitialDevice() for publishing to matter when the stack has started up.
  /// @note must call startupComplete() later to signal adapter startup is complete.
  virtual void startup() override;

  /// @brief reports commissionable status
  /// @param aIsCommissionable true when matter side is commissionable (which may
  ///    cause adapter implementation to show or hide commissioning info its UI)
  virtual void reportCommissionable(bool aIsCommissionable) override;

  /// @brief update commissioning info
  /// @param aQRCodeData string data that must go into QR Code presented to the user who wants to commission the bridge into a fabric
  /// @param aManualPairingCode string ma be shown to the user who cannot scan a QR code but must enter the commissioning info manually
  virtual void updateCommissioningInfo(const string aQRCodeData, const string aManualPairingCode) override;

  /// @brief update matter bridge running status
  /// @param aRunning true when matter bridge is running
  virtual void setBridgeRunning(bool aRunning) override;

  /// @brief is called when all initial devices are installed
  ///   (and thus have a valid endpointID and can access attributes)
  virtual void initialDevicesInstalled() override {};

  /// @brief is called when bridge should identify itself. This is the case whenever a
  ///   device is not able to identify itself individually.
  /// @param aDurationS >0: number of seconds the identification action
  ///   should perform, such as blinking or beeping.
  ///   0: use default duration of hardware device
  ///   <0: stop ongoing identification
  virtual void identifyBridge(int aDurationS) override;

  /// @brief cleanup (disconnect API, etc) the adapter
  virtual void cleanup() override;

  /// @}

private:

  void createDeviceForData(JsonObjectPtr item, bool in_init);

  void jsonRpcConnectionOpen();
  void jsonRpcConnectionStatusHandler(ErrorPtr aError);
  void jsonRpcRequestHandler(const char *aMethod, const JsonObjectPtr aJsonRpcId, JsonObjectPtr aParams);

  void client_subscribed(int32_t aResponseId, ErrorPtr &aError, JsonObjectPtr aResultOrErrorData);
  void client_registered(int32_t aResponseId, ErrorPtr &aError, JsonObjectPtr aResultOrErrorData);
  void deviceListReceived(int32_t aResponseId, ErrorPtr &aError, JsonObjectPtr aResultOrErrorData);
  void ignoreLogResponse(int32_t aResponseId, ErrorPtr &aError, JsonObjectPtr aResultOrErrorData);
  void itemInfoReceived(int32_t aResponseId, ErrorPtr &aError, JsonObjectPtr aResultOrErrorData);

};

#endif // CC_ADAPTERS

