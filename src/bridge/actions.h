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


using namespace chip;
using namespace app;
using namespace Clusters;
using namespace std;
using namespace p44;

using Status = Protocols::InteractionModel::Status;

class EndpointListInfo : public P44Obj
{
public:
  EndpointListInfo(uint16_t endpointListId, std::string name, Actions::EndpointListTypeEnum type);
  void addEndpoint(EndpointId endpointId);
  inline uint16_t GetEndpointListId() { return mEndpointListId; };
  std::string GetName() { return mName; };
  inline Actions::EndpointListTypeEnum GetType() { return mType; };
  inline EndpointId * GetEndpointListData() { return mEndpoints.data(); };
  inline size_t GetEndpointListSize() { return mEndpoints.size(); };

private:
  uint16_t mEndpointListId = static_cast<uint16_t>(0);
  std::string mName;
  chip::app::Clusters::Actions::EndpointListTypeEnum mType = static_cast<chip::app::Clusters::Actions::EndpointListTypeEnum>(0);
  std::vector<chip::EndpointId> mEndpoints;
};
typedef boost::intrusive_ptr<EndpointListInfo> EndpointListInfoPtr;





class Action : public P44LoggingObj
{
public:
  Action(uint16_t actionId, std::string name, chip::app::Clusters::Actions::ActionTypeEnum type, uint16_t endpointListId,
         uint16_t supportedCommands, chip::app::Clusters::Actions::ActionStateEnum status);
  inline void setName(std::string name) { mName = name; };
  inline std::string getName() { return mName; };
  inline chip::app::Clusters::Actions::ActionTypeEnum getType() { return mType; };
  inline chip::app::Clusters::Actions::ActionStateEnum getStatus() { return mStatus; };
  inline uint16_t getActionId() { return mActionId; };
  inline uint16_t getEndpointListId() { return mEndpointListId; };
  inline uint16_t getSupportedCommands() { return mSupportedCommands; };

  /// invoke action
  virtual void invoke(Optional<uint16_t> aTransitionTime);

private:
  std::string mName;
  chip::app::Clusters::Actions::ActionTypeEnum mType;
  chip::app::Clusters::Actions::ActionStateEnum mStatus;
  uint16_t mActionId;
  uint16_t mEndpointListId;
  uint16_t mSupportedCommands;
};
typedef boost::intrusive_ptr<Action> ActionPtr;


class ActionsManager : public AttributeAccessInterface
{
public:

  typedef std::map<uint16_t, ActionPtr> ActionsMap;
  typedef std::map<uint16_t, EndpointListInfoPtr> EndPointListsMap;

  // Register for the Actions cluster on all endpoints.
  ActionsManager(ActionsMap& aActions, EndPointListsMap& aEndPointLists) :
    AttributeAccessInterface(Optional<EndpointId>::Missing(), Actions::Id),
    mActions(aActions),
    mEndPointLists(aEndPointLists)
  {}

  CHIP_ERROR Read(const ConcreteReadAttributePath & aPath, AttributeValueEncoder & aEncoder) override;

  /// invoke instant action
  Status invokeInstantAction(
    const ConcreteCommandPath& aCommandPath,
    uint16_t aActionID,
    Optional<uint32_t> aInvokeID,
    Optional<uint16_t> aTransitionTime
  );

private:

  ActionsMap& mActions;
  EndPointListsMap& mEndPointLists;

  CHIP_ERROR ReadActionListAttribute(EndpointId endpoint, AttributeValueEncoder & aEncoder);
  CHIP_ERROR ReadEndpointListAttribute(EndpointId endpoint, AttributeValueEncoder & aEncoder);
  CHIP_ERROR ReadSetupUrlAttribute(EndpointId endpoint, AttributeValueEncoder & aEncoder);
  CHIP_ERROR ReadClusterRevision(EndpointId endpoint, AttributeValueEncoder & aEncoder);
};


