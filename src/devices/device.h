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

#include "p44mbrd_main.h"
#include "matter_utils.h"

#include "logger.hpp"


using namespace chip;
using namespace app;
using namespace Clusters;
using namespace std;
using namespace p44;

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
public:

  virtual ~DeviceInfoDelegate() = default;

  /// @brief called just after the device got installed into the matter stack
  /// @note at this point, the device has a valid endpointId and can
  ///   access attributes. This is the place to override RAM attribute default
  ///   values with values obtained from device setup
  virtual void deviceDidGetInstalled() { /* NOP */ };

  /// @return unique identifier for the (part of a) bridged device that is represented by the
  ///   matter device this object is the device info delegate of.
  /// @note the identifier is defined by the device adapter implementation such that it allows
  ///   the adapter to identify a the correct bridged device (or, in case of composed device, part of the
  ///   device) when presented with this ID.
  virtual const string endpointUID() const = 0;

  /// @return true if the device hardware is reachable at this time
  virtual bool isReachable() const = 0;

  /// @return name of the device as in use at the far end of the bridge
  /// @note this may or may not be in sync with the nodeLabel as used by matter. Usually, this serves
  ///   as a suggestion for naming devices at commissioning (depending on the commissioner implementation)
  virtual string name() const = 0;

  /// Request to change the bridged device's name
  /// @note this is called when matter side receives a nodeLabel change. Implementation can reject the change.
  /// @return true if name could be changed in the bridged device
  virtual bool changeName(const string aNewName) { return false; /* not changeable from matter side by default */ }

  /// @return name of the zone (area, room) the device is in as seen at the far end of the bridge
  virtual string zone() const = 0;

  /// Request to change the zone
  /// @return true if zone could be changed in the bridged device
  virtual bool changeZone(const string aNewZone) { return false; /* not changeable from matter side by default */ }

};


/// @brief Base class for all devices represented in matter by the bridge
class Device : public p44::P44LoggingObj
{
  /// device info delegate
  DeviceInfoDelegate& mDeviceInfoDelegate;

  /// @name matter device and cluster representations
  /// @{
  Span<EmberAfDeviceType> mDeviceTypeList; ///< span pointing to (allocated) device type list
  EmberAfEndpointType mEndpointDefinition; ///< endpoint declaration info
  DataVersion* mClusterDataVersionsP; ///< storage for cluster versions, one for each .cluster in mEndpointDefinition
  std::list<Span<EmberAfClusterSpec>> mTemplateClusterSpecSpanList; ///< used to dynamically collect template cluster ids
  /// @}

  /// @name matter endpointIds and device structure
  /// constant after init
  /// @{
  bool mPartOfComposedDevice; ///< set if this device is (or needs to become) part of a composed device
  DevicesList mSubdevices; ///< subdevices of this device (in case it is a composed device)
  EndpointId mEndpointId; ///< endpoint ID
  EndpointId mParentEndpointId; ///< endpointId of the parent (composed device or bridge itself)
  EndpointId mDynamicEndpointIdx; ///< the dynamic endpoint index to be used for this device
  /// @}

  /// @name runtime variable attributes
  /// @{
  bool mReachable; ///< currently reported reachable state, usually synchronized with actual hardware device reachability state
  string mNodeLabel; ///< currently reported node label, usually synchronized with actual device name
  string mZone;
  /// @}

public:

  /// @param aDeviceInfoDelegate object reference for implementation of device info handling
  Device(DeviceInfoDelegate& aDeviceInfoDelegate);

  virtual ~Device();

  /// @return a prefix string identifying the device for log messages issued via the OLOG macro
  virtual string logContextPrefix() override;

  /// @return a short name for the type of device
  virtual const char *deviceType() = 0;

  /// @return a description of the device, usually including current state
  virtual string description();

  /// @return list of subdevices, non-empty if this is a composed device
  DevicesList& subDevices() { return mSubdevices; };

  /// @return the device info delegate (needed by composed devices' delegates internally)
  inline DeviceInfoDelegate& deviceInfoDelegate() { return mDeviceInfoDelegate; }

  /// @name matter-side endpoint identification
  /// endpointIds are semi-stable "id"s (actually 16-bit indexes). The standard mandates not reusing indexes but
  /// always assigning incremented indexes for new devices, until wrap-around at 0xFFFF->0.
  /// @note This is not actually feasible with the current SDK implementation, which assumes endpointIds mostly
  ///   as something defined at compile time and thus immutable - not considering the bridge case at all.
  /// @{

