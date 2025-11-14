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

/**
 *    @file
 *          Example project configuration file for CHIP.
 *
 *          This is a place to put application or project-specific overrides
 *          to the default configuration values for general CHIP features.
 *
 */

#pragma once

// overrides CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT in CHIPProjectConfig.h
#ifndef CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT
  #define CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT 200
#endif

// overrides CHIP_CONFIG_MAX_GROUP_ENDPOINTS_PER_FABRIC in CHIPConfig.
// Note:
// - this define is only used to be multiplied by 4 to determine CHIP_CONFIG_MAX_GROUPS_PER_FABRIC
// - this does not really make sense in a bridge with variable number of group clusters (one per EP with an output),
//   but we think 20 (=4*5) group endpoints per fabric could make sense
#ifndef CHIP_CONFIG_MAX_GROUP_ENDPOINTS_PER_FABRIC
  #define CHIP_CONFIG_MAX_GROUP_ENDPOINTS_PER_FABRIC 5 // default is 1
#endif


// This is a bridge, overrides CHIP_DEVICE_CONFIG_DEVICE_TYPE in CHIPDeviceConfig.h
#define CHIP_DEVICE_CONFIG_DEVICE_TYPE 0x000e

// this is the version relevant for OTA updates (and certification?)
// TODO: probably update at some point (now just setting SDK defaults again)
#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION 1
#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION_STRING "1.0"


// FIXME: luz temp debug, remove again to reduce FOOTPRINT
// we want verbose error code display
#define CHIP_CONFIG_IM_STATUS_CODE_VERBOSE_FORMAT 1

// include the CHIPProjectConfig from config/standalone
#include <CHIPProjectConfig.h>
