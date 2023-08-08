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


#include "deviceattestationprovider.h"

#include <crypto/CHIPCryptoPAL.h>
#include <lib/core/CHIPError.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/Span.h>


using namespace chip::DeviceLayer::Internal;

extern uint8_t linker_nvm_end[];
static uint8_t * _credentials_address = (uint8_t *) linker_nvm_end;

CHIP_ERROR P44mbrdDeviceAttestationProvider::GetCertificationDeclaration(MutableByteSpan & out_span)
{
  uint32_t offset = SILABS_CREDENTIALS_CD_OFFSET;
  uint32_t size   = SILABS_CREDENTIALS_CD_SIZE;

  if (SilabsConfig::ConfigValueExists(SilabsConfig::kConfigKey_Creds_CD_Offset) &&
      SilabsConfig::ConfigValueExists(SilabsConfig::kConfigKey_Creds_CD_Size))
  {
    ReturnErrorOnFailure(SilabsConfig::ReadConfigValue(SilabsConfig::kConfigKey_Creds_CD_Offset, offset));
    ReturnErrorOnFailure(SilabsConfig::ReadConfigValue(SilabsConfig::kConfigKey_Creds_CD_Size, size));
  }

  uint8_t * address = _credentials_address + offset;
  ByteSpan cd_span(address, size);
  ChipLogProgress(DeviceLayer, "GetCertificationDeclaration, addr:%p, size:%lu", address, size);
  ChipLogByteSpan(DeviceLayer, ByteSpan(cd_span.data(), kDebugLength > cd_span.size() ? cd_span.size() : kDebugLength));
  return CopySpanToMutableSpan(cd_span, out_span);
}


CHIP_ERROR P44mbrdDeviceAttestationProvider::GetFirmwareInformation(MutableByteSpan & out_firmware_info_buffer)
{
  // TODO: We need a real example FirmwareInformation to be populated.
  out_firmware_info_buffer.reduce_size(0);
  return CHIP_NO_ERROR;
}


CHIP_ERROR P44mbrdDeviceAttestationProvider::GetDeviceAttestationCert(MutableByteSpan & out_span)
{
  uint32_t offset = SILABS_CREDENTIALS_DAC_OFFSET;
  uint32_t size   = SILABS_CREDENTIALS_DAC_SIZE;

  if (SilabsConfig::ConfigValueExists(SilabsConfig::kConfigKey_Creds_DAC_Offset) &&
      SilabsConfig::ConfigValueExists(SilabsConfig::kConfigKey_Creds_DAC_Size))
  {
    ReturnErrorOnFailure(SilabsConfig::ReadConfigValue(SilabsConfig::kConfigKey_Creds_DAC_Offset, offset));
    ReturnErrorOnFailure(SilabsConfig::ReadConfigValue(SilabsConfig::kConfigKey_Creds_DAC_Size, size));
  }

  uint8_t * address = _credentials_address + offset;
  ByteSpan cert_span(address, size);
  ChipLogProgress(DeviceLayer, "GetDeviceAttestationCert, addr:%p, size:%lu", address, size);
  ChipLogByteSpan(DeviceLayer, ByteSpan(cert_span.data(), kDebugLength > cert_span.size() ? cert_span.size() : kDebugLength));
  return CopySpanToMutableSpan(cert_span, out_span);
}


CHIP_ERROR P44mbrdDeviceAttestationProvider::GetProductAttestationIntermediateCert(MutableByteSpan & out_span)
{
  uint32_t offset = SILABS_CREDENTIALS_PAI_OFFSET;
  uint32_t size   = SILABS_CREDENTIALS_PAI_SIZE;

  if (SilabsConfig::ConfigValueExists(SilabsConfig::kConfigKey_Creds_PAI_Offset) &&
      SilabsConfig::ConfigValueExists(SilabsConfig::kConfigKey_Creds_PAI_Size))
  {
    ReturnErrorOnFailure(SilabsConfig::ReadConfigValue(SilabsConfig::kConfigKey_Creds_PAI_Offset, offset));
    ReturnErrorOnFailure(SilabsConfig::ReadConfigValue(SilabsConfig::kConfigKey_Creds_PAI_Size, size));
  }

  uint8_t * address = _credentials_address + offset;
  ByteSpan cert_span(address, size);
  ChipLogProgress(DeviceLayer, "GetProductAttestationIntermediateCert, addr:%p, size:%lu", address, size);
  ChipLogByteSpan(DeviceLayer, ByteSpan(cert_span.data(), kDebugLength > cert_span.size() ? cert_span.size() : kDebugLength));
  return CopySpanToMutableSpan(cert_span, out_span);
}


CHIP_ERROR P44mbrdDeviceAttestationProvider::SignWithDeviceAttestationKey(const ByteSpan & message_to_sign, MutableByteSpan & out_span)
{
  uint32_t key_id       = SILABS_CREDENTIALS_DAC_KEY_ID;
  uint8_t signature[64] = { 0 };
  size_t signature_size = sizeof(signature);

  if (SilabsConfig::ConfigValueExists(SilabsConfig::kConfigKey_Creds_KeyId))
  {
    ReturnErrorOnFailure(SilabsConfig::ReadConfigValue(SilabsConfig::kConfigKey_Creds_KeyId, key_id));
  }

  ChipLogProgress(DeviceLayer, "SignWithDeviceAttestationKey, key:%lu", key_id);

  psa_status_t err =
  psa_sign_message(static_cast<psa_key_id_t>(key_id), PSA_ALG_ECDSA(PSA_ALG_SHA_256), message_to_sign.data(),
                   message_to_sign.size(), signature, signature_size, &signature_size);
  VerifyOrReturnError(!err, CHIP_ERROR_INTERNAL);

  return CopySpanToMutableSpan(ByteSpan(signature, signature_size), out_span);
}
