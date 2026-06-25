/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
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
 *          Platform-specific implementatiuon of KVS for linux.
 */

#include <platform/KeyValueStoreManager.h>

#include <algorithm>
#include <inttypes.h>
#include <string.h>

#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>
#include <system/SystemClock.h>

#ifndef EXTERNAL_KEYVALUESTOREMANAGERIMPL_HEADER
  #error "This custom implementation of KeyValueStoreManagerImpl.cpp" cannot be used without EXTERNAL_KEYVALUESTOREMANAGERIMPL_HEADER
#else
  #include EXTERNAL_KEYVALUESTOREMANAGERIMPL_HEADER // use the external version (local override)
#endif

namespace chip {
namespace DeviceLayer {
namespace PersistedStorage {

KeyValueStoreManagerImpl KeyValueStoreManagerImpl::sInstance;

namespace {

#ifndef P44_KVS_DURABLE_RECOMMENDED_FLUSH_DELAY_MS
#define P44_KVS_DURABLE_RECOMMENDED_FLUSH_DELAY_MS (5ULL * 1000)
#endif

#ifndef P44_KVS_DURABLE_LATEST_FLUSH_DELAY_MS
#define P44_KVS_DURABLE_LATEST_FLUSH_DELAY_MS (30ULL * 1000)
#endif

#ifndef P44_KVS_CACHE_RECOMMENDED_FLUSH_DELAY_MS
#define P44_KVS_CACHE_RECOMMENDED_FLUSH_DELAY_MS (3ULL * 60 * 60 * 1000)
#endif

#ifndef P44_KVS_CACHE_LATEST_FLUSH_DELAY_MS
#define P44_KVS_CACHE_LATEST_FLUSH_DELAY_MS (24ULL * 60 * 60 * 1000)
#endif

constexpr uint64_t kDurableRecommendedFlushDelayMs = P44_KVS_DURABLE_RECOMMENDED_FLUSH_DELAY_MS;
constexpr uint64_t kDurableLatestFlushDelayMs      = P44_KVS_DURABLE_LATEST_FLUSH_DELAY_MS;
constexpr uint64_t kCacheRecommendedFlushDelayMs   = P44_KVS_CACHE_RECOMMENDED_FLUSH_DELAY_MS;
constexpr uint64_t kCacheLatestFlushDelayMs        = P44_KVS_CACHE_LATEST_FLUSH_DELAY_MS;

bool IsSessionResumptionLinkKey(const char * key)
{
    VerifyOrReturnValue(key != nullptr, false);

    // Fabric-scoped CASE resumption state: f/<fabric-index>/s/<node-id>
    if (strncmp(key, "f/", 2) != 0)
    {
        return false;
    }

    const char * fabricSeparator = strchr(key + 2, '/');
    return fabricSeparator != nullptr && strncmp(fabricSeparator, "/s/", 3) == 0;
}

bool IsCacheLikeKey(const char * key)
{
    VerifyOrReturnValue(key != nullptr, false);

    // Subscription resumption records are cache-like and can churn during unstable controller sessions.
    if (strncmp(key, "g/su/", 5) == 0)
    {
        return true;
    }

    // CASE session resumption records are also cache-like; a lost update only causes full CASE next time.
    if (strcmp(key, "g/sri") == 0 || strncmp(key, "g/s/", 4) == 0 || IsSessionResumptionLinkKey(key))
    {
        return true;
    }

    return false;
}

} // namespace

CHIP_ERROR KeyValueStoreManagerImpl::Init(const char * file)
{
    CHIP_ERROR err = mStorage.Init(file);
    if (err == CHIP_NO_ERROR)
    {
        mLastKvsFlushTimeMs = static_cast<uint64_t>(System::SystemClock().GetMonotonicMilliseconds64().count());
    }
    return err;
}

KeyValueStoreManagerImpl::CommitDelay KeyValueStoreManagerImpl::CommitDelaysFor(const char * key)
{
    if (IsCacheLikeKey(key))
    {
        return CommitDelay{ kCacheRecommendedFlushDelayMs, kCacheLatestFlushDelayMs };
    }

    return CommitDelay{ kDurableRecommendedFlushDelayMs, kDurableLatestFlushDelayMs };
}

void KeyValueStoreManagerImpl::DeferredFlushTimerHandler(System::Layer * /* systemLayer */, void * appState)
{
    VerifyOrReturn(appState != nullptr);
    static_cast<KeyValueStoreManagerImpl *>(appState)->HandleDeferredFlushTimer();
}

void KeyValueStoreManagerImpl::HandleDeferredFlushTimer()
{
    mFlushTimerArmed = false;

    CHIP_ERROR err = Flush();
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "Deferred KVS flush failed: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

void KeyValueStoreManagerImpl::ResetDeferredFlushState()
{
    mRecommendedFlushDeadlineMs = 0;
    mLatestFlushDeadlineMs      = 0;
    mFlushTimerArmed            = false;
}

CHIP_ERROR KeyValueStoreManagerImpl::ScheduleDeferredFlush(const char * key)
{
    const uint64_t nowMs = static_cast<uint64_t>(System::SystemClock().GetMonotonicMilliseconds64().count());
    const CommitDelay delay = CommitDelaysFor(key);
    const uint64_t recommendedDeadlineMs = nowMs + delay.recommendedAfterMs;
    const uint64_t latestDeadlineMs      = nowMs + delay.latestAfterMs;

    mRecommendedFlushDeadlineMs = recommendedDeadlineMs;
    if (!mFlushTimerArmed || mLatestFlushDeadlineMs == 0 || latestDeadlineMs < mLatestFlushDeadlineMs)
    {
        mLatestFlushDeadlineMs = latestDeadlineMs;
    }

    uint64_t nextDeadlineMs = std::min(mRecommendedFlushDeadlineMs, mLatestFlushDeadlineMs);
    uint64_t timerDelayMs   = (nextDeadlineMs > nowMs) ? (nextDeadlineMs - nowMs) : 0;
    timerDelayMs            = std::min<uint64_t>(timerDelayMs, UINT32_MAX);

    System::Layer & systemLayer = DeviceLayer::SystemLayer();
    if (!systemLayer.IsInitialized())
    {
        return CHIP_ERROR_INCORRECT_STATE;
    }

    if (mFlushTimerArmed)
    {
        systemLayer.CancelTimer(DeferredFlushTimerHandler, this);
        mFlushTimerArmed = false;
    }

    CHIP_ERROR err = systemLayer.StartTimer(System::Clock::Milliseconds32(static_cast<uint32_t>(timerDelayMs)),
                                            DeferredFlushTimerHandler, this);
    if (err == CHIP_NO_ERROR)
    {
        mFlushTimerArmed = true;
    }

    return err;
}

CHIP_ERROR KeyValueStoreManagerImpl::_Get(const char * key, void * value, size_t value_size, size_t * read_bytes_size,
                                          size_t offset_bytes)
{
    size_t read_size;

    // On linux read first without a buffer which returns the size, and then
    // use a local buffer to read the entire object, which allows partial and
    // offset reads.
    CHIP_ERROR err = mStorage.ReadValueBin(key, nullptr, 0, read_size);
    if (err == CHIP_ERROR_KEY_NOT_FOUND)
    {
        return CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND;
    }

    // there is a value for this key
    if (value_size==0) {
        // giving no buffer space always means it is too small
        // but we might use this to query the value length, so return it in read_bytes_size
        if (read_bytes_size != nullptr) {
            *read_bytes_size = read_size;
        }
        return CHIP_ERROR_BUFFER_TOO_SMALL;
    }

    // we have a value size, so buffer must exist
    if (value==nullptr) {
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    if ((err != CHIP_NO_ERROR) && (err != CHIP_ERROR_BUFFER_TOO_SMALL))
    {
        return err;
    }
    if (offset_bytes > read_size)
    {
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    Platform::ScopedMemoryBuffer<uint8_t> buf;
    VerifyOrReturnError(buf.Alloc(read_size), CHIP_ERROR_NO_MEMORY);
    ReturnErrorOnFailure(mStorage.ReadValueBin(key, buf.Get(), read_size, read_size));

    size_t total_size_to_read = read_size - offset_bytes;
    size_t copy_size          = std::min(value_size, total_size_to_read);
    if (read_bytes_size != nullptr)
    {
        *read_bytes_size = copy_size;
    }
    ::memcpy(value, buf.Get() + offset_bytes, copy_size);

    return (value_size < total_size_to_read) ? CHIP_ERROR_BUFFER_TOO_SMALL : CHIP_NO_ERROR;
}


CHIP_ERROR KeyValueStoreManagerImpl::Flush()
{
    const uint64_t startMs = static_cast<uint64_t>(System::SystemClock().GetMonotonicMilliseconds64().count());
    bool didCommit        = false;
    CHIP_ERROR err        = mStorage.Commit(&didCommit);
    const uint64_t endMs  = static_cast<uint64_t>(System::SystemClock().GetMonotonicMilliseconds64().count());

    if (err == CHIP_NO_ERROR && didCommit)
    {
        if (mFlushTimerArmed && DeviceLayer::SystemLayer().IsInitialized())
        {
            DeviceLayer::SystemLayer().CancelTimer(DeferredFlushTimerHandler, this);
        }
        mLastKvsFlushTimeMs = endMs;
        ResetDeferredFlushState();
        ChipLogProgress(DeviceLayer, "Flushed KVS entries to file in %" PRIu64 " ms", endMs - startMs);
    }

    return err;
}


CHIP_ERROR KeyValueStoreManagerImpl::_Put(const char * key, const void * value, size_t value_size)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    err = mStorage.WriteValueBin(key, reinterpret_cast<const uint8_t *>(value), value_size);
    SuccessOrExit(err);

    if (mStorage.IsDirty())
    {
        err = ScheduleDeferredFlush(key);
        if (err != CHIP_NO_ERROR)
        {
            ChipLogDetail(DeviceLayer, "Deferred KVS flush scheduling failed for key '%s': %" CHIP_ERROR_FORMAT, key,
                          err.Format());
            err = CHIP_NO_ERROR;
        }
    }

exit:
    return err;
}

CHIP_ERROR KeyValueStoreManagerImpl::_Delete(const char * key)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    err            = mStorage.ClearValue(key);

    if (err == CHIP_ERROR_KEY_NOT_FOUND)
    {
        ExitNow(err = CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND);
    }
    SuccessOrExit(err);

    if (mStorage.IsDirty())
    {
        err = ScheduleDeferredFlush(key);
        if (err != CHIP_NO_ERROR)
        {
            ChipLogDetail(DeviceLayer, "Deferred KVS flush scheduling failed for deleted key '%s': %" CHIP_ERROR_FORMAT, key,
                          err.Format());
            err = CHIP_NO_ERROR;
        }
    }

exit:
    return err;
}

} // namespace PersistedStorage
} // namespace DeviceLayer
} // namespace chip
