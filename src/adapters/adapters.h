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

#include "p44mbrd_common.h"

#include "device.h"
#include "deviceinfoprovider.h"

// commonly needed matter headers
#include <app-common/zap-generated/attributes/Accessors.h>

// Macros for constructing final `Device` leaf subclasses containing needed delegates

#define DEVICE_ACCESSOR virtual Device &device() override { return static_cast<Device&>(*this); }
#define DG(DelegateBasename) static_cast<DelegateBasename##Delegate&>(*this)


class BridgeAdapter;

class BridgeMainDelegate
{

protected:

  virtual ~BridgeMainDelegate() = default;

public:

  /// must be called after BridgeAdapter::startup(), when adapter has started up and
  virtual void adapterStartupComplete(ErrorPtr aError, BridgeAdapter &aAdapter) = 0;

  /// can be be called by the bridge implementation at any time after
  /// adapter startup up to when cleanup() is invoked to add further devices while the matter
  /// bridge is already operational.
  /// @param aDevice device to add
  /// @return ok or error when device could not be added
  virtual ErrorPtr addAdditionalDevice(DevicePtr aDevice, BridgeAdapter& aAdapter) = 0;

  /// can be called to make the bridge device open or close a commissioning window
  /// @param aCommissionable requested commissionable status
  /// @return ok or error if requested commissionable status cannot be established
  virtual ErrorPtr makeCommissionable(bool aCommissionable, BridgeAdapter& aAdapter) = 0;

  /// can be called to install
  virtual ChipError installDevice(DevicePtr aDevice, BridgeAdapter& aAdapter) = 0;

};



/// @brief base class for bridge API adapters
/// Contains utilities needed to access the matter side from bridge API implementations
class BridgeAdapter
{
  // friend class P44mbrd;

protected:

  virtual ~BridgeAdapter() = default;

  /// maps device UIDs to actual devices bridged via this adapter
  typedef std::map<string, DevicePtr> DeviceUIDMap;
  DeviceUIDMap mDeviceUIDMap;

  /// callback for starting an adapter
  typedef boost::function<void (ErrorPtr aError, BridgeAdapter& aBridgeAdapter)> AdapterStartedCB;

  /// @brief startup the bridge adapter
  /// The bridge adapter should query its API, discover devices to bridge to matter, instantiate them,
  /// and add them via registerInitialDevice() for publishing to matter when the stack is started up.
  /// @note must call startupComplete() later to signal startup is complete.
  virtual void startup() = 0;

  /// must be called after startup() when the adapter has started up and has discovered
  ///   and registered the devices that should get bridged to matter at stack startup.
  /// @param aError OK when startup has not fatally failed. Recoverable errors should not
  ///   be reported here.
  void startupComplete(ErrorPtr aError);

private:

  /// delegate for calling main-level functionality from adapters
  BridgeMainDelegate* mBridgeMainDelegateP = nullptr;

public:

  /// entry point for main program to start this adapter
  /// @param aBridgeMainDelegate the delegate for the adapter to request global functionality
  void startup(BridgeMainDelegate& aBridgeMainDelegate);

  /// will be called to have adapter install the devices collected during startup()..startupComplete().
  void installInitialDevices(CHIP_ERROR& aChipErr);

  /// @return UID of this adapter (or the device it bridges)
  virtual string UID() = 0;

  /// @return user-specified name of this bridge (or the device it bridges)
  virtual string label() = 0;

  /// @return model name/number of this bridge (or the device it bridges)
  virtual string model() = 0;

  /// @return vendor name for this bridge (or the device it bridges)
  virtual string vendor() = 0;

  /// @return serial number of this bridge (or the device it bridges)
  virtual string serial() = 0;

  /// @return true if the adapter as at least on bridgeable device
  bool hasBridgeableDevices();

  /// @brief update commissioning info
  /// @note the adapter should be ready to receive and store this data independently of the
  ///    current commissioning status.
  /// @param aQRCodeData string data that must go into QR Code presented to the user who
  ///   wants to commission the bridge into a fabric
  /// @param aManualPairingCode string ma be shown to the user who cannot scan a
  ///   QR code but must enter the commissioning info manually.
  ///   Empty string if there is no manual pairing code available
  virtual void updateCommissioningInfo(const string aQRCodeData, const string aManualPairingCode) = 0;

  /// @brief reports commissionable status
  /// @param aIsCommissionable true when matter side is commissionable (which may
  ///    cause adapter implementation to show or hide QR code and/or setup code in its UI)
  virtual void reportCommissionable(bool aIsCommissionable) = 0;

  /// @brief can be called to request opening or closing the commissioning window
  /// @param aCommissionable requested commissionable status
  /// @note reportCommissionable() will be called to report when commissioning window status
  ///   actually has changed.
  /// @return Ok or error when requested commission status cannot be established
  ErrorPtr requestCommissioning(bool aCommissionable);

  /// @brief update matter bridge running status
  /// @param aRunning true when matter bridge is running
  virtual void setBridgeRunning(bool aRunning) = 0;

  /// @brief cleanup (disconnect API, etc) the adapter
  virtual void cleanup();

  /// @brief register a device to appear initially (at start of the matter stack) as a bridged device
  /// @param aDevice the device to register.
  /// @note this must be called as part of the adapter startup procedure, and may NOT be called
  ///    when the bridge is already operational. Use bridgeAdditionalDevice() for that.
  /// @note register only bridge-level devices, not subdevices of a composed device!
  ///   Subdevices must be added to the composed device via addSubdevice() BEFORE the
  ///   composed device is registered, and will be published to the matter side automatically.
  void registerInitialDevice(DevicePtr aDevice);

  /// @brief add an additional device to the matter bridge while it is already operational
  /// @param aDevice the device to register.
  /// @note register only bridge-level devices, not subdevices of a composed device!
  ///   Subdevices must be added to the composed device via addSubdevice() BEFORE the
  ///   composed device is registered, and will be published to the matter side automatically.
  void bridgeAdditionalDevice(DevicePtr aDevice);

};



/// @brief common class for device adapters
/// Contains utilities needed to access the matter side device from delegates
class DeviceAdapter
{
public:
  using UpdateMode = Device::UpdateMode;
  using UpdateFlags = Device::UpdateFlags;

  virtual ~DeviceAdapter() = default;

  /// @return reference to the actual device this adapter is part of
  /// @note this method must be implemented in all final subclasses of Device for delegates to access the
  ///    device instance
  virtual Device& device() = 0;

  /// @return const to the actual device this adapter is part of
  inline const Device& const_device() const { return const_cast<DeviceAdapter *>(this)->device(); }

  /// @return non-null pointer to subclass pointer as specified by DevType, or fails assertion.
  template<typename DevType> auto deviceP() { auto devP = dynamic_cast<DevType*>(&device()); assert(devP); return devP; }

  /// Convenience function (as the endpointID is needed often to access attributes
  /// @return the endpointId
  EndpointId endpointId() { return device().endpointId(); }
};

// logging from within adapter implementation classes
#define DLOGENABLED(lvl) (device().logEnabled(lvl))
#define DLOG(lvl,...) { if (device().logEnabled(lvl)) device().log(lvl,##__VA_ARGS__); }