  /// @return the endpointId of this device (can be part of a composed device)
  /// @note valid only after device setup is complete and device is operational
  inline chip::EndpointId endpointId() const { return mEndpointId; };

  /// @return the parentId of this device (can be a composed device or the bridge itself)
  /// @note valid only after device setup is complete and device is operational
  inline chip::EndpointId GetParentEndpointId() const { return mParentEndpointId; };

  /// @return true when this device endpoint is part of a composed device
  inline bool isPartOfComposedDevice() const { return mPartOfComposedDevice; };

  /// @}


  /// @name operational propagating setters
  /// These can be called while the device is active to propagate changes according to UpdateMode
  /// @{

  /// @brief update mode flags
  /// These control the propagation of changes towards matter and the bridged devices.
  enum class UpdateFlags : uint8_t
  {
    bridged = 0x01, ///< update state in bridge (send change notification/call)
    matter = 0x02, ///< update state in matter (report attribute as changed)
    noderive = 0x10, ///< do not derive anything from this change (in particular: not the color mode!)
    chained = 0x20, ///< this update was triggered by another update (prevent recursion)
    noapply = 0x40, ///< do not apply to hardware right now when updating bridge (i.e. color components)
    forced = 0x80 ///< perform updates even when cached state has not changed
  };
  using UpdateMode = BitFlags<UpdateFlags>; ///< update mode consisting of zero or more UpdateFlags

  /// @brief update the reachable status.
  /// Device adapters should call this when detecting reachability changes
  void updateReachable(bool aReachable, UpdateMode aUpdateMode);

  /// @brief update the node label.
  /// Device adapters managing a name string that should be in sync with the node label may call this
  void updateNodeLabel(const string aNodeLabel, UpdateMode aUpdateMode);

  /// @}


  /// @name setup setters
  /// These must ONLY be called during setup of a device, but NOT while a device is already operational.
  /// @{

  inline void SetEndpointId(chip::EndpointId aID) { mEndpointId = aID; };
  inline void SetDynamicEndpointIdx(chip::EndpointId aIdx) { mDynamicEndpointIdx = aIdx; };
  inline void SetParentEndpointId(chip::EndpointId aID) { mParentEndpointId = aID; };
  inline void initNodeLabel(const string aName) { mNodeLabel = aName; };
  inline void initZone(string aZone) { mZone = aZone; };

  /// @brief Set this device to behave as part of a composed device
  inline void flagAsPartOfComposedDevice() { mPartOfComposedDevice = true; };

  /// @}


  /// add the device using the previously set cluster info (and parent endpoint ID)
  bool addAsDeviceEndpoint();

  /// @brief called just before the device gets installed
  /// @note at this point, the device is the fully constructed final class, but is
  ///   not yet connected to the matter stack, so e.g. DOES NOT HAVE VALID endpointIDs
  ///   yet and cannot interoperate with the stack, IN PARTICULAR, CANNOT ACCESS
  ///   ATTRIBUTES.
  virtual void willBeInstalled();

  /// @brief called just after the device got installed into the matter stack
  /// @note at this point, the device has a valid endpointId and can
  ///   access attributes. This is the place to override RAM attribute default
  ///   values with values obtained from device setup
  virtual void didGetInstalled();

  /// @brief called when device has become operational within the matter stack
  virtual void didBecomeOperational();

  /// @brief called immediately before device gets disabled
  virtual void willBeDisabled();

  /// handler for external attribute read access
  virtual Status handleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength);

  /// handler for external attribute write access
  virtual Status handleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer);

  /// handler for getting notified after attribute was changed via a client writing to it
  virtual void handleAttributeChange(ClusterId clusterId, chip::AttributeId attributeId);

  /// utility to report attribute changes in this device to matter for reporting in subscriptions
  void reportAttributeChange(ClusterId aClusterId, chip::AttributeId aAttributeId);

