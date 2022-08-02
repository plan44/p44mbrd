/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
 *    Copyright (c) 2019 Google LLC.
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

#include "Device.h"
#include "bridgeapi.hpp"

#include <cstdio>
#include <platform/CHIPDeviceLayer.h>

using namespace chip::app::Clusters::BridgedActions;

// MARK: - Device

Device::Device(const char * szDeviceName, std::string szLocation, std::string aDSUID)
{
    strncpy(mName, szDeviceName, sizeof(mName));
    mLocation   = szLocation;
    mReachable  = false;
    mDynamicEndpointIdx = kInvalidEndpointId;
    mDynamicEndpointBase = 0;
    // p44
    mBridgedDSUID = aDSUID;
    // - instantiation info
    mNumClusterVersions = 0;
    mClusterDataVersionsP = nullptr;
    mEndpointTypeP = nullptr;
    mDeviceTypeListP = nullptr;
    mParentEndpointId = kInvalidEndpointId;
}


Device::~Device()
{
  if (mClusterDataVersionsP) {
    delete mClusterDataVersionsP;
    mClusterDataVersionsP = nullptr;
  }
}


bool Device::IsReachable()
{
    return mReachable;
}

void Device::SetReachable(bool aReachable)
{
    bool changed = (mReachable != aReachable);

    mReachable = aReachable;

    if (aReachable)
    {
        ChipLogProgress(DeviceLayer, "Device[%s]: ONLINE", mName);
    }
    else
    {
        ChipLogProgress(DeviceLayer, "Device[%s]: OFFLINE", mName);
    }

    if (changed)
    {
        HandleDeviceChange(this, kChanged_Reachable);
    }
}

void Device::SetName(const char * szName)
{
    bool changed = (strncmp(mName, szName, sizeof(mName)) != 0);

    ChipLogProgress(DeviceLayer, "Device[%s]: New Name=\"%s\"", mName, szName);
    strncpy(mName, szName, sizeof(mName));

    if (changed)
    {
        JsonObjectPtr params = JsonObject::newObj();
        params->add("dSUID", JsonObject::newString(mBridgedDSUID));
        JsonObjectPtr props = JsonObject::newObj();
        props->add("name", JsonObject::newString(mName));
        params->add("properties", props);
        BridgeApi::sharedBridgeApi().call("setProperty", params, NULL);

        HandleDeviceChange(this, kChanged_Name);
    }
}

void Device::SetLocation(std::string szLocation)
{
    bool changed = (mLocation.compare(szLocation) != 0);

    mLocation = szLocation;

    ChipLogProgress(DeviceLayer, "Device[%s]: Location=\"%s\"", mName, mLocation.c_str());

    if (changed)
    {
        HandleDeviceChange(this, kChanged_Location);
    }
}


void Device::setUpClusterInfo(
  size_t aNumClusterVersions,
  EmberAfEndpointType* aEndpointTypeP,
  const Span<const EmberAfDeviceType>& aDeviceTypeList,
  EndpointId aParentEndpointId
)
{
  // save number of clusters and create storage for cluster data versions
  mNumClusterVersions = aNumClusterVersions;
  if (mClusterDataVersionsP) delete mClusterDataVersionsP;
  mClusterDataVersionsP = new DataVersion[mNumClusterVersions];
  // save other params
  mEndpointTypeP = aEndpointTypeP;
  mDeviceTypeListP = &aDeviceTypeList;
  mParentEndpointId = aParentEndpointId;
}


bool Device::AddAsDeviceEndpoint(EndpointId aDynamicEndpointBase)
{
  // allocate data versions
  mDynamicEndpointBase = aDynamicEndpointBase;
  EmberAfStatus ret = emberAfSetDynamicEndpoint(
    mDynamicEndpointIdx,
    GetEndpointId(),
    mEndpointTypeP,
    Span<DataVersion>(mClusterDataVersionsP, mNumClusterVersions),
    *mDeviceTypeListP,
    mParentEndpointId
  );
  if (ret==EMBER_ZCL_STATUS_SUCCESS) {
    ChipLogProgress(
      DeviceLayer, "Added device %s to dynamic endpoint %d (index=%d)",
      GetName(), GetEndpointId(), mDynamicEndpointIdx
    );
  }
  else {
    ChipLogError(DeviceLayer, "emberAfSetDynamicEndpoint failed with EmberAfStatus=%d", ret);
    return false;
  }
  return true;
}



// MARK: - DeviceOnOff

DeviceOnOff::DeviceOnOff(const char * szDeviceName, std::string szLocation, std::string aDSUID) : Device(szDeviceName, szLocation, aDSUID)
{
    mOn = false;
}

bool DeviceOnOff::IsOn()
{
    return mOn;
}

void DeviceOnOff::SetOnOff(bool aOn)
{
    bool changed;

    changed = aOn ^ mOn;
    mOn     = aOn;
    ChipLogProgress(DeviceLayer, "Device[%s]: %s", mName, aOn ? "ON" : "OFF");

    if (changed)
    {
        // call preset1 or off on the bridged device
        JsonObjectPtr params = JsonObject::newObj();
        params->add("dSUID", JsonObject::newString(mBridgedDSUID));
        params->add("scene", JsonObject::newInt32(mOn ? 5 : 0));
        params->add("force", JsonObject::newBool(true));
        BridgeApi::sharedBridgeApi().notify("callScene", params);

        if (mChanged_CB)
        {
            mChanged_CB(this, kChanged_OnOff);
        }
    }
}

void DeviceOnOff::Toggle()
{
    bool aOn = !IsOn();
    SetOnOff(aOn);
}

void DeviceOnOff::SetChangeCallback(DeviceCallback_fn aChanged_CB)
{
    mChanged_CB = aChanged_CB;
}

void DeviceOnOff::HandleDeviceChange(Device * device, Device::Changed_t changeMask)
{
    if (mChanged_CB)
    {
        mChanged_CB(this, (DeviceOnOff::Changed_t) changeMask);
    }
}
