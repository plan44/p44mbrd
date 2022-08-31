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

#include "chip_error.h"
#include "utils.hpp"

using namespace std;
using namespace p44;

// MARK: - ESP IDF error

const char *P44ChipError::domain()
{
  return "CHIP/Matter";
}


const char *P44ChipError::getErrorDomain() const
{
  return P44ChipError::domain();
}


P44ChipError::P44ChipError(CHIP_ERROR aChipError, const char *aContextMessage) :
  Error(aChipError.GetValue(), string(nonNullCStr(aContextMessage)).append(nonNullCStr(aChipError.AsString())))
{
}


ErrorPtr P44ChipError::err(CHIP_ERROR aChipError, const char *aContextMessage)
{
  if (aChipError==CHIP_NO_ERROR)
    return ErrorPtr(); // empty, no error
  return ErrorPtr(new P44ChipError(aChipError, aContextMessage));
}
