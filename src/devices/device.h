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

#include <app/util/attribute-storage.h>
#include <app/util/af-types.h>

#include <stdbool.h>
#include <stdint.h>

#include <functional>
#include <list>
#include <string>
#include <vector>

#include <controller/CHIPCluster.h>

#include "bridgeapi.h"
#include "p44mbrd_main.h"
#include "logger.hpp"

using namespace chip;
using namespace app;
using namespace std;
using namespace p44;

using Status = Protocols::InteractionModel::Status;

class Device;
typedef boost::intrusive_ptr<Device> DevicePtr;

// FIXME: put these somewhere more suitable
DevicePtr deviceForEndPointIndex(EndpointId aDynamicEndpointIndex);
DevicePtr deviceForEndPointId(EndpointId aEndpointId);

class DeviceEndpoints
{
public:
  template<typename DevType> static boost::intrusive_ptr<DevType> getDevice(EndpointId aEndpointId) {
    return dynamic_pointer_cast<DevType>(deviceForEndPointId(aEndpointId));
  };

};

typedef std::list<DevicePtr> DevicesList;



// @brief delegate for obtaining device information
class DeviceInfoDelegate
{
  virtual string getVendorName() = 0;
  virtual string getModelName() = 0;
  virtual string getConfigUrl() = 0;
}



class Device : public p44::P44LoggingObj
{
  // info for instantiating
  Span<const EmberAfDeviceType> mDeviceTypeList; ///< span pointing to device type list
  EmberAfEndpointType mEndpointDefinition; ///< endpoint declaration info
  DataVersion* mClusterDataVersionsP; ///< storage for cluster versions, one for each .cluster in mEndpointDefinition
  std::list<Span<EmberAfCluster>> mClusterListCollector; ///< used to dynamically collect cluster info

  // constant after init
  bool mPartOfComposedDevice;
  EndpointId mParentEndpointId;
  EndpointId mDynamicEndpointBase;
  EndpointId mDynamicEndpointIdx;

  // runtime variable attributes
  bool mReachable; ///< note: this is only the currently reported state, derived from mBridgeable and mActive
  string mName;
  string mZone;

  // internal state
  bool mBridgeable;
  bool mActive;

  // delegate
  DeviceInfoDelegate& mDeviceInfoDelegate;

protected:

  // possible subdevices
  DevicesList mSubdevices;

public:

  Device(DeviceInfoDelegate& aDeviceInfoDelegate);
  virtual ~Device();

  virtual string logContextPrefix() override;

  /// @return a short name for the type of device
  virtual const char *deviceType() = 0;

  /// @return a description of the device, usually including current state
  virtual string description();

  /// @return list of subdevices, non-empty if this is a composed device
  DevicesList& subDevices() { return mSubdevices; };

  /// Set this device to behave as part of a composed device
  inline void flagAsPartOfComposedDevice() { mPartOfComposedDevice = true; };

  bool IsReachable();
  inline chip::EndpointId GetEndpointId() { return mDynamicEndpointBase+mDynamicEndpointIdx; };
  inline chip::EndpointId GetParentEndpointId() { return mParentEndpointId; };
  inline string GetName() { return mName; };
  inline string GetZone() { return mZone; };

  enum class UpdateFlags : uint8_t
  {
    bridged = 0x01, ///< update state in bridge (send change notification/call)
    matter = 0x02, ///< update state in matter (report attribute as changed)
    noderive = 0x10, ///< do not derive anything from this change
    chained = 0x20, ///< this update was triggered by another update (prevent recursion)
    noapply = 0x40, ///< do not apply right now when updating bridge (i.e. color components)
    forced = 0x80 ///< perform updates even when cached state has not changed
  };
  using UpdateMode = BitFlags<UpdateFlags>;

  // propagating setters
  void updateReachable(bool aReachable, UpdateMode aUpdateMode);
  void updateName(const string aDeviceName, UpdateMode aUpdateMode);

  // setup setters
  inline void SetParentEndpointId(chip::EndpointId aID) { mParentEndpointId = aID; };
  inline void SetDynamicEndpointIdx(chip::EndpointId aIdx) { mDynamicEndpointIdx = aIdx; };
  inline void SetDynamicEndpointBase(EndpointId aDynamicEndpointBase) { mDynamicEndpointBase = aDynamicEndpointBase; };
  inline void initName(const string aName) { mName = aName; };
  inline void initZone(string aZone) { mZone = aZone; };


