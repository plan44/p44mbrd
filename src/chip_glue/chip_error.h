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

#include <lib/core/CHIPError.h>
#include <string>

#include "error.hpp"

using namespace std;

namespace p44 {

  /// ConnectedHomeIP/Matter error
  class P44ChipError : public Error
  {
  public:
    static const char *domain();
    virtual const char *getErrorDomain() const P44_OVERRIDE;

    /// create system error from passed CHIP_ERROR
    /// @param aChipError a CHIP native error
    /// @param aContextMessage if set, this message will be used to prefix the error message
    P44ChipError(CHIP_ERROR aChipError, const char *aContextMessage = NULL);

    /// factory function to create a ErrorPtr either containing NULL (if aChipError indicates OK)
    /// or a P44ChipError (if aChipError is not CHIP_NO_ERROR)
    /// @param aChipError a CHIP native error
    /// @param aContextMessage if set, this message will be used to prefix the error message
    static ErrorPtr err(CHIP_ERROR aChipError, const char *aContextMessage = NULL);
  };

} // namespace p44
