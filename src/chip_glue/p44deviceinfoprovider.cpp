/*
 *
 *    Copyright (c) 2022 Project CHIP Authors
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
#include "p44deviceinfoprovider.h"

#include <lib/core/TLV.h>
#include <lib/support/CHIPMemString.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/DefaultStorageKeyAllocator.h>
#include <lib/support/SafeInt.h>
#include <platform/internal/CHIPDeviceLayerInternal.h>

#include <stdlib.h>
#include <string.h>

#include <cstring>

namespace chip {
namespace DeviceLayer {

namespace {
  constexpr TLV::Tag kLabelNameTag  = TLV::ContextTag(0);
  constexpr TLV::Tag kLabelValueTag = TLV::ContextTag(1);
} // anonymous namespace


P44DeviceInfoProvider & P44DeviceInfoProvider::GetDefaultInstance()
{
  static P44DeviceInfoProvider sInstance;
  return sInstance;
}


// MARK: - Fixed Labels

DeviceInfoProvider::FixedLabelIterator * P44DeviceInfoProvider::IterateFixedLabel(EndpointId endpoint)
{
  return chip::Platform::New<FixedLabelIteratorImpl>(endpoint);
}


P44DeviceInfoProvider::FixedLabelIteratorImpl::FixedLabelIteratorImpl(EndpointId endpoint) : mEndpoint(endpoint)
{
  mIndex = 0;
}


size_t P44DeviceInfoProvider::FixedLabelIteratorImpl::Count()
{
  // empty list
  // TODO: future implementation might want to query actual P44 devices for fixed labels
  return 0;
}


bool P44DeviceInfoProvider::FixedLabelIteratorImpl::Next(FixedLabelType & output)
{
  // empty list
  return false;
}


// MARK: - Supported Locales

DeviceInfoProvider::SupportedLocalesIterator * P44DeviceInfoProvider::IterateSupportedLocales()
{
    return chip::Platform::New<SupportedLocalesIteratorImpl>();
}


size_t P44DeviceInfoProvider::SupportedLocalesIteratorImpl::Count()
{
  // empty list
  // TODO: future implementation might want to query actual P44 devices for supported locales
  return 0;
}


bool P44DeviceInfoProvider::SupportedLocalesIteratorImpl::Next(CharSpan & output)
{
  // empty list
  return false;
}


// MARK: - Supported Calendar Types

DeviceInfoProvider::SupportedCalendarTypesIterator * P44DeviceInfoProvider::IterateSupportedCalendarTypes()
{
  return chip::Platform::New<SupportedCalendarTypesIteratorImpl>();
}


size_t P44DeviceInfoProvider::SupportedCalendarTypesIteratorImpl::Count()
{
  // empty list
  // TODO: future implementation might want to query actual P44 devices for supported calendar types
  return 0;
}


bool P44DeviceInfoProvider::SupportedCalendarTypesIteratorImpl::Next(CalendarType & output)
{
  return 0;
}


// MARK: - User Labels

// Current implementation is copied from CHIP examples, works based on KVS storage

CHIP_ERROR P44DeviceInfoProvider::SetUserLabelLength(EndpointId endpoint, size_t val)
{
  return mStorage->SyncSetKeyValue(
    DefaultStorageKeyAllocator::UserLabelLengthKey(endpoint).KeyName(), &val,
    static_cast<uint16_t>(sizeof(val))
  );
}


CHIP_ERROR P44DeviceInfoProvider::GetUserLabelLength(EndpointId endpoint, size_t & val)
{
  uint16_t len = static_cast<uint16_t>(sizeof(val));

  return mStorage->SyncGetKeyValue(DefaultStorageKeyAllocator::UserLabelLengthKey(endpoint).KeyName(), &val, len);
}


CHIP_ERROR P44DeviceInfoProvider::SetUserLabelAt(EndpointId endpoint, size_t index, const UserLabelType & userLabel)
{
  VerifyOrReturnError(CanCastTo<uint32_t>(index), CHIP_ERROR_INVALID_ARGUMENT);

  uint8_t buf[UserLabelTLVMaxSize()];
  TLV::TLVWriter writer;
  writer.Init(buf);

  TLV::TLVType outerType;
  ReturnErrorOnFailure(writer.StartContainer(TLV::AnonymousTag(), TLV::kTLVType_Structure, outerType));
  ReturnErrorOnFailure(writer.PutString(kLabelNameTag, userLabel.label));
  ReturnErrorOnFailure(writer.PutString(kLabelValueTag, userLabel.value));
  ReturnErrorOnFailure(writer.EndContainer(outerType));

  return mStorage->SyncSetKeyValue(
    DefaultStorageKeyAllocator::UserLabelIndexKey(endpoint, static_cast<uint32_t>(index)).KeyName(), buf,
    static_cast<uint16_t>(writer.GetLengthWritten())
  );
}


CHIP_ERROR P44DeviceInfoProvider::DeleteUserLabelAt(EndpointId endpoint, size_t index)
{
  return mStorage->SyncDeleteKeyValue(
  DefaultStorageKeyAllocator::UserLabelIndexKey(endpoint, static_cast<uint32_t>(index)).KeyName());
}


DeviceInfoProvider::UserLabelIterator * P44DeviceInfoProvider::IterateUserLabel(EndpointId endpoint)
{
  return chip::Platform::New<UserLabelIteratorImpl>(*this, endpoint);
}


P44DeviceInfoProvider::UserLabelIteratorImpl::UserLabelIteratorImpl(P44DeviceInfoProvider & provider, EndpointId endpoint) :
  mProvider(provider), mEndpoint(endpoint)
{
  size_t total = 0;

  ReturnOnFailure(mProvider.GetUserLabelLength(mEndpoint, total));
  mTotal = total;
  mIndex = 0;
}


bool P44DeviceInfoProvider::UserLabelIteratorImpl::Next(UserLabelType & output)
{
  CHIP_ERROR err = CHIP_NO_ERROR;

  VerifyOrReturnError(mIndex < mTotal, false);
  VerifyOrReturnError(CanCastTo<uint32_t>(mIndex), false);

  uint8_t buf[UserLabelTLVMaxSize()];
  uint16_t len = static_cast<uint16_t>(sizeof(buf));

  err = mProvider.mStorage->SyncGetKeyValue(
      DefaultStorageKeyAllocator::UserLabelIndexKey(mEndpoint, static_cast<uint32_t>(mIndex)).KeyName(), buf, len);
  VerifyOrReturnError(err == CHIP_NO_ERROR, false);

  TLV::ContiguousBufferTLVReader reader;
  reader.Init(buf);
  err = reader.Next(TLV::kTLVType_Structure, TLV::AnonymousTag());
  VerifyOrReturnError(err == CHIP_NO_ERROR, false);

  TLV::TLVType containerType;
  VerifyOrReturnError(reader.EnterContainer(containerType) == CHIP_NO_ERROR, false);

  chip::CharSpan label;
  chip::CharSpan value;

  VerifyOrReturnError(reader.Next(kLabelNameTag) == CHIP_NO_ERROR, false);
  VerifyOrReturnError(reader.Get(label) == CHIP_NO_ERROR, false);

  VerifyOrReturnError(reader.Next(kLabelValueTag) == CHIP_NO_ERROR, false);
  VerifyOrReturnError(reader.Get(value) == CHIP_NO_ERROR, false);

  VerifyOrReturnError(reader.VerifyEndOfContainer() == CHIP_NO_ERROR, false);
  VerifyOrReturnError(reader.ExitContainer(containerType) == CHIP_NO_ERROR, false);

  Platform::CopyString(mUserLabelNameBuf, label);
  Platform::CopyString(mUserLabelValueBuf, value);

  output.label = CharSpan::fromCharString(mUserLabelNameBuf);
  output.value = CharSpan::fromCharString(mUserLabelValueBuf);

  mIndex++;

  return true;
}

} // namespace DeviceLayer
} // namespace chip