  /// add the device using the previously set cluster info (and parent endpoint ID)
  virtual bool AddAsDeviceEndpoint();

  /// called when device is instantiated and registered in CHIP (and chip is running)
  virtual void inChipInit();

  /// handler for external attribute read access
  virtual EmberAfStatus HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength);

  /// handler for external attribute write access
  virtual EmberAfStatus HandleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer);

protected:

  /// return suffix for endpointDSUID() for when this device is installed as a subdevice
  virtual const string endPointDSUIDSuffix() { return "output"; }

  /// Add a cluster declaration during device setup
  /// @note preferably this should be called from ctor, to have general cluster defs first
  /// @note this replaces use of `DECLARE_DYNAMIC_CLUSTER_LIST_xxx` macros, to allow dynamically collecting needed
  ///    clusters over the class hierachy.
  /// @param aClusterDeclarationList the cluster declarations
  void addClusterDeclarations(const Span<EmberAfCluster>& aClusterDeclarationList);

  /// called to have the final leaf class declare the correct device type list
  virtual void finalizeDeviceDeclaration() = 0;

  /// utility for implementing finalizeDeviceDeclaration()
  void finalizeDeviceDeclarationWithTypes(const Span<const EmberAfDeviceType>& aDeviceTypeList);

  /// utility for returning attribute values in external attribute storage callbacks
  template <typename T> static inline void storeInBuffer(uint8_t* aBuffer, T& aValue)
  {
    *((T*)aBuffer) = aValue;
  }

  template <typename T> static inline EmberAfStatus getAttr(uint8_t* aBuffer, uint16_t aMaxReadLength, T aValue)
  {
    using ST = typename app::NumericAttributeTraits<T>::StorageType;
    if (aMaxReadLength==sizeof(ST)) {
      *((ST*)aBuffer) = aValue;
      return EMBER_ZCL_STATUS_SUCCESS;
    }
    else {
      return EMBER_ZCL_STATUS_FAILURE;
    }
  }

  template <typename T> static inline EmberAfStatus setAttr(T &aValue, uint8_t* aBuffer)
  {
    aValue = *((T*)aBuffer);
    return EMBER_ZCL_STATUS_SUCCESS;
  }

};


/// @brief delegate for making a device identify itself
class IdentifyDelegate
{
  /// start or stop identification of the device
  /// @param aDurationS >0: number of seconds the identification action
  ///   on the hardware device should perform, such as blinking or beeping.
  ///   0: use default duration of hardware device
  ///   <0: stop ongoing identification
  virtual identify(int aDurationS) = 0;
}


class IdentifiableDevice : public Device, public IdentifyInterface
{
  typedef Device inherited;

  uint16_t mIdentifyTime;
  MLTicket mIdentifyTickTimer;

  IdentifyDelegate& mIdentifyDelegate;

public:

  IdentifiableDevice(IdentifyDelegate& aIdentifyDelegate, DeviceInfoDelegate& aDeviceInfoDelegate);
  virtual ~IdentifiableDevice();

  virtual EmberAfStatus HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength) override;
  virtual EmberAfStatus HandleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer) override;

  bool updateIdentifyTime(uint16_t aIdentifyTime, UpdateMode aUpdateMode);

protected:

  virtual uint8_t identifyType() { return EMBER_ZCL_IDENTIFY_IDENTIFY_TYPE_NONE; }

private:

  void identifyTick(uint16_t aRemainingSeconds);

};


#if COMPLETE

class ComposedDevice : public Device
{
  typedef Device inherited;

public:

  ComposedDevice();
  virtual ~ComposedDevice();

  virtual const char *deviceType() override { return "composed"; }

  virtual string description() override;

  void addSubdevice(DevicePtr aSubDevice);

  virtual void handleBridgePushProperties(JsonObjectPtr aChangedProperties) override;

protected:

  virtual void finalizeDeviceDeclaration() override;

};


class InputDevice : public Device
{
  typedef Device inherited;

protected:

  string mInputType;
  string mInputId;

public:

  InputDevice();
  virtual ~InputDevice();

  virtual void initBridgedInfo(JsonObjectPtr aDeviceInfo, JsonObjectPtr aDeviceComponentInfo = nullptr, const char* aInputType = nullptr, const char* aInputId = nullptr) override;

protected:

  virtual const string endPointDSUIDSuffix() override { return mInputType + "_" + mInputId; }

};

#endif
