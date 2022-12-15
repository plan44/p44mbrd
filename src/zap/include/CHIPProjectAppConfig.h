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

// overrides CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT in CHIPProjectConfig
// FIXME: the dynamic endpoint ids should be ever incrementing until 0xFFFF according to
//   specs, but current implementation
#define CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT 200

// FIXME: hard-code this because of hard-coded certs included through this
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_ID 0x8002

// FIXME: luz temp debug, remove again to reduce FOOTPRINT
// we want verbose error code display
#define CHIP_CONFIG_IM_STATUS_CODE_VERBOSE_FORMAT 1

// include the CHIPProjectConfig from config/standalone
#include <CHIPProjectConfig.h>
