/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

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



class DeviceOnOff : public Device
{
  typedef Device inherited;
public:

  DeviceOnOff(const char * szDeviceName, std::string szLocation, std::string aDSUID);

  bool IsOn() { return mOn; }
  bool SetOnOff(bool aOn);

  /// handler for external attribute read access
  virtual EmberAfStatus HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength) override;
  /// handler for external attribute write access
  virtual EmberAfStatus HandleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer) override;

private:
  bool mOn;
};


class DeviceDimmable : public DeviceOnOff
{
  typedef DeviceOnOff inherited;
public:

  DeviceDimmable(const char * szDeviceName, std::string szLocation, std::string aDSUID);

  uint8_t currentLevel() { return mLevel; };
  bool setCurrentLevel(uint8_t aNewLevel);

  /// handler for external attribute read access
  virtual EmberAfStatus HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength) override;
  /// handler for external attribute write access
  virtual EmberAfStatus HandleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer) override;

private:
  uint8_t mLevel;
};


class DeviceColor : public DeviceDimmable
{
  typedef DeviceDimmable inherited;
public:

  DeviceColor(const char * szDeviceName, std::string szLocation, std::string aDSUID, bool aCTOnly);

  uint8_t currentHue() { return mHue; };
  uint8_t currentSaturation() { return mSaturation; };
  uint16_t currentColortemp() { return mColorTemp; };

  bool setCurrentHue(uint8_t aHue);
  bool setCurrentSaturation(uint8_t aSaturation);
  bool setCurrentColortemp(uint8_t aColortemp);

  /// handler for external attribute read access
  virtual EmberAfStatus HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength) override;
  /// handler for external attribute write access
  virtual EmberAfStatus HandleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer) override;

private:
  bool mCtOnly;
  uint8_t mColorMode; // 0=HS, 1=XY, 2=Colortemp
  uint8_t mHue;
  uint8_t mSaturation;
  uint16_t mColorTemp;
};

