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

// MARK: - bridge API constants

/// @note: this is an excerpt from p44vdc/vdc_common/dsdefs.h

/// color/class
typedef enum {
  class_undefined = 0,
  class_yellow_light = 1,
  class_grey_shadow = 2,
  class_blue_climate = 3,
  class_cyan_audio = 4,
  class_magenta_video = 5,
  class_red_security = 6,
  class_green_access = 7,
  class_black_joker = 8,
  class_white_singledevices = 9,
  numColorClasses = 10,
} DsClass;

/// color/group
typedef enum {
  group_undefined = 0, ///< formerly "variable", but now 8 is called "variable"
  group_yellow_light = 1,
  group_grey_shadow = 2,
  group_blue_heating = 3, ///< heating - formerly "climate"
  group_cyan_audio = 4,
  group_magenta_video = 5,
  group_red_security = 6,  ///< no group!
  group_green_access = 7,  ///< no group!
  group_black_variable = 8,
  group_blue_cooling = 9, ///< cooling - formerly just "white" (is it still white? snow?)
  group_blue_ventilation = 10, ///< ventilation - formerly "display"?!
  group_blue_windows = 11, ///< windows (not the OS, holes in the wall..)
  group_blue_air_recirculation = 12, ///< air recirculation for fan coil units
  group_roomtemperature_control = 48, ///< room temperature control
  group_ventilation_control = 49, ///< room ventilation control
} DsGroup;

typedef uint64_t DsGroupMask; ///< 64 bit mask, Bit0 = group 0, Bit63 = group 63


/// button click types
typedef enum {
  ct_tip_1x = 0, ///< first tip
  ct_tip_2x = 1, ///< second tip
  ct_tip_3x = 2, ///< third tip
  ct_tip_4x = 3, ///< fourth tip
  ct_hold_start = 4, ///< hold start
  ct_hold_repeat = 5, ///< hold repeat
  ct_hold_end = 6, ///< hold end
  ct_click_1x = 7, ///< short click
  ct_click_2x = 8, ///< double click
  ct_click_3x = 9, ///< triple click
  ct_short_long = 10, ///< short/long = programming mode
  ct_local_off = 11, ///< local button has turned device off
  ct_local_on = 12, ///< local button has turned device on
  ct_short_short_long = 13, ///< short/short/long = local programming mode
  ct_local_stop = 14, ///< local stop
  ct_progress = 128, ///< extra progress event, not sent to dS, only bridges
  ct_complete = 129, ///< extra end-of-click-sequence event, not sent to dS, only bridges
  ct_none = 255 ///< no click (for state)
} DsClickType;


/// button function aka "LTNUM" (lower 4 bits in LTNUMGRP0)
typedef enum {
  // all colored buttons
  buttonFunc_device = 0, ///< device button (and preset 2-4)
  buttonFunc_area1_preset0x = 1, ///< area1 button (and preset 2-4)
  buttonFunc_area2_preset0x = 2, ///< area2 button (and preset 2-4)
  buttonFunc_area3_preset0x = 3, ///< area3 button (and preset 2-4)
  buttonFunc_area4_preset0x = 4, ///< area4 button (and preset 2-4)
  buttonFunc_room_preset0x = 5, ///< room button (and preset 1-4)
  buttonFunc_room_preset1x = 6, ///< room button (and preset 10-14)
  buttonFunc_room_preset2x = 7, ///< room button (and preset 20-24)
  buttonFunc_room_preset3x = 8, ///< room button (and preset 30-34)
  buttonFunc_room_preset4x = 9, ///< room button (and preset 40-44)
  buttonFunc_area1_preset1x = 10, ///< area1 button (and preset 12-14)
  buttonFunc_area2_preset2x = 11, ///< area2 button (and preset 22-24)
  buttonFunc_area3_preset3x = 12, ///< area3 button (and preset 32-34)
  buttonFunc_area4_preset4x = 13, ///< area4 button (and preset 42-44)
  // black buttons
  buttonFunc_alarm = 1, ///< alarm
  buttonFunc_panic = 2, ///< panic
  buttonFunc_leave = 3, ///< leaving home
  buttonFunc_doorbell = 5, ///< door bell
  buttonFunc_apartment = 14, ///< appartment button
  buttonFunc_app = 15, ///< application specific button
} DsButtonFunc;


