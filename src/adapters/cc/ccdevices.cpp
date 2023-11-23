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

#include "ccdevices.h"

#if CC_ADAPTERS

#include "ccbridge.h"

using namespace p44;


// MARK: - CC_DeviceImpl

CC_DeviceImpl::CC_DeviceImpl(int _item_id) :
    item_id (_item_id)
{
}


string CC_DeviceImpl::uid_string(int aItem_id)
{
  return string_format("cc_id_%d", aItem_id);
}



// MARK: DeviceInfoDelegate implementation in CC_DeviceImpl base class

const string CC_DeviceImpl::endpointUID() const
{
  return uid_string(item_id);
}


void CC_DeviceImpl::deviceDidGetInstalled()
{
  // initialize read-only attributes that will not change during device lifetime
  // TODO: set a sensible value
  SET_ATTR_STRING(BridgedDeviceBasicInformation, SerialNumber, device().endpointId(), endpointUID());
  // TODO: set a sensible value
  SET_ATTR_STRING(BridgedDeviceBasicInformation, VendorName, device().endpointId(), "Becker Antriebe GmbH");
  // TODO: set a sensible value
  SET_ATTR_STRING(BridgedDeviceBasicInformation, ProductName, device().endpointId(), "Bridged Becker Device");
  // TODO: set a sensible value
  SET_ATTR_STRING(BridgedDeviceBasicInformation, ProductURL, device().endpointId(), "");

  updateBridgedInfo(NULL);
}


bool CC_DeviceImpl::isReachable() const
{
  // TODO: return actual reachability status
  return true;
}


string CC_DeviceImpl::name() const
{
  return mName;
}


bool CC_DeviceImpl::changeName(const string aNewName)
{
  if (aNewName!=mName) {
    mName = aNewName;
#if 0
    /* do not propagate back to deviced */
    JsonObjectPtr params = JsonObject::newObj();
    params->add("item_id", JsonObject::newInt32(CC_DeviceImpl::get_item_id ()));
    params->add("name", JsonObject::newString(aNewName));
    CC_BridgeImpl::adapter().api().sendRequest("item_set_name", params);
#endif

    updateBridgedInfo(NULL);
  }
  return true; // new name propagated
}


string CC_DeviceImpl::zone() const
{
  return mZone;
}


int CC_DeviceImpl::get_item_id()
{
  return item_id;
}


void CC_DeviceImpl::updateBridgedInfo(JsonObjectPtr aDeviceInfo)
{
  JsonObjectPtr o;
  // propagate locally stored info to matter attributes
  device().updateNodeLabel(mName, UpdateMode(UpdateFlags::matter));
}


// MARK: - CC_IdentifiableImpl

// MARK: IdentityDelegate implementation

void CC_IdentifiableImpl::identify(int aDurationS)
{
  // (re)start or stop identify in the bridged device
  // <0 = stop, >0 = duration (duration==0 would mean default duration, not used here)

  // TODO: make API calls to the device to draw user's attention to it...

  // probably something like
  JsonObjectPtr params = JsonObject::newObj();
  params->add("someting", JsonObject::newInt32(3));
  CC_BridgeImpl::adapter().api().sendRequest("some_method", params);
}


Identify::IdentifyTypeEnum CC_IdentifiableImpl::identifyType()
{
  // TODO: maybe return another identify type, depending on device features (LED?)
  return Identify::IdentifyTypeEnum::kActuator;
}


// MARK: - CC_OnOffImpl

// MARK: OnOffDelegate implementation

void CC_OnOffImpl::setOnOffState(bool aOn)
{

  // make API call to set the output state

  JsonObjectPtr params = JsonObject::newObj();
  params->add("group_id", JsonObject::newInt32 (get_item_id ()));
  params->add("command", JsonObject::newString ("switch"));
  params->add("value", JsonObject::newInt32 (aOn ? 1 : 0));
  DLOG(LOG_INFO, "sending deviced.group_send_command with params = %s", JsonObject::text(params));
  CC_BridgeImpl::adapter().api().sendRequest("deviced.group_send_command", params, boost::bind(&CC_OnOffImpl::onOffResponse, this, _1, _2, _3));

}


void CC_OnOffImpl::onOffResponse(int32_t aResponseId, ErrorPtr &aError, JsonObjectPtr aResultOrErrorData)
{
  DLOG(LOG_INFO, "got response for deviced.group_send_command: error=%s, result=%s", Error::text(aError), JsonObject::text(aResultOrErrorData));
}

// MARK: cc bridge specifics

// {"item_id":4,"config":{"name":"fump"}}
void CC_OnOffImpl::handle_config_changed(JsonObjectPtr aParams)
{
  JsonObjectPtr o, vo;
  if (aParams->get("config", o)) {
    if (o->get("name", vo)) {
      changeName(vo->stringValue());
    }
  }
}

