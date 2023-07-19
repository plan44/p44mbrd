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

#if CC51_ADAPTERS

#include "jsonrpccomm.hpp"

// MARK: - CC51_BridgeImpl

/// @brief implements the bridge for the CC51 API
class CC51_BridgeImpl : public BridgeAdapter, public P44LoggingObj
{
  typedef BridgeAdapter inherited;

  JsonRpcComm mJsonRpcAPI;
  MLTicket mApiRetryTicket;

  /// private constructor because we must use the adapter() singleton getter/factory
  CC51_BridgeImpl();

  /// callback to report adapter startup
  AdapterStartedCB mAdapterStartedCB;

  /// identification of this bridge
  string mUID;
  string mLabel;
  string mModel;
  string mSerial;

public:

  /// singleton getter / on demand constructor for a CC51 adapter
  static CC51_BridgeImpl& adapter();

  /// @return the CC51 JSON RPC API for this adapter
  JsonRpcComm& api() { return mJsonRpcAPI; };

  /// @brief Set up connection parameters for the CC51 bridge API
  /// @param aApiHost the host name of the CC51 bridge API server
  /// @param aApiService the "service name" (at this time: port number only) of the CC51 bridge API server
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
  virtual string vendor() override { return "plan44.ch"; };

  /// @return serial number of this bridge (or the device it bridges)
  virtual string serial() override { return mSerial; }

  /// @brief start the CC51 bridge adapter implementation
  /// @param aAdapterStartedCB will be called when the adapter has started up and has discovered
  ///   and registered the devices that should get bridged to matter at stack startup.
  ///   The aError parameter of the callback should only return unrecoverable errors.
  virtual void adapterStartup(AdapterStartedCB aAdapterStartedCB) override;

  /// @brief update commissionable status
  /// @param aIsCommissionable true when matter side is commissionable (which may
  ///    cause adapter implementation to show or hide commissioning info its UI)
  virtual void setCommissionable(bool aIsCommissionable) override;

  /// @brief update commissioning info
  /// @param aQRCodeData string data that must go into QR Code presented to the user who wants to commission the bridge into a fabric
  /// @param aManualPairingCode string ma be shown to the user who cannot scan a QR code but must enter the commissioning info manually
  virtual void updateCommissioningInfo(const string aQRCodeData, const string aManualPairingCode) override;

  /// @brief update matter bridge running status
  /// @param aRunning true when matter bridge is running
  virtual void setBridgeRunning(bool aRunning) override;

  /// @brief cleanup (disconnect API, etc) the adapter
  virtual void cleanup() override;

  /// @}

private:

  void jsonRpcConnectionOpen();
  void jsonRpcConnectionStatusHandler(ErrorPtr aError);
  void jsonRpcRequestHandler(const char *aMethod, const char *aJsonRpcId, JsonObjectPtr aParams);

  void deviceListReceived(int32_t aResponseId, ErrorPtr &aError, JsonObjectPtr aResultOrErrorData);

};

#endif // CC51_ADAPTERS