/// output channel types
typedef enum {
  channeltype_default = 0, ///< default channel (main output value, e.g. brightness for lights)
  channeltype_brightness = 1, ///< brightness for lights
  channeltype_hue = 2, ///< hue for color lights
  channeltype_saturation = 3, ///< saturation for color lights
  channeltype_colortemp = 4, ///< color temperature for lights with variable white point
  channeltype_cie_x = 5, ///< X in CIE Color Model for color lights
  channeltype_cie_y = 6, ///< Y in CIE Color Model for color lights
  channeltype_shade_position_outside = 7, ///< shade position outside (blinds)
  channeltype_shade_position_inside = 8, ///< shade position inside (curtains)
  channeltype_shade_angle_outside = 9, ///< shade opening angle outside (blinds)
  channeltype_shade_angle_inside = 10, ///< shade opening angle inside (curtains)
  channeltype_permeability = 11, ///< permeability (smart glass)
  channeltype_airflow_intensity = 12, ///< airflow intensity channel
  channeltype_airflow_direction = 13, ///< airflow direction (DsVentilationDirectionState)
  channeltype_airflow_flap_position = 14, ///< airflow flap position (angle), 0..100 of device's available range
  channeltype_airflow_louver_position = 15, ///< louver position (angle), 0..100 of device's available range
  channeltype_heating_power = 16, ///< power level for simple heating or cooling device (such as valve)
  channeltype_cooling_capacity = 17, ///< cooling capacity
  channeltype_audio_volume = 18, /// audio volume
  channeltype_power_state = 19, ///< FCU custom channel: power state
  channeltype_airflow_louver_auto = 20, ///< louver automatic mode (0=off, >0=on)
  channeltype_airflow_intensity_auto = 21, ///< airflow intensity automatic mode (0=off, >0=on)
  channeltype_water_temperature = 22, ///< water temperature
  channeltype_water_flow = 23, ///< water flow rate
  channeltype_power_level = 24, ///< power level
  channeltype_video_station = 25, ///< video tv station (channel number)
  channeltype_video_input_source = 26, ///< video input source (TV, HDMI etc.)

  channeltype_custom_first = 192, ///< first device-specific channel
  channeltype_custom_last = 239, ///< last device-specific channel

  channeltype_fcu_operation_mode = channeltype_custom_first+0, ///< FCU custom channel: operating mode

  channeltype_p44_position_v = channeltype_custom_first+0, ///< vertical position (e.g for moving lights)
  channeltype_p44_position_h = channeltype_custom_first+1, ///< horizontal position (e.g for moving lights)
  channeltype_p44_zoom_v = channeltype_custom_first+2, ///< vertical zoom (for extended functionality moving lights)
  channeltype_p44_zoom_h = channeltype_custom_first+3, ///< horizontal zoom (for extended functionality moving lights)
  channeltype_p44_rotation = channeltype_custom_first+4, ///< rotation (for extended functionality moving lights)
  channeltype_p44_brightness_gradient = channeltype_custom_first+5, ///< gradient for brightness
  channeltype_p44_hue_gradient = channeltype_custom_first+6, ///< gradient for hue
  channeltype_p44_saturation_gradient = channeltype_custom_first+7, ///< gradient for saturation
  channeltype_p44_feature_mode = channeltype_custom_first+8, ///< feature mode

  channeltype_p44_audio_content_source = channeltype_custom_first+22, ///< audio content source // FIXME: p44-specific channel type for audio content source until dS specifies one

  numChannelTypes = 240 // 0..239 are channel types
} DsChannelTypeEnum;
typedef uint8_t DsChannelType;


/// Power state channel values (audio, FCU...)
typedef enum {
  powerState_off = 0, ///< "normal" off (no standby, just off)
  powerState_on = 1, ///< "normal" on
  powerState_forcedOff = 2, ///< also "local off" (climate: turned off locally/by user e.g. to silence a device, but will turn on when global building protection requires it)
  powerState_standby = 3, ///< also "power save" (audio: off, but ready for quick start)
  numDsPowerStates
} DsPowerState;


/// ventilation airflow direction channel states
typedef enum {
  dsVentilationDirection_undefined = 0,
  dsVentilationDirection_supply_or_down = 1,
  dsVentilationDirection_exhaust_or_up = 2,
  numDsVentilationDirectionStates
} DsVentilationAirflowDirection;