protected:

  /// Use cluster declarations from a ZAP template endpoint during device setup
  /// @note preferably this should be called from ctor, to have general cluster defs first
  /// @note this replaces use of `DECLARE_DYNAMIC_CLUSTER_LIST_xxx` macros, to allow dynamically collecting needed
  ///   clusters over the class hierachy, and preventing re-declaration of clusters and attributes that are
  ///   already declared in ZAP. Together with dynamic attribute storage for RAM and NVRAM attributes,
  ///   this reduces setup of dynamic endpoints to a few lines, ensuring consistency with the ZAP definitions automatically.
  /// @param aTemplateClusterIdList a list of clusterIds. These must be present in the last fixed endpoint
  ///   (which must be defined in ZAP to generate accessors and cluster implementation, and contains all clusters
  ///   of all to-be-bridged devices. This template endpoint must be set to disabled)
  void useClusterTemplates(const Span<EmberAfClusterSpec>& aTemplateClusterSpecList);

  /// called to have the final leaf class declare the correct device type list
  virtual bool finalizeDeviceDeclaration() = 0;

  /// utility for implementing finalizeDeviceDeclaration()
  /// @return true when successful
  bool finalizeDeviceDeclarationWithTypes(const Span<const EmberAfDeviceType>& aDeviceTypeList);

  /// utility for returning attribute values in external attribute storage callbacks
  template <typename T> static inline void storeInBuffer(uint8_t* aBuffer, T& aValue)
  {
    *((T*)aBuffer) = aValue;
  }

  /// utility for extracting attribute data in HandleReadAttribute implementations
  template <typename T> static inline Status getAttr(uint8_t* aBuffer, uint16_t aMaxReadLength, T aValue)
  {
    using ST = typename app::NumericAttributeTraits<T>::StorageType;
    if (aMaxReadLength==sizeof(ST)) {
      *((ST*)aBuffer) = aValue;
      return Status::Success;
    }
    else {
      return Status::Failure;
    }
  }

  /// utility for preparing attribute data in HandleWriteAttribute implementations
  template <typename T> static inline Status setAttr(T &aValue, uint8_t* aBuffer)
  {
    aValue = *((T*)aBuffer);
    return Status::Success;
  }

};


/// @brief delegate for making a device identify itself
class IdentifyDelegate
{
public:

  virtual ~IdentifyDelegate() = default;

  /// start or stop identification of the device
  /// @param aDurationS >0: number of seconds the identification action
  ///   on the hardware device should perform, such as blinking or beeping.
  ///   0: use default duration of hardware device
  ///   <0: stop ongoing identification
  virtual void identify(int aDurationS) = 0;

  /// @return the identify type for this device
  virtual Identify::IdentifyTypeEnum identifyType() { return Identify::IdentifyTypeEnum::kNone; }
};


/// @brief base class for (usually output) devices which have a means to identify themselves to the user
class IdentifiableDevice : public Device
{
  typedef Device inherited;

  /// identify delegate
  IdentifyDelegate* mIdentifyDelegateP;

  /// @name external attributes
  /// @{

  uint16_t mIdentifyTime; ///< identify time, setting it directly affects identify

  /// @}

  MLTicket mIdentifyTickTimer;

public:

  /// @param aIdentifyDelegateP pointer for implementation of device level identification (sound, blinking etc.), null if none available
  /// @param aDeviceInfoDelegate object reference for implementation of device info handling
  IdentifiableDevice(IdentifyDelegate* aIdentifyDelegateP, DeviceInfoDelegate& aDeviceInfoDelegate);

  virtual ~IdentifiableDevice();

  virtual void didGetInstalled() override;

  virtual Status handleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength) override;
  virtual Status handleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer) override;

  /// interface for identify cluster command implementations
  bool updateIdentifyTime(uint16_t aIdentifyTime, UpdateMode aUpdateMode);

private:

  void identifyTick(uint16_t aRemainingSeconds);

};


/// @brief representation for a composed device with no functionality of its own, but just grouping some actual devices
class ComposedDevice : public Device
{
  typedef Device inherited;

public:

  /// @param aDeviceInfoDelegate object reference for implementation of device info handling
  ComposedDevice(DeviceInfoDelegate& aDeviceInfoDelegate) : Device(aDeviceInfoDelegate) {};

  virtual const char *deviceType() override { return "composed"; }

  virtual string description() override;

  /// @param aSubDevice device to add as part of this composed device
  void addSubdevice(DevicePtr aSubDevice);

protected:

  virtual bool finalizeDeviceDeclaration() override;

};
