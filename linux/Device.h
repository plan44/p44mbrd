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

#include <stdbool.h>
#include <stdint.h>

#include <functional>
#include <vector>

#include "p44obj.hpp"

using namespace chip;

class Device : public p44::P44Obj
{
  // info for instantiating
  size_t mNumClusterVersions;
  DataVersion* mClusterDataVersionsP;
  EmberAfEndpointType* mEndpointTypeP;
  const Span<const EmberAfDeviceType> *mDeviceTypeListP;

public:
  static const int kDeviceNameSize = 32;

  // P44
  std::string mBridgedDSUID;

  Device(const char * szDeviceName, std::string szLocation, std::string aDSUID);
  virtual ~Device();

  bool IsReachable();
  void SetReachable(bool aReachable);
  void SetName(const char * szDeviceName);
  void SetLocation(std::string szLocation);
  inline void SetDynamicEndpointIdx(chip::EndpointId aIdx) { mDynamicEndpointIdx = aIdx; };
  inline chip::EndpointId GetEndpointId() { return mDynamicEndpointBase+mDynamicEndpointIdx; };
  inline void SetParentEndpointId(chip::EndpointId id) { mParentEndpointId = id; };
  inline chip::EndpointId GetParentEndpointId() { return mParentEndpointId; };
  inline char * GetName() { return mName; };
  inline std::string GetLocation() { return mLocation; };
  inline std::string GetZone() { return mZone; };
  inline void SetZone(std::string zone) { mZone = zone; };

  // FIXME: ugly Q&D cluster setup. Move cluster declaration into class itself, later
  void setUpClusterInfo(
    size_t aNumClusterVersions,
    EmberAfEndpointType* aEndpointTypeP,
    const Span<const EmberAfDeviceType>& aDeviceTypeList,
    EndpointId aParentEndpointId = chip::kInvalidEndpointId
  );

  /// add the device using the previously set cluster info
  /// @param aDynamicEndpointBase the ID of the first dynamic endpoint
  bool AddAsDeviceEndpoint(EndpointId aDynamicEndpointBase);

  /// handler for external attribute read access
  virtual EmberAfStatus HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength);

  /// handler for external attribute write access
  virtual EmberAfStatus HandleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer);

protected:

  bool mReachable;
  char mName[kDeviceNameSize];
  std::string mLocation;
  chip::EndpointId mParentEndpointId;
  std::string mZone;
  EndpointId mDynamicEndpointBase;
  EndpointId mDynamicEndpointIdx;

};
typedef boost::intrusive_ptr<Device> DevicePtr;
