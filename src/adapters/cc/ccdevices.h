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

#pragma once

#include "adapters/adapters.h"

#if CC_ADAPTERS

#include "jsonrpccomm.hpp"

#include "deviceonoff.h"
#include "devicelevelcontrol.h"
#include "devicecolorcontrol.h"
#include "devicewindowcovering.h"
#include "sensordevices.h"
#include "booleaninputdevices.h"


// MARK: - Generic device implementation classes

/// @brief base class for devices implemented via the CC API
class CC_DeviceImpl :
  public DeviceAdapter, // common device adapter
  public DeviceInfoDelegate // the CC side delegate implementations
{
  /// @name device properties that are immutable after instantiation
  /// @{

  // TODO: usually things like model, vendor, UUID etc.

  /// @}

  /// @name device properties that can change after instantiation
  /// @{

  string mName; ///< current CC side name of the device

  /// @}

public:

  CC_DeviceImpl(int item_id);

  /// class method to create UID string from item_id
  static string uid_string(int item_id);

  /// @name DeviceInfoDelegate
  /// @{
  virtual const string endpointUID() const override;

  virtual void deviceDidGetInstalled() override;

  virtual bool isReachable() const override;

  virtual string name() const override;
  virtual bool changeName(const string aNewName) override;

  /// @}

  /// @name CC bridge API specific methods
  /// @{

  /// @return `CC_DeviceImpl` pointer from DevicePtr
  inline static CC_DeviceImpl* impl(DevicePtr aDevice) { return static_cast<CC_DeviceImpl*>(&(aDevice->deviceInfoDelegate())); } // we *know* the delegate *is* a CC_DeviceImpl

  int item_id;
  int get_item_id ();
  bool mFeedback;
  bool mUnresponsive;

  void initialize_name(const string _name) { mName = _name; }
  void initialize_feedback(const bool _feedback) { mFeedback = _feedback; }
  void initialize_unresponsive() { mUnresponsive = false; }

  virtual void handle_config_changed(JsonObjectPtr aParams) { /* NOP in base class */ }
  virtual void handle_state_changed(JsonObjectPtr aParams) { /* NOP in base class */ }

  virtual void updateBridgedInfo(JsonObjectPtr aDeviceInfo);

  /// @}

};



class CC_IdentifiableImpl : public CC_DeviceImpl, public IdentifyDelegate
{
  typedef CC_DeviceImpl inherited;

protected:

  CC_IdentifiableImpl(int _item_id) : inherited(_item_id) { /* NOP so far */ }

  /// @name IdentifyDelegate
  /// @{
  virtual void identify(int aDurationS) override;
  virtual Identify::IdentifyTypeEnum identifyType() override;
  /// @}

private:
  void onIdentifyResponse(int32_t aResponseId, ErrorPtr &aError, JsonObjectPtr aResultOrErrorData);

};


// MARK: output devices

class CC_OnOffImpl : public CC_IdentifiableImpl, public OnOffDelegate
{
  typedef CC_IdentifiableImpl inherited;

protected:

  CC_OnOffImpl(int _item_id) : inherited(_item_id) { /* NOP so far */ }

  /// @name OnOffDelegate
  /// @{
  virtual void setOnOffState(bool aOn) override;
  /// @}
  ///

  virtual void handle_config_changed(JsonObjectPtr aParams) override;
  virtual void handle_state_changed(JsonObjectPtr aParams) override;

private:

  void onOffResponse(int32_t aResponseId, ErrorPtr &aError, JsonObjectPtr aResultOrErrorData);


};


class CC_LevelControlImpl : public CC_OnOffImpl, public LevelControlDelegate
{
  typedef CC_OnOffImpl inherited;

protected:
  CC_LevelControlImpl(int _item_id) : inherited(_item_id) { /* NOP so far */ }

  /// the hardware recommended transition time (usually provided by the bridged hardware)
  uint16_t mRecommendedTransitionTimeDS;
  MLMicroSeconds mEndOfLatestTransition;

  /// @name LevelControlDelegate
  /// @{
  virtual void setLevel(double aNewLevel, uint16_t aTransitionTimeDS) override;
  virtual void dim(int8_t aDirection, uint8_t aRate) override;
  virtual MLMicroSeconds endOfLatestTransition() override { return mEndOfLatestTransition; };
  /// @}

  virtual void handle_config_changed(JsonObjectPtr aParams) override;
  virtual void handle_state_changed(JsonObjectPtr aParams) override;

private:
  void levelControlResponse(int32_t aResponseId, ErrorPtr &aError, JsonObjectPtr aResultOrErrorData);
};


class CC_WindowCoveringImpl : public CC_IdentifiableImpl, public WindowCoveringDelegate
{
  typedef CC_IdentifiableImpl inherited;

