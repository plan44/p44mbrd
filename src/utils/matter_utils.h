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

#include "p44mbrd_common.h"
#include "matter_common.h"
#include "utils.hpp"

using namespace std;

using namespace chip;
using namespace app;
using namespace Clusters;

using Status = Protocols::InteractionModel::Status;

string attrString(const EndpointId aEndpointId, const ClusterId aClusterId, const AttributeId aAttributeId);
void setAttrString(const EndpointId aEndpointId, const ClusterId aClusterId, const AttributeId aAttributeId, string aString, p44::AbbreviationStyle aAbbreviationStyle);

#define ATTR_STRING(cluster, attr, endpoint) attrString(endpoint, cluster::Id, cluster::Attributes::attr::Id)
#define SET_ATTR_STRING(cluster, attr, endpoint, string) setAttrString(endpoint, cluster::Id, cluster::Attributes::attr::Id, string, p44::end_ellipsis)
#define SET_ATTR_STRING_M(cluster, attr, endpoint, string) setAttrString(endpoint, cluster::Id, cluster::Attributes::attr::Id, string, p44::middle_ellipsis)