// {"item_id":4,"state":{"error-flags":null,"value":1},"error_flags":[]}
void CC_OnOffImpl::handle_state_changed(JsonObjectPtr aParams)
{
  JsonObjectPtr o, vo;
  if (aParams->get("state", o)) {
    if (o->get("value", vo)) {
      deviceP<DeviceOnOff>()->updateOnOff(vo->int32Value()>0, UpdateMode(UpdateFlags::matter));
    }
  }
}




// MARK: - CC_WindowCoveringImpl

// MARK: WindowCoveringDelegate implementation


CC_WindowCoveringImpl::CC_WindowCoveringImpl(int _item_id, WindowCovering::Type _type, WindowCovering::EndProductType _end_product_type) :
  inherited(_item_id),
  mType(_type),
  mEndProductType(_end_product_type)
{
  // Note: CANNOT set up device attributes here, because we are not yet an installed endpoint.
  //   Do these inits in deviceDidGetInstalled()
}


void CC_WindowCoveringImpl::deviceDidGetInstalled()
{
  inherited::updateBridgedInfo(NULL);

  underlying_type_t<WindowCovering::Feature> featuremap = 0;
  switch (mType) {
    case WindowCovering::Type::kShutter:
    case WindowCovering::Type::kTiltBlindLiftAndTilt:
      // with tilt
      mHasTilt = true;
      // TODO: maybe init more specifics
      break;
    default:
    case WindowCovering::Type::kRollerShade:
    case WindowCovering::Type::kRollerShade2Motor:
    case WindowCovering::Type::kRollerShadeExterior:
    case WindowCovering::Type::kRollerShadeExterior2Motor:
    case WindowCovering::Type::kDrapery:
    case WindowCovering::Type::kAwning :
    case WindowCovering::Type::kProjectorScreen:
      // w/o tilt
      // TODO: maybe init more specifics
      break;
    case WindowCovering::Type::kTiltBlindTiltOnly:
      // not supported
      break;
  }
  // set features
  featuremap |= to_underlying(WindowCovering::Feature::kLift);
  if (feedback)
    featuremap |= to_underlying(WindowCovering::Feature::kPositionAwareLift);
  if (mHasTilt) {
    featuremap |= to_underlying(WindowCovering::Feature::kTilt);
    if (feedback)
      featuremap |= to_underlying(WindowCovering::Feature::kPositionAwareTilt);
  }
  WindowCovering::Attributes::FeatureMap::Set(endpointId(), featuremap);
  // set type
  WindowCovering::Attributes::Type::Set(endpointId(), mType);
  // set end product type
  WindowCovering::Attributes::EndProductType::Set(endpointId(), mEndProductType);
}



static double matter2bridge(const Percent100ths aPercent100th, bool aMotorDirectionReversed)
{
  double v = (double)aPercent100th/100;
  return aMotorDirectionReversed ? 100-v : v; // standard is: 100%=fully covered, 0%=fully open
}


static Percent100ths bridge2matter(double aBridgeValue, bool aMotorDirectionReversed)
{
  Percent100ths v = static_cast<Percent100ths>(aBridgeValue*100);
  if (v<100) v=0; // FIXME: q&d to get to zero, otherwise fully open position would not get recognized
  return aMotorDirectionReversed ? static_cast<chip::Percent100ths> (100*100 - v) : v; // standard is: 100%=fully covered, 0%=fully open
}


void CC_WindowCoveringImpl::startMovement(WindowCovering::WindowCoveringType aMovementType)
{
  // - get mode (for kMotorDirectionReversed)
  BitMask<WindowCovering::Mode> mode;
  WindowCovering::Attributes::Mode::Get(endpointId(), &mode);
  // - get target values for lift and tilt
  DataModel::Nullable<Percent100ths> lift;
  WindowCovering::Attributes::TargetPositionLiftPercent100ths::Get(endpointId(), lift);
  DataModel::Nullable<Percent100ths> tilt;
  WindowCovering::Attributes::TargetPositionTiltPercent100ths::Get(endpointId(), tilt);
  // TODO: implement starting movement
  // - make movement start, possibly invert 0..100 -> 100..0 when
  //   `mode.Has(WindowCovering::Mode::kMotorDirectionReversed)`
  // - check !lift.IsNull() and !tilt.IsNull() before using target values

  if (!lift.IsNull())
    {
      JsonObjectPtr params = JsonObject::newObj();
      params->add ("group_id", JsonObject::newInt32 (get_item_id ()));
      params->add ("command", JsonObject::newString ("moveto"));
      params->add ("value", JsonObject::newDouble (matter2bridge(lift.Value(), mode.Has(WindowCovering::Mode::kMotorDirectionReversed))));
      DLOG(LOG_INFO, "sending deviced.group_send_command with params = %s", JsonObject::text(params));
      CC_BridgeImpl::adapter().api().sendRequest("deviced.group_send_command", params, boost::bind(&CC_WindowCoveringImpl::windowCoveringResponse, this, _1, _2, _3));
    }

  if (!tilt.IsNull())
    {
      JsonObjectPtr params = JsonObject::newObj();
      params->add ("group_id", JsonObject::newInt32 (get_item_id ()));
      params->add ("command", JsonObject::newString ("tilt"));
      params->add ("value", JsonObject::newDouble (matter2bridge(tilt.Value(), mode.Has(WindowCovering::Mode::kMotorDirectionReversed))));
      DLOG(LOG_INFO, "sending deviced.group_send_command with params = %s", JsonObject::text(params));
      CC_BridgeImpl::adapter().api().sendRequest("deviced.group_send_command", params, boost::bind(&CC_WindowCoveringImpl::windowCoveringResponse, this, _1, _2, _3));
    }

}


