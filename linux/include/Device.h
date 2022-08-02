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

    enum Changed_t
    {
        kChanged_Reachable = 1u << 0,
        kChanged_Location  = 1u << 1,
        kChanged_Name      = 1u << 2,
        kChanged_Last      = kChanged_Name,
    } Changed;

    // P44
    std::string mBridgedDSUID;

    Device(const char * szDeviceName, std::string szLocation, std::string aDSUID);
    virtual ~Device();

    bool IsReachable();
    void SetReachable(bool aReachable);
    void SetName(const char * szDeviceName);
    void SetLocation(std::string szLocation);
    //inline void SetEndpointId(chip::EndpointId id) { mEndpointId = id; };
    inline void SetDynamicEndpointIdx(chip::EndpointId aIdx) { mDynamicEndpointIdx = aIdx; };
    inline chip::EndpointId GetEndpointId() { return mDynamicEndpointBase+mDynamicEndpointIdx; };
    inline void SetParentEndpointId(chip::EndpointId id) { mParentEndpointId = id; };
    inline chip::EndpointId GetParentEndpointId() { return mParentEndpointId; };
    inline char * GetName() { return mName; };
    inline std::string GetLocation() { return mLocation; };
    inline std::string GetZone() { return mZone; };
    inline void SetZone(std::string zone) { mZone = zone; };

    // FIXME: ugly Q&D cluster setup
    void setUpClusterInfo(
      size_t aNumClusterVersions,
      EmberAfEndpointType* aEndpointTypeP,
      const Span<const EmberAfDeviceType>& aDeviceTypeList,
      EndpointId aParentEndpointId = chip::kInvalidEndpointId
    );

    // add the device using the previously set cluster info
    // @param aDynamicEndpointBase the ID of the first dynamic endpoint
    bool AddAsDeviceEndpoint(EndpointId aDynamicEndpointBase);

private:
    virtual void HandleDeviceChange(Device * device, Device::Changed_t changeMask) = 0;

protected:
    bool mReachable;
    char mName[kDeviceNameSize];
    std::string mLocation;
    //chip::EndpointId mEndpointId;
    chip::EndpointId mParentEndpointId;
    std::string mZone;
    EndpointId mDynamicEndpointBase;
    EndpointId mDynamicEndpointIdx;

};
typedef boost::intrusive_ptr<Device> DevicePtr;






class DeviceOnOff : public Device
{
public:
    enum Changed_t
    {
        kChanged_OnOff = kChanged_Last << 1,
    } Changed;

    DeviceOnOff(const char * szDeviceName, std::string szLocation, std::string aDSUID);

    bool IsOn();
    void SetOnOff(bool aOn);
    void Toggle();

    using DeviceCallback_fn = std::function<void(DeviceOnOff *, DeviceOnOff::Changed_t)>;
    void SetChangeCallback(DeviceCallback_fn aChanged_CB);

private:
    void HandleDeviceChange(Device * device, Device::Changed_t changeMask);

private:
    bool mOn;
    DeviceCallback_fn mChanged_CB;
};



{
public:

private:
};