  WindowCovering::Type mType;
  WindowCovering::EndProductType mEndProductType;
  bool mHasTilt;
  bool mInverted;

protected:

  CC_WindowCoveringImpl(int _item_id, WindowCovering::Type _type, WindowCovering::EndProductType _end_product_type);

  /// @name DeviceInfoDelegate
  /// @{
  virtual void deviceDidGetInstalled() override;
  /// @}

  /// @name WindowCoveringDelegate
  /// @{
  virtual void startMovement(WindowCovering::WindowCoveringType aMovementType) override;
  virtual void simpleStartMovement(WindowCovering::WindowCoveringType type, bool aUpOrOpen) override;
  virtual void stopMovement() override;
  /// @}

  /// @name IdentifyDelegate
  /// @{
  virtual Identify::IdentifyTypeEnum identifyType() override { return Identify::IdentifyTypeEnum::kActuator; }
  /// @}

  virtual void handle_config_changed(JsonObjectPtr aParams) override;
  virtual void handle_state_changed(JsonObjectPtr aParams) override;

private:
  void windowCoveringResponse(int32_t aResponseId, ErrorPtr &aError, JsonObjectPtr aResultOrErrorData);

public:
  CC_WindowCoveringImpl();

};




// MARK: - Final device classes - required implementation interfaces plus device itself in one object


/// @brief CC composed device (no functionality of its own, just container with device info
class CC_ComposedDevice final :
  public ComposedDevice, // the matter side device
  public CC_DeviceImpl // the CC side delegate implementation
{
  typedef CC_DeviceImpl inherited;

public:
  CC_ComposedDevice(int _item_id) : ComposedDevice(DG(DeviceInfo)), inherited(_item_id) {}; // this class itself implements all needed delegates
  DEVICE_ACCESSOR;
};


// MARK: Outputs

class CC_OnOffLightDevice final :
  public DeviceOnOffLight, // the matter side device
  public CC_OnOffImpl // the CC side delegate implementation
{
  typedef CC_OnOffImpl inherited;
public:
  CC_OnOffLightDevice(int _item_id) :
      DeviceOnOffLight(DG(OnOff), DGP(Identify), DG(DeviceInfo)),
      inherited(_item_id)
  {}; // this class itself implements all needed delegates
  DEVICE_ACCESSOR;
};


class CC_OnOffPluginUnitDevice final :
  public DeviceOnOffPluginUnit, // the matter side device
  public CC_OnOffImpl // the CC side delegate implementation
{
  typedef CC_OnOffImpl inherited;
public:
  CC_OnOffPluginUnitDevice(int _item_id) :
      DeviceOnOffPluginUnit(DG(OnOff), DGP(Identify), DG(DeviceInfo)),
      inherited(_item_id)
  { }; // this class itself implements all needed delegates
  DEVICE_ACCESSOR;
};


class CC_DimmableLightDevice final :
  public DeviceDimmableLight, // the matter side device
  public CC_LevelControlImpl  // the CC side delegate implementation
{
  typedef CC_LevelControlImpl inherited;
public:
  CC_DimmableLightDevice(int _item_id) :
      DeviceDimmableLight(DG(LevelControl), DG(OnOff),
                          DGP(Identify), DG(DeviceInfo)),
      inherited(_item_id)
  {}; // this class itself implements all needed delegates
  virtual Identify::IdentifyTypeEnum identifyType() override { return Identify::IdentifyTypeEnum::kLightOutput; }
  DEVICE_ACCESSOR;
};


class CC_DimmablePluginUnitDevice final :
  public DeviceDimmablePluginUnit, // the matter side device
  public CC_LevelControlImpl // the CC side delegate implementation
{
  typedef CC_LevelControlImpl inherited;
public:
  CC_DimmablePluginUnitDevice(int _item_id) :
      DeviceDimmablePluginUnit(DG(LevelControl), DG(OnOff),
                               DGP(Identify), DG(DeviceInfo)),
      inherited(_item_id)
  {}; // this class itself implements all needed delegates
  virtual Identify::IdentifyTypeEnum identifyType() override { return Identify::IdentifyTypeEnum::kActuator; }
  DEVICE_ACCESSOR;
};


class CC_WindowCoveringDevice final :
  public DeviceWindowCovering, // the matter side device
  public CC_WindowCoveringImpl // the CC side delegate implementation
{
  typedef CC_WindowCoveringImpl inherited;
public:
public:
  CC_WindowCoveringDevice(int _item_id, WindowCovering::Type _type, WindowCovering::EndProductType _end_product_type) :
      DeviceWindowCovering(DG(WindowCovering), DGP(Identify), DG(DeviceInfo)),
      inherited(_item_id, _type, _end_product_type)
  {}; // this class itself implements all needed delegates
  DEVICE_ACCESSOR;
};




#endif // CC_ADAPTERS