/// binary input types (sensor functions)
typedef enum {
  binInpType_none = 0, ///< no system function
  binInpType_presence = 1, ///< Presence
  binInpType_light = 2, ///< Light
  binInpType_presenceInDarkness = 3, ///< Presence in darkness
  binInpType_twilight = 4, ///< twilight
  binInpType_motion = 5, ///< motion
  binInpType_motionInDarkness = 6, ///< motion in darkness
  binInpType_smoke = 7, ///< smoke
  binInpType_wind = 8, ///< wind
  binInpType_rain = 9, ///< rain
  binInpType_sun = 10, ///< solar radiation (sun light above threshold)
  binInpType_thermostat = 11, ///< thermostat (temperature below user-adjusted threshold)
  binInpType_lowBattery = 12, ///< device has low battery
  binInpType_windowClosed = 13, ///< window is closed
  binInpType_doorClosed = 14, ///< door is closed
  binInpType_windowHandle = 15, ///< TRI-STATE! Window handle, has extendedValue showing closed/open/tilted, bool value is just closed/open
  binInpType_garageDoorOpen = 16, ///< garage door is open
  binInpType_sunProtection = 17, ///< protect against too much sunlight
  binInpType_frost = 18, ///< frost detector
  binInpType_heatingActivated = 19, ///< heating system activated
  binInpType_heatingChangeOver = 20, ///< heating system change over (active=warm water, non active=cold water)
  binInpType_initStatus = 21, ///< can indicate when not all functions are ready yet
  binInpType_malfunction = 22, ///< malfunction, device needs maintainance, cannot operate
  binInpType_service = 23, ///< device needs service, but can still operate normally at the moment
} DsBinaryInputType;



/// sensor types
/// @note these are used in numeric enum form in sensorDescriptions[].sensorType since vDC API 1.0
///   but are not 1:1 mapped to dS sensor types (dS sensor types are constructed from these + VdcUsageHint)
typedef enum {
  sensorType_none = 0,
  // physical double values
  sensorType_temperature = 1, ///< temperature in degrees celsius
  sensorType_humidity = 2, ///< relative humidity in %
  sensorType_illumination = 3, ///< illumination in lux
  sensorType_supplyVoltage = 4, ///< supply voltage level in Volts
  sensorType_gas_CO = 5, ///< CO (carbon monoxide) concentration in ppm
  sensorType_gas_radon = 6, ///< Radon activity in Bq/m3
  sensorType_gas_type = 7, ///< gas type sensor
  sensorType_dust_PM10 = 8, ///< particles <10µm in μg/m3
  sensorType_dust_PM2_5 = 9, ///< particles <2.5µm in μg/m3
  sensorType_dust_PM1 = 10, ///< particles <1µm in μg/m3
  sensorType_set_point = 11, ///< room operating panel set point, 0..1
  sensorType_fan_speed = 12, ///< fan speed, 0..1 (0=off, <0=auto)
  sensorType_wind_speed = 13, ///< wind speed in m/s
  sensorType_power = 14, ///< Power in W
  sensorType_current = 15, ///< Electric current in A
  sensorType_energy = 16, ///< Energy in kWh
  sensorType_apparent_power = 17, ///< Apparent electric power in VA
  sensorType_air_pressure = 18, ///< Air pressure in hPa
  sensorType_wind_direction = 19, ///< Wind direction in degrees
  sensorType_sound_volume = 20, ///< Sound pressure level in dB
  sensorType_precipitation = 21, ///< Precipitation in mm/m2
  sensorType_gas_CO2 = 22, ///< CO2 (carbon dioxide) concentration in ppm
  sensorType_gust_speed = 23, ///< gust speed in m/S
  sensorType_gust_direction = 24, ///< gust direction in degrees
  sensorType_generated_power = 25, ///< Generated power in W
  sensorType_generated_energy = 26, ///< Generated energy in kWh
  sensorType_water_quantity = 27, ///< Water quantity in liters
  sensorType_water_flowrate = 28, ///< Water flow rate in liters/minute
  sensorType_length = 29, ///< Length in meters
  sensorType_mass = 30, ///< mass in grams
  sensorType_duration = 31, ///< time in seconds
  numVdcSensorTypes
} VdcSensorType;


/// technical value types
/// @note these are used to describe single device properties and parameter values, along with VdcSiUnit
typedef enum {
  valueType_unknown,
  valueType_numeric,
  valueType_integer,
  valueType_boolean,
  valueType_enumeration,
  valueType_string,
  numValueTypes
} VdcValueType;


/// Scene Effects (transition and alerting)
typedef enum {
  scene_effect_none = 0, ///< no effect, immediate transition
  scene_effect_smooth = 1, ///< smooth (default: 100mS) normal transition
  scene_effect_slow = 2, ///< slow (default: 1min) transition
  scene_effect_custom = 3, ///< custom (default: 5sec) transition
  scene_effect_alert = 4, ///< blink (for light devices, effectParam!=0 allows detail control) / alerting (in general: an effect that draws the user’s attention)
  scene_effect_transition = 5, ///< transition time according to scene-level effectParam (milliseconds)
  scene_effect_script = 6, ///< run scene script
} VdcSceneEffect;


/// Dim mode
typedef enum {
  dimmode_down = -1,
  dimmode_stop = 0,
  dimmode_up = 1
} VdcDimMode;


