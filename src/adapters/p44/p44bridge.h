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

#if P44_ADAPTERS

#include "adapters/p44/p44bridgeapi.h"


// MARK: - P44_BridgeImpl

/// @brief implements the bridge for the P44 API
class P44_BridgeImpl : public BridgeAdapter, public P44LoggingObj
{
  typedef BridgeAdapter inherited;

  P44BridgeApi mBridgeApi;
  bool mConnectedOnce;

  /// private constructor because we must use the adapter() singleton getter/factory
  P44_BridgeImpl();

  /// identification of this bridge
  string mUID;
  string mLabel;
  string mModel;
  string mSerial;

public:

  /// singleton getter / on demand constructor for a P44 adapter
  static P44_BridgeImpl& adapter();

  virtual string logContextPrefix() override { return "P44 Adapter"; }

  /// @brief Set up connection parameters for the P44 bridge API
  /// @param aApiHost the host name of the P44 bridge API server
  /// @param aApiService the "service name" (at this time: port number only) of the P44 bridge API server
  void setAPIParams(const string aApiHost, const string aApiService);

  /// @return the P44 bridge API for this adapter
  P44BridgeApi& api() { return mBridgeApi; };

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

  /// @brief start the P44 bridge adapter implementation
  /// The adapter should query its API, discover devices to bridge to matter, instantiate them,
  /// and add them via registerInitialDevice() for publishing to matter when the stack has started up.
  /// @note must call startupComplete() later to signal adapter startup is complete.
  virtual void startup() override;

  /// @brief update commissionable status
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

  /// @brief cleanup (disconnect API, etc) the adapter
  virtual void cleanup() override;

  /// @}

  /// utility function to check model feature presence
  static bool hasModelFeature(JsonObjectPtr aDeviceInfo, const char* aModelFeature);


private:

  void bridgeApiConnectedHandler(ErrorPtr aStatus);
  void bridgeApiNotificationHandler(ErrorPtr aError, JsonObjectPtr aJsonMsg);
  void updateBridgeStatus(bool aStarted);
  void queryBridge();
  DevicePtr bridgedDeviceFromJSON(JsonObjectPtr aDeviceJSON);
  void bridgeApiCollectQueryHandler(ErrorPtr aError, JsonObjectPtr aJsonMsg);
  void reconnectBridgedDevices();
  void bridgeApiReconnectQueryHandler(ErrorPtr aError, JsonObjectPtr aJsonMsg);
  void handleGlobalNotification(const string notification, JsonObjectPtr aJsonMsg);
  void newDeviceGotBridgeable(string aNewDeviceDSUID);
  void newDeviceInfoQueryHandler(ErrorPtr aError, JsonObjectPtr aJsonMsg);

};

#endif // P44_ADAPTERS

