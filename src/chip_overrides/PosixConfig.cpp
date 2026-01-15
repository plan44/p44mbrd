/*
 *
 *    Copyright (c) 2020-2022 Project CHIP Authors
 *    Copyright (c) 2019-2020 Google LLC.
 *    Copyright (c) 2018 Nest Labs, Inc.
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
 *          Utilities for interacting with multiple file partitions and maps
 *          key-value config calls to the correct partition.
 */

#include <platform/internal/CHIPDeviceLayerInternal.h>
#include <platform/internal/testing/ConfigUnitTest.h>

#include <lib/core/CHIPEncoding.h>
#include <lib/support/CodeUtils.h>
#if !CHIP_CUSTOM_POSIX_CONFIG
  #error "This custom implementation of PosixConfig.cpp" cannot be used without CHIP_CUSTOM_POSIX_CONFIG
#else
  #include "PosixConfig.h" // local override
#endif

#include <platform/KeyValueStoreManager.h>

// P44 specific
#include "chip_glue/p44deviceinstanceinfoprovider.h" // information about vendor, name, serial, URL etc.
#include "chip_glue/p44deviceattestationprovider.h" // information about attestation information

namespace chip {
namespace DeviceLayer {
namespace Internal {

// *** CAUTION ***: Changing the names or namespaces of these values will *break* existing devices.

// NVS namespaces used to store device configuration information.
const char PosixConfig::kConfigNamespace_ChipFactory[]  = "chip-factory";
const char PosixConfig::kConfigNamespace_ChipConfig[]   = "chip-config";
const char PosixConfig::kConfigNamespace_ChipCounters[] = "chip-counters";

// Keys stored in the Chip-factory namespace
const PosixConfig::Key PosixConfig::kConfigKey_SerialNum             = { kConfigNamespace_ChipFactory, "serial-num" };
const PosixConfig::Key PosixConfig::kConfigKey_MfrDeviceId           = { kConfigNamespace_ChipFactory, "device-id" };
const PosixConfig::Key PosixConfig::kConfigKey_MfrDeviceCert         = { kConfigNamespace_ChipFactory, "device-cert" };
const PosixConfig::Key PosixConfig::kConfigKey_MfrDeviceICACerts     = { kConfigNamespace_ChipFactory, "device-ca-certs" };
const PosixConfig::Key PosixConfig::kConfigKey_MfrDevicePrivateKey   = { kConfigNamespace_ChipFactory, "device-key" };
const PosixConfig::Key PosixConfig::kConfigKey_HardwareVersion       = { kConfigNamespace_ChipFactory, "hardware-ver" };
const PosixConfig::Key PosixConfig::kConfigKey_ManufacturingDate     = { kConfigNamespace_ChipFactory, "mfg-date" };
const PosixConfig::Key PosixConfig::kConfigKey_SetupPinCode          = { kConfigNamespace_ChipFactory, "pin-code" };
const PosixConfig::Key PosixConfig::kConfigKey_SetupDiscriminator    = { kConfigNamespace_ChipFactory, "discriminator" };
const PosixConfig::Key PosixConfig::kConfigKey_Spake2pIterationCount = { kConfigNamespace_ChipFactory, "iteration-count" };
const PosixConfig::Key PosixConfig::kConfigKey_Spake2pSalt           = { kConfigNamespace_ChipFactory, "salt" };
const PosixConfig::Key PosixConfig::kConfigKey_Spake2pVerifier       = { kConfigNamespace_ChipFactory, "verifier" };
const PosixConfig::Key PosixConfig::kConfigKey_VendorId              = { kConfigNamespace_ChipFactory, "vendor-id" };
const PosixConfig::Key PosixConfig::kConfigKey_ProductId             = { kConfigNamespace_ChipFactory, "product-id" };

// Keys stored in the Chip-config namespace
const PosixConfig::Key PosixConfig::kConfigKey_ServiceConfig      = { kConfigNamespace_ChipConfig, "service-config" };
const PosixConfig::Key PosixConfig::kConfigKey_PairedAccountId    = { kConfigNamespace_ChipConfig, "account-id" };
const PosixConfig::Key PosixConfig::kConfigKey_ServiceId          = { kConfigNamespace_ChipConfig, "service-id" };
const PosixConfig::Key PosixConfig::kConfigKey_LastUsedEpochKeyId = { kConfigNamespace_ChipConfig, "last-ek-id" };
const PosixConfig::Key PosixConfig::kConfigKey_FailSafeArmed      = { kConfigNamespace_ChipConfig, "fail-safe-armed" };
const PosixConfig::Key PosixConfig::kConfigKey_RegulatoryLocation = { kConfigNamespace_ChipConfig, "regulatory-location" };
const PosixConfig::Key PosixConfig::kConfigKey_CountryCode        = { kConfigNamespace_ChipConfig, "country-code" };
const PosixConfig::Key PosixConfig::kConfigKey_LocationCapability = { kConfigNamespace_ChipConfig, "location-capability" };
const PosixConfig::Key PosixConfig::kConfigKey_UniqueId           = { kConfigNamespace_ChipConfig, "unique-id" };

// Keys stored in the Chip-counters namespace
const PosixConfig::Key PosixConfig::kCounterKey_RebootCount           = { kConfigNamespace_ChipCounters, "reboot-count" };
const PosixConfig::Key PosixConfig::kCounterKey_UpTime                = { kConfigNamespace_ChipCounters, "up-time" };
const PosixConfig::Key PosixConfig::kCounterKey_BootReason            = { kConfigNamespace_ChipCounters, "boot-reason" };
const PosixConfig::Key PosixConfig::kCounterKey_TotalOperationalHours = { kConfigNamespace_ChipCounters, "total-operational-hours" };

// MARK: - typed wrappers

CHIP_ERROR PosixConfig::ReadConfigValue(Key key, bool & val)
{
  size_t outLen = 0;
  return ReadConfigValueBin(key, reinterpret_cast<uint8_t *>(&val), sizeof(val), outLen);
}


CHIP_ERROR PosixConfig::ReadConfigValue(Key key, uint16_t & val)
{
  size_t outLen = 0;
  return ReadConfigValueBin(key, reinterpret_cast<uint8_t *>(&val), sizeof(val), outLen);
}


CHIP_ERROR PosixConfig::ReadConfigValue(Key key, uint32_t & val)
{
  size_t outLen = 0;
  return ReadConfigValueBin(key, reinterpret_cast<uint8_t *>(&val), sizeof(val), outLen);
}


CHIP_ERROR PosixConfig::ReadConfigValue(Key key, uint64_t & val)
{
  size_t outLen = 0;
  return ReadConfigValueBin(key, reinterpret_cast<uint8_t *>(&val), sizeof(val), outLen);
}


CHIP_ERROR PosixConfig::ReadConfigValueStr(Key key, char * buf, size_t bufSize, size_t & outLen)
{
  return ReadConfigValueBin(key, reinterpret_cast<uint8_t *>(buf), bufSize, outLen);
}



CHIP_ERROR PosixConfig::WriteConfigValue(Key key, bool val)
{
  return WriteConfigValueBin(key, reinterpret_cast<uint8_t *>(&val), sizeof(val));
}


CHIP_ERROR PosixConfig::WriteConfigValue(Key key, uint16_t val)
{
  return WriteConfigValueBin(key, reinterpret_cast<uint8_t *>(&val), sizeof(val));
}


CHIP_ERROR PosixConfig::WriteConfigValue(Key key, uint32_t val)
{
  return WriteConfigValueBin(key, reinterpret_cast<uint8_t *>(&val), sizeof(val));
}


CHIP_ERROR PosixConfig::WriteConfigValue(Key key, uint64_t val)
{
  return WriteConfigValueBin(key, reinterpret_cast<uint8_t *>(&val), sizeof(val));
}


CHIP_ERROR PosixConfig::WriteConfigValueStr(Key key, const char * str)
{
  return WriteConfigValueBin(key, reinterpret_cast<const uint8_t *>(str), strlen(str));
}


CHIP_ERROR PosixConfig::WriteConfigValueStr(Key key, const char * str, size_t strLen)
{
  return WriteConfigValueBin(key, reinterpret_cast<const uint8_t *>(str), strLen);
}


// MARK: - factory values

CHIP_ERROR PosixConfig::ReadFactoryValueBin(const char* key, uint8_t* buf, size_t bufSize, size_t& outLen)
{
  DeviceAttestationCredentialsProvider* dap = Credentials::GetDeviceAttestationCredentialsProvider();
  assert(dap);
  DeviceInstanceInfoProvider* dip = DeviceLayer::GetDeviceInstanceInfoProvider();
  assert(dip);
  if (uequals(key, kConfigKey_SerialNum.Name)) {
    return dip->GetSerialNumber((char*)buf, bufSize);
  }
  if (uequals(key, kConfigKey_HardwareVersion.Name)) {
    outLen = 2;
    if (bufSize<outLen) return CHIP_ERROR_BUFFER_TOO_SMALL;
    return dip->GetHardwareVersion(*((uint16_t*)buf));
  }
  if (uequals(key, kConfigKey_VendorId.Name)) {
    outLen = 2;
    if (bufSize<outLen) return CHIP_ERROR_BUFFER_TOO_SMALL;
    return dip->GetVendorId(*((uint16_t*)buf));
  }
  if (uequals(key, kConfigKey_ProductId.Name)) {
    outLen = 2;
    if (bufSize<outLen) return CHIP_ERROR_BUFFER_TOO_SMALL;
    return dip->GetProductId(*((uint16_t*)buf));
  }

// We do not need these because we provide them via our own attestation provider
//  if (uequals(key, kConfigKey_ManufacturingDate.Name)) return dip->GetManufacturingDate((char*)buf, bufSize);
//  if (uequals(key, kConfigKey_SetupPinCode.Name)) return dip->Getpin-code((char*)buf, bufSize);
//  if (uequals(key, kConfigKey_SetupDiscriminator.Name)) return dip->Getdiscriminator((char*)buf, bufSize);
//  if (uequals(key, kConfigKey_Spake2pIterationCount.Name)) return dip->Getteration-count((char*)buf, bufSize);
//  if (uequals(key, kConfigKey_Spake2pSalt.Name)) return dip->Getsalt((char*)buf, bufSize);
//  if (uequals(key, kConfigKey_Spake2pVerifier.Name)) return dip->Getverifier((char*)buf, bufSize);
//  kConfigKey_MfrDeviceId           = { kConfigNamespace_ChipFactory, "device-id" };
//  kConfigKey_MfrDeviceCert         = { kConfigNamespace_ChipFactory, "device-cert" };
//  kConfigKey_MfrDeviceICACerts     = { kConfigNamespace_ChipFactory, "device-ca-certs" };
//  kConfigKey_MfrDevicePrivateKey   = { kConfigNamespace_ChipFactory, "device-key" };
  return CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND;
}


// MARK: - actual KVS based storage implementation

CHIP_ERROR PosixConfig::Init()
{
  // Nothing to do, we just rely on KVS being ready when it is accessed. If not, we'll get errors then
  return CHIP_NO_ERROR;
}


/// @param key the namespace/key to read
/// @param buf buffer to read into, set to nullptr (and bufSize==0) to just obtain value size
/// @param bufSize size of buffer to read into, set to 0 (and buf==nullptr) to just obtain value size
/// @param outLen actual value size (in both actual read and obtain size cases)
/// @return ok if value could be read or size could be obtained, error otherwise
CHIP_ERROR PosixConfig::ReadConfigValueBin(Key key, uint8_t * buf, size_t bufSize, size_t & outLen)
{
  if (key.Namespace==kConfigNamespace_ChipFactory) {
    // factory data, get from unified read-only factory data
    return ReadFactoryValueBin(key.Name, buf, bufSize, outLen);
  }
  // read from KVS
  string kvs_key = string_format("%s::%s", key.Namespace, key.Name);
  CHIP_ERROR err = PersistedStorage::KeyValueStoreMgr().Get(kvs_key.c_str(), buf, bufSize, &outLen);
  if (err == CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND) {
    err = CHIP_DEVICE_ERROR_CONFIG_NOT_FOUND;
  }
  return err;
}


CHIP_ERROR PosixConfig::WriteConfigValueBin(Key key, const uint8_t * data, size_t dataLen)
{
  if (key.Namespace==kConfigNamespace_ChipFactory) {
    // factory data, cannot be written
    return CHIP_ERROR_PERSISTED_STORAGE_FAILED;
  }
  // write to KVS
  string kvs_key = string_format("%s::%s", key.Namespace, key.Name);
  return PersistedStorage::KeyValueStoreMgr().Put(kvs_key.c_str(), data, dataLen);
}


CHIP_ERROR PosixConfig::ClearConfigValue(Key key)
{
  return PersistedStorage::KeyValueStoreMgr().Delete(key.Name);
}


bool PosixConfig::ConfigValueExists(Key key)
{
  size_t outLen;
  CHIP_ERROR err = ReadConfigValueBin(key, nullptr, 0, outLen);
  if (err == CHIP_NO_ERROR || err == CHIP_ERROR_BUFFER_TOO_SMALL) {
    return true;
  }
  return false;
}


CHIP_ERROR PosixConfig::EnsureNamespace(const char * ns)
{
  // all namespaces are there by default
  return CHIP_NO_ERROR;
}


CHIP_ERROR PosixConfig::ClearNamespace(const char * ns)
{
  CHIP_ERROR err = CHIP_ERROR_PERSISTED_STORAGE_FAILED;
  SuccessOrExit(err);
exit:
  return err;
}


CHIP_ERROR PosixConfig::FactoryResetConfig()
{
  CHIP_ERROR err = CHIP_ERROR_PERSISTED_STORAGE_FAILED;
  SuccessOrExit(err);

exit:
  return err;
}


CHIP_ERROR PosixConfig::FactoryResetCounters()
{
  CHIP_ERROR err = CHIP_ERROR_PERSISTED_STORAGE_FAILED;
  SuccessOrExit(err);

exit:
  return err;
}

void PosixConfig::RunConfigUnitTest() {}


} // namespace Internal
} // namespace DeviceLayer
} // namespace chip
