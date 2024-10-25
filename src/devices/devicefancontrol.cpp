//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 7

#include "device_impl.h" // include as first file!
#include "devicefancontrol.h"

using namespace Clusters;
using namespace FanControl;
using namespace Attributes;

// MARK: - DeviceFanControl

static EmberAfClusterSpec gFanControlClusters[] = {
  { FanControl::Id, CLUSTER_MASK_SERVER },
  { Groups::Id, CLUSTER_MASK_SERVER }
};

static const EmberAfDeviceType gFanDeviceTypes[] = {
  { DEVICE_TYPE_MA_FAN_DEVICE, DEVICE_VERSION_DEFAULT },
  { DEVICE_TYPE_MA_BRIDGED_DEVICE, DEVICE_VERSION_DEFAULT }
};


DeviceFanControl::DeviceFanControl(FanControlExtrasDelegate* aOptionalFanControlExtrasDelegate, LevelControlDelegate& aLevelControlDelegate, IdentifyDelegate& aIdentifyDelegate, DeviceInfoDelegate& aDeviceInfoDelegate) :
  inherited(aIdentifyDelegate, aDeviceInfoDelegate),
  FanControl::Delegate(kInvalidEndpointId), // nobody needs that ID stored in the delegate, and we want to instantiate it here where we don't know the
  mLevelControlDelegate(aLevelControlDelegate),
  mFanControlExtrasDelegateP(aOptionalFanControlExtrasDelegate)
{
  // - declare onoff device specific clusters
  useClusterTemplates(Span<EmberAfClusterSpec>(gFanControlClusters));
}


string DeviceFanControl::description()
{
  string s = inherited::description();
  // maybe add attributes
  return s;
}


void DeviceFanControl::finalizeDeviceDeclaration()
{
  finalizeDeviceDeclarationWithTypes(Span<const EmberAfDeviceType>(gFanDeviceTypes));
}


void DeviceFanControl::didGetInstalled()
{
  // install delegate
  FanControl::SetDefaultDelegate(endpointId(), this);
  // static settings
  bool hasAuto = mFanControlExtrasDelegateP && mFanControlExtrasDelegateP->hasAutoMode();
  FanModeSequence::Set(endpointId(), hasAuto ? FanModeSequenceEnum::kOffLowMedHighAuto : FanModeSequenceEnum::kOffLowMedHigh);
  FeatureMap::Set(endpointId(), to_underlying(Feature::kMultiSpeed) | (hasAuto ? to_underlying(Feature::kAuto) : 0));
  // call base class last
  inherited::didGetInstalled();
}

// MARK: LevelControlImplementationInterface

void DeviceFanControl::setDefaultOnLevel(double aLevelPercent)
{
  /* NOP, not needed in FanControl */
}


bool DeviceFanControl::updateLevel(double aLevelPercent, Device::UpdateMode aUpdateMode)
{
  Percent currentLevel = static_cast<uint8_t>(aLevelPercent);
  Percent previousLevel;
  PercentCurrent::Get(endpointId(), &previousLevel);
  if (currentLevel!=previousLevel || aUpdateMode.Has(UpdateFlags::forced)) {
    if (aUpdateMode.Has(UpdateFlags::bridged)) {
      mLevelControlDelegate.setLevel(currentLevel, 0);
    }
    if (aUpdateMode.Has(UpdateFlags::matter)) {
      PercentCurrent::Set(endpointId(), currentLevel);
    }
    return true;
  }
  return false;
}


// MARK: internals

bool DeviceFanControl::updateAuto(bool aAuto, double aLevel, Device::UpdateMode aUpdateMode)
{
  FanModeEnum fanMode;
  FanMode::Get(endpointId(), &fanMode);
  bool nowauto = fanMode==FanModeEnum::kAuto;
  if (aAuto!=nowauto || aUpdateMode.Has(UpdateFlags::forced)) {
    if (aUpdateMode.Has(UpdateFlags::bridged) && mFanControlExtrasDelegateP) {
      mFanControlExtrasDelegateP->setAutoMode(aAuto, aLevel);
    }
    if (aUpdateMode.Has(UpdateFlags::matter)) {
      setImpliedLevel(aAuto ? 255 : static_cast<Percent>(aLevel));
    }
    return true;
  }
  return false;
}



void DeviceFanControl::setImpliedLevel(Percent aLevelPercent)
{
  if (aLevelPercent>100 && mFanControlExtrasDelegateP && mFanControlExtrasDelegateP->hasAutoMode()) {
    // automatic
    PercentSetting::SetNull(endpointId()); // null value indicates auto
  }
  else {
    if (aLevelPercent>100) aLevelPercent = 100; // just max
    PercentSetting::Set(endpointId(), aLevelPercent); // fixed level
  }
}


// MARK: attribute access

void DeviceFanControl::handleAttributeChange(ClusterId aClusterId, chip::AttributeId aAttributeId)
{
  if (aClusterId==FanControl::Id) {
    if (aAttributeId==FanMode::Id) {
      // handle mode change
      FanModeEnum fanMode;
      FanMode::Get(endpointId(), &fanMode);
      switch(fanMode) {
        case FanModeEnum::kOff: setImpliedLevel(0); break;
        case FanModeEnum::kLow: setImpliedLevel(33); break;
        case FanModeEnum::kMedium: setImpliedLevel(66); break;
        case FanModeEnum::kOn:
        case FanModeEnum::kHigh: setImpliedLevel(100); break;
        case FanModeEnum::kAuto: setImpliedLevel(255); break; // >100 = auto
        default: break; // unknown
      }
    }
    else if (aAttributeId==PercentSetting::Id) {
      // handle percent change
      DataModel::Nullable<Percent> p;
      PercentSetting::Get(endpointId(), p);
      if (!p.IsNull()) {
        updateLevel(p.Value(), UpdateFlags::bridged);
      }
    }
  }
  // let base class check as well
  return inherited::handleAttributeChange(aClusterId, aAttributeId);
}


// MARK: FanControl::Delegate

Status DeviceFanControl::HandleStep(FanControl::StepDirectionEnum aDirection, bool aWrap, bool aLowestOff)
{
  // we just support, 0,1,2,3 in 33% steps for now
  int dir = aDirection == StepDirectionEnum::kIncrease ? 1 : -1;
  Percent currentLevel;
  PercentCurrent::Get(endpointId(), &currentLevel);
  int stage = (currentLevel+16) / 33;
  stage += dir;
  int minstage = aLowestOff ? 0 : 1;
  if (stage>3) {
    if (aWrap) stage = minstage; else stage = 3;
  }
  if (stage<minstage) {
    if (aWrap) stage = 3; else stage = minstage;
  }
  PercentSetting::Set(endpointId(), static_cast<Percent>(stage==0 ? 0 : stage*33+1));
  return Status::Success;
}
