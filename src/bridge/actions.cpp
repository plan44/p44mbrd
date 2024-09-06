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

#include "actions.h"


// MARK: - EndpointListInfo

EndpointListInfo::EndpointListInfo(uint16_t endpointListId, std::string name, Actions::EndpointListTypeEnum type)
{
  mEndpointListId = endpointListId;
  mName = name;
  mType = type;
}


void EndpointListInfo::addEndpoint(chip::EndpointId endpointId)
{
  mEndpoints.push_back(endpointId);
}


// MARK: - Action

Action::Action(
 uint16_t actionId,
 std::string name,
 Actions::ActionTypeEnum type,
 uint16_t endpointListId,
 uint16_t supportedCommands,
 Actions::ActionStateEnum status
)
{
  mActionId = actionId;
  mName = name;
  mType = type;
  mEndpointListId = endpointListId;
  mSupportedCommands = supportedCommands;
  mStatus = status;
}


void Action::invoke(Optional<uint16_t> aTransitionTime)
{
  OLOG(LOG_WARNING, "invoke not implemented")
}


// MARK: - ActionsManager

CHIP_ERROR ActionsManager::ReadActionListAttribute(EndpointId endpoint, AttributeValueEncoder & aEncoder)
{
  CHIP_ERROR err = aEncoder.EncodeList([this](const auto & encoder) -> CHIP_ERROR {
    for (auto action : mActions) {
      Actions::Structs::ActionStruct::Type actionStruct = {
        action.first, // map key is the action ID
        CharSpan::fromCharString(action.second->getName().c_str()),
        action.second->getType(),
        action.second->getEndpointListId(),
        action.second->getSupportedCommands(),
        action.second->getStatus()
      };
      ReturnErrorOnFailure(encoder.Encode(actionStruct));
    }
    return CHIP_NO_ERROR;
  });
  return err;
}


CHIP_ERROR ActionsManager::ReadEndpointListAttribute(EndpointId endpoint, AttributeValueEncoder & aEncoder)
{
  CHIP_ERROR err = aEncoder.EncodeList([this](const auto & encoder) -> CHIP_ERROR {
    for (auto info : mEndPointLists) {
      Actions::Structs::EndpointListStruct::Type endpointListStruct = {
        info.first, // map key is the endpointList ID
        CharSpan::fromCharString(info.second->GetName().c_str()),
        info.second->GetType(),
        DataModel::List<chip::EndpointId>(info.second->GetEndpointListData(), info.second->GetEndpointListSize())
      };
      ReturnErrorOnFailure(encoder.Encode(endpointListStruct));
    }
    return CHIP_NO_ERROR;
  });
  return err;
}


CHIP_ERROR ActionsManager::Read(const ConcreteReadAttributePath & aPath, AttributeValueEncoder & aEncoder)
{
  VerifyOrDie(aPath.mClusterId == Actions::Id);

  switch (aPath.mAttributeId)
  {
    case Actions::Attributes::ActionList::Id:
      return ReadActionListAttribute(aPath.mEndpointId, aEncoder);
    case Actions::Attributes::EndpointLists::Id:
      return ReadEndpointListAttribute(aPath.mEndpointId, aEncoder);
    // Note: we let ember storage handle URL and cluster version
    default:
      // As long as we don't touch aEncoder (which would set TriedEncode())
      // exiting here will fall back to automatic ember storage
      break;
  }
  return CHIP_NO_ERROR;
}



Protocols::InteractionModel::Status ActionsManager::invokeInstantAction(
  const ConcreteCommandPath& aCommandPath,
  uint16_t aActionID,
  Optional<uint32_t> aInvokeID,
  Optional<uint16_t> aTransitionTime
)
{
  ActionsMap::iterator action = mActions.find(aActionID);
  if (action==mActions.end()) {
    return Protocols::InteractionModel::Status::NotFound;
  }
  action->second->invoke(aTransitionTime);
  return Protocols::InteractionModel::Status::Success;
}
