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

#include "matter_common.h"

#include "devices/device.h"

// Current ZCL implementation of Struct uses a max-size array of 254 bytes
const int kDescriptorAttributeArraySize = 254;


// Device types for dynamic endpoints: TODO Need a generated file from ZAP to define these!
// (taken from third_party/zap/repo/zcl-builtin/matter/matter-devices.xml)
#define DEVICE_TYPE_MA_BRIDGED_DEVICE 0x0013
#define DEVICE_TYPE_MA_ON_OFF_LIGHT 0x0100
#define DEVICE_TYPE_MA_ON_OFF_PLUGIN_UNIT 0x010A
#define DEVICE_TYPE_MA_DIMMABLE_LIGHT 0x0101
#define DEVICE_TYPE_MA_DIMMABLE_PLUGIN_UNIT 0x010B
#define DEVICE_TYPE_MA_CT_LIGHT 0x010C
#define DEVICE_TYPE_MA_COLOR_LIGHT 0x010D
#define DEVICE_TYPE_MA_TEMP_SENSOR 0x0302
#define DEVICE_TYPE_MA_ILLUM_SENSOR 0x0106
#define DEVICE_TYPE_MA_OCCUPANCY_SENSOR 0x0107
#define DEVICE_TYPE_MA_RELATIVE_HUMIDITY_SENSOR 0x0307
#define DEVICE_TYPE_MA_CONTACT_SENSOR 0x0015
#define DEVICE_TYPE_MA_WINDOW_COVERING 0x0202
#define DEVICE_TYPE_MA_FAN_DEVICE 0x002B
#define DEVICE_TYPE_MA_GENERIC_SWITCH 0x000F

// Device Version for dynamic endpoints:
#define DEVICE_VERSION_DEFAULT 1
