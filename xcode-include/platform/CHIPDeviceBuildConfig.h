// Generated by write_buildconfig_header.py
// From "//third_party/connectedhomeip/src/platform:gen_platform_buildconfig"

#ifndef PLATFORM_CHIPDEVICEBUILDCONFIG_H_
#define PLATFORM_CHIPDEVICEBUILDCONFIG_H_

#define CHIP_DEVICE_CONFIG_ENABLE_WPA 0
#define CHIP_ENABLE_OPENTHREAD 0
#define CHIP_DEVICE_CONFIG_THREAD_FTD 0
#define CHIP_WITH_GIO 0
#define OPENTHREAD_CONFIG_ENABLE_TOBLE 0

#define CHIP_STACK_LOCK_TRACKING_ENABLED 0 // FIXME: temp
//#define CHIP_STACK_LOCK_TRACKING_ENABLED 1
#define CHIP_STACK_LOCK_TRACKING_ERROR_FATAL 1

#define CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING 0
#define CHIP_DEVICE_CONFIG_RUN_AS_ROOT 0
#define CHIP_DISABLE_PLATFORM_KVS 0
#define CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE 0
#define CHIP_DEVICE_PROJECT_CONFIG_INCLUDE <CHIPProjectAppConfig.h>
#define CHIP_DEVICE_PLATFORM_CONFIG_INCLUDE <platform/Linux/CHIPDevicePlatformConfig.h>
#define CHIP_USE_TRANSITIONAL_COMMISSIONABLE_DATA_PROVIDER 0

// these we have in the XCode project for now
/*
#define P44_INTERNAL_XCODE_BUILD 1
#define CHIP_HAVE_CONFIG_H 1
#define CHIP_DEVICE_CONFIG_MAX_DISCOVERED_IP_ADDRESSES 10
#define ICD_MAX_NOTIFICATION_SUBSCRIBERS 1
#define CHIP_CONFIG_ENABLE_ICD_SERVER 0
#define CHIP_ADDRESS_RESOLVE_IMPL_INCLUDE_HEADER "<lib/address_resolve/AddressResolve_DefaultImpl.h>"
#define CHIP_USE_TRANSITIONAL_COMMISSIONABLE_DATA_PROVIDER 1
#define CHIP_MINMDNS_USE_EPHEMERAL_UNICAST_PORT 0
#define CHIP_DNSSD_DEFAULT_PLATFORM 1
#define CHIP_MINMDNS_HIGH_VERBOSITY 0
#define CHIP_LOG_FILTERING 1
#define DYNAMIC_ENDPOINT_COUNT 42
#define JSON_USE_EXCEPTION 0
#define JSON_USE_NULLREF 0
*/

//#define CHIP_DEVICE_LAYER_TARGET_LINUX 1
//#define CHIP_DEVICE_LAYER_TARGET Linux
#define CHIP_DEVICE_LAYER_TARGET_DARWIN 1
#define CHIP_DEVICE_LAYER_TARGET Darwin
#define TARGET_OS_MAC 1
#define TARGET_OS_OSX 1
#define DARWIN_STDOUT_LOG_DISABLE 1

#define CHIP_DEVICE_CONFIG_ENABLE_WIFI 0
#define CONFIG_NETWORK_LAYER_BLE 0



#endif  // PLATFORM_CHIPDEVICEBUILDCONFIG_H_
