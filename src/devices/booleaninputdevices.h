//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2022 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "device.h"

using namespace chip;
using namespace app;


// MARK: - BooleanInputDevice


class BooleanInputDevice : public InputDevice
{
  typedef InputDevice inherited;
public:

  BooleanInputDevice();

  virtual string description() override;

  virtual void initBridgedInfo(JsonObjectPtr aDeviceInfo, JsonObjectPtr aDeviceComponentInfo = nullptr, const char* aInputType = nullptr, const char* aInputId = nullptr) override;

  /// called to handle pushed properties coming from bridge
  virtual void handleBridgePushProperties(JsonObjectPtr aChangedProperties) override;

  /// handler for external attribute read access
  virtual EmberAfStatus HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength) override;

protected:

  /// Parse measured sensor value (according to InputDevice's mInputType and mInputId)
  /// from property container
  /// @param aProperties the root property container, either received via push or queried
  /// @param aUpdateMode what to update
  virtual void parseInputValue(JsonObjectPtr aProperties, UpdateMode aUpdateMode);

  uint8_t mState; ///< current state, NULL if currently not known

};


class ContactSensorDevice : public BooleanInputDevice
{
  typedef BooleanInputDevice inherited;
public:

  ContactSensorDevice();

  virtual const char *deviceType() override { return "contact sensor"; }

protected:

  /// called to have the final leaf class declare the correct device type list
  virtual void finalizeDeviceDeclaration() override;
};