/// button types (for buttonDescriptions[].buttonType)
typedef enum {
  buttonType_undefined = 0, ///< kind of button not defined by device hardware
  buttonType_single = 1, ///< single pushbutton
  buttonType_2way = 2, ///< two-way pushbutton or rocker
  buttonType_4way = 3, ///< 4-way navigation button
  buttonType_4wayWithCenter = 4, ///< 4-way navigation with center button
  buttonType_8wayWithCenter = 5, ///< 8-way navigation with center button
  buttonType_onOffSwitch = 6, ///< On-Off switch
} VdcButtonType;


/// button element IDs (for buttonDescriptions[].buttonElementID)
typedef enum {
  buttonElement_center = 0, ///< center element / single button
  buttonElement_down = 1, ///< down, for 2,4,8-way
  buttonElement_up = 2, ///< up, for 2,4,8-way
  buttonElement_left = 3, ///< left, for 2,4,8-way
  buttonElement_right = 4, ///< right, for 2,4,8-way
  buttonElement_upperLeft = 5, ///< upper left, for 8-way
  buttonElement_lowerLeft = 6, ///< lower left, for 8-way
  buttonElement_upperRight = 7, ///< upper right, for 8-way
  buttonElement_lowerRight = 8, ///< lower right, for 8-way
} VdcButtonElement;



/// direct scene call action mode for buttons
typedef enum {
  buttonActionMode_normal = 0, ///< normal scene call
  buttonActionMode_force = 1, ///< forced scene call
  buttonActionMode_undo = 2, ///< undo scene
  buttonActionMode_none = 255, ///< no action
} VdcButtonActionMode;


/// output functions (describes capability of output)
typedef enum {
  outputFunction_switch = 0, ///< switch output - single channel 0..100
  outputFunction_dimmer = 1, ///< effective value dimmer - single channel 0..100
  outputFunction_positional = 2, ///< positional (servo, unipolar valves, blinds - single channel 0..n, usually n=100)
  outputFunction_ctdimmer = 3, ///< dimmer with color temperature - channels 1 and 4
  outputFunction_colordimmer = 4, ///< full color dimmer - channels 1..6
  outputFunction_bipolar_positional = 5, ///< bipolar valves, dual direction fan control etc. - single channel -n...0...n, usually n=100
  outputFunction_internallyControlled = 6, ///< output values(s) mostly internally controlled, e.g. FCU
  outputFunction_custom = 0x7F ///< custom output/channel configuration, none of the well-known functions above
} VdcOutputFunction;

/// output modes
typedef enum {
  outputmode_disabled = 0, ///< disabled
  outputmode_binary = 1, ///< binary ON/OFF mode
  outputmode_gradual = 2, ///< gradual output value (dimmer, valve, positional etc.)
  outputmode_default = 0x7F ///< use device in its default (or only) mode, without further specification
} VdcOutputMode;


/// heatingSystemCapability modes
typedef enum {
  hscapability_heatingOnly = 1, ///< only positive "heatingLevel" will be applied to the output
  hscapability_coolingOnly = 2, ///< only negative "heatingLevel" will be applied as positive values to the output
  hscapability_heatingAndCooling = 3 ///< absolute value of "heatingLevel" will be applied to the output
} VdcHeatingSystemCapability;


typedef enum {
  hstype_unknown = 0,
  hstype_floor = 1,
  hstype_radiator = 2,
  hstype_wall = 3,
  hstype_convectorPassive = 4,
  hstype_convectorActive = 5,
  hstype_floorLowEnergy = 6
} VdcHeatingSystemType;


/// hardware error status
typedef enum {
  hardwareError_none = 0, ///< hardware is ok
  hardwareError_openCircuit = 1, ///< input or output open circuit  (eg. bulb burnt)
  hardwareError_shortCircuit = 2, ///< input or output short circuit
  hardwareError_overload = 3, ///< output overload, including mechanical overload (e.g. heating valve actuator obstructed)
  hardwareError_busConnection = 4, ///< third party device bus problem (such as DALI short-circuit)
  hardwareError_lowBattery = 5, ///< third party device has low battery
  hardwareError_deviceError = 6, ///< other device error
} VdcHardwareError;


/// usage hints for inputs and outputs
typedef enum {
  usage_undefined = 0, ///< usage not defined
  usage_room = 1, ///< room related (e.g. indoor sensors and controllers)
  usage_outdoors = 2, ///< outdoors related (e.g. outdoor sensors)
  usage_user = 3, ///< user interaction (e.g. indicators, displays, dials, sliders)
  usage_total = 4, ///< total
  usage_lastrun = 5, ///< last run (of an activity like a washing machine program)
  usage_average = 6, ///< average (per run activity)
} VdcUsageHint;


/// @}