void CC_WindowCoveringImpl::stopMovement()
{
  // - get mode (for kMotorDirectionReversed)
  BitMask<WindowCovering::Mode> mode;
  WindowCovering::Attributes::Mode::Get(endpointId(), &mode);

  // implement stopping movement

  JsonObjectPtr params = JsonObject::newObj();
  params->add ("group_id", JsonObject::newInt32 (get_item_id ()));
  params->add ("command", JsonObject::newString ("move"));
  params->add ("value", JsonObject::newInt32 (0));
  DLOG(LOG_INFO, "sending deviced.group_send_command with params = %s", JsonObject::text(params));
  CC_BridgeImpl::adapter().api().sendRequest("deviced.group_send_command", params, boost::bind(&CC_WindowCoveringImpl::windowCoveringResponse, this, _1, _2, _3));
}


// {"item_id":4,"config":{"name":"fump"}}
void CC_WindowCoveringImpl::handle_config_changed(JsonObjectPtr aParams)
{
  JsonObjectPtr o, vo;
  if (aParams->get("config", o)) {
    if (o->get("name", vo)) {
      changeName(vo->stringValue());
    }
  }
}

// {"item_id":4,"state":{"error-flags":null,"value":1},"error_flags":[]}
void CC_WindowCoveringImpl::handle_state_changed(JsonObjectPtr aParams)
{
  // Notes:
  // - while movement runs, optionally post intermediate updates to current values
  //   WindowCovering::Attributes::CurrentPositionLiftPercent100ths and
  //   WindowCovering::Attributes::CurrentPositionTiltPercent100ths
  // - when movement stops (early or completed):
  //   - FIRST set TargetPositionXXX attributes to current position
  //   - THEN set CurrentPositionXXX attributes to SAME current position
  //   (only this sequence makes the Window Covering Cluster recognize end of operation)

  BitMask<WindowCovering::Mode> mode;
  WindowCovering::Attributes::Mode::Get(endpointId(), &mode);
  JsonObjectPtr o, vo;
  if (aParams->get("state", o)) {
    if (o->get("value", vo)) {
      // sb: roughly something like this?
      // deviceP<DeviceWindowCovering>()->updateLift(vo->doubleValue(), UpdateMode(UpdateFlags::matter));
      // luz: Would be nice, maybe we can abstract later, but at this time there are too many
      //   variants how to handle this, so we directly access the attributes:
      // luz: Assumptions:
      // - this is the current position
      WindowCovering::Attributes::CurrentPositionLiftPercent100ths::Set(endpointId(), bridge2matter(vo->doubleValue(), mode.Has(WindowCovering::Mode::kMotorDirectionReversed)));
    }
    if (o->get("value-tilt", vo) && mHasTilt) {
      // roughly something like this?
      // deviceP<DeviceWindowCovering>()->updateTilt(vo->doubleValue(), UpdateMode(UpdateFlags::matter));
      // luz: Assumptions:
      // - this is the current position
      WindowCovering::Attributes::CurrentPositionTiltPercent100ths::Set(endpointId(), bridge2matter(vo->doubleValue(), mode.Has(WindowCovering::Mode::kMotorDirectionReversed)));
    }
  }
}

void CC_WindowCoveringImpl::windowCoveringResponse(int32_t aResponseId, ErrorPtr &aError, JsonObjectPtr aResultOrErrorData)
{
  DLOG(LOG_INFO, "got response for deviced.group_send_command: error=%s, result=%s", Error::text(aError), JsonObject::text(aResultOrErrorData));
}


#endif // CC_ADAPTERS
