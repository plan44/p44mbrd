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

using namespace chip;
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

//  template<typename DevType> static bool handleCallback(
//    chip::app::CommandHandler * commandObj, const chip::app::ConcreteCommandPath & commandPath,
//    const chip::app::Clusters::LevelControl::Commands::MoveToLevel::DecodableType & commandData,
//    void (DevType::*)(
//      CommandId aCommandId,
//      CommandData aCommandData
//    )
//  );
  template<typename DevType> static boost::intrusive_ptr<DevType> getDevice(EndpointId aEndpointId) {
    return dynamic_pointer_cast<DevType>(deviceForEndPointId(aEndpointId));
  };

};



class Device : public p44::P44Obj
{
  // info for instantiating
  Span<const EmberAfDeviceType> mDeviceTypeList; ///< span pointing to device type list
  EmberAfEndpointType mEndpointDefinition; ///< endpoint declaration info
  DataVersion* mClusterDataVersionsP; ///< storage for cluster versions, one for each .cluster in mEndpointDefinition
  std::list<Span<EmberAfCluster>> mClusterListCollector; ///< used to dynamically collect cluster info

  // device info for bridged device
  string mVendorName;
  string mModelName;
  string mConfigUrl;

  // constant after init
  string mBridgedDSUID;
  EndpointId mParentEndpointId;
  EndpointId mDynamicEndpointBase;
  EndpointId mDynamicEndpointIdx;

  // runtime variable attributes
  bool mReachable;
  string mName;
  string mZone;

public:

  Device();
  virtual ~Device();

  virtual void logStatus(const char *aReason = NULL);

  virtual void initBridgedInfo(JsonObjectPtr aDeviceInfo);

  const string bridgedDSUID() { return mBridgedDSUID; };

  bool IsReachable();
  inline chip::EndpointId GetEndpointId() { return mDynamicEndpointBase+mDynamicEndpointIdx; };
  inline chip::EndpointId GetParentEndpointId() { return mParentEndpointId; };
  inline string GetName() { return mName; };
  inline string GetZone() { return mZone; };

  enum class UpdateFlags : uint8_t
  {
    bridged = 0x01, ///< update state in bridge (send change notification/call)
    matter = 0x02, ///< update state in matter (report attribute as changed)
    noapply = 0x40, ///< do not apply right now when updating bridge (i.e. color components)
    forced = 0x80 ///< perform updates even when cached state has not changed
  };
  using UpdateMode = BitFlags<UpdateFlags>;

  // propagating setters
  void updateReachable(bool aReachable, UpdateMode aUpdateMode);
  void updateName(const string aDeviceName, UpdateMode aUpdateMode);

  // setup setters
  inline void SetDynamicEndpointIdx(chip::EndpointId aIdx) { mDynamicEndpointIdx = aIdx; };
  inline void initName(const string aName) { mName = aName; };
  inline void initZone(string aZone) { mZone = aZone; };


  /// add the device using the previously set cluster info
  /// @param aDynamicEndpointBase the ID of the first dynamic endpoint
  bool AddAsDeviceEndpoint(EndpointId aDynamicEndpointBase, EndpointId aParentEndpoint);

  /// called after instantiating device as dynamic endpoint, but before chip mainloop starts
  virtual void beforeChipMainloopPrep();

  /// called as a chip mainloop scheduled work just after mainloop starts
  virtual void inChipMainloopInit();

  /// handler for external attribute read access
  virtual EmberAfStatus HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength);

  /// handler for external attribute write access
  virtual EmberAfStatus HandleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer);

  /// @name bridge API helpers
  /// @{

  void notify(const string aNotification, JsonObjectPtr aParams);
  void call(const string aNotification, JsonObjectPtr aParams, JSonMessageCB aResponseCB);

  /// called to handle notifications from bridge
  bool handleBridgeNotification(const string aNotification, JsonObjectPtr aParams);

  /// called to handle pushed properties coming from bridge
  virtual void handleBridgePushProperties(JsonObjectPtr aChangedProperties);

  /// @}


protected:

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

};
