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

#include "matter_common.h"
#include "p44mbrd_common.h"

#include "factorydataprovider.h"

#include <credentials/DeviceAttestationCredsProvider.h>

using namespace chip;
using namespace Credentials;

class P44mbrdDeviceAttestationProvider : public DeviceAttestationCredentialsProvider
{

  string mCD; ///< the certification declaration
  string mFirmwareInfo; ///< the firmware information from the certification
  string mDAC; ///< the device attestation certificate
  string mPAIC; ///< the product attestation intermediate certificate
  string mDACKey; ///< the device attestation private key
  string mDACPubKey; ///< the device attestation public key

public:

  void loadFromFactoryData(FactoryDataProviderPtr aFactoryDataProvider);

  CHIP_ERROR GetCertificationDeclaration(MutableByteSpan & out_span) override;
  CHIP_ERROR GetFirmwareInformation(MutableByteSpan & out_firmware_info_buffer) override;
  CHIP_ERROR GetDeviceAttestationCert(MutableByteSpan & out_span) override;
  CHIP_ERROR GetProductAttestationIntermediateCert(MutableByteSpan & out_span) override;
  CHIP_ERROR SignWithDeviceAttestationKey(const ByteSpan & message_to_sign, MutableByteSpan & out_span) override;
  
};
