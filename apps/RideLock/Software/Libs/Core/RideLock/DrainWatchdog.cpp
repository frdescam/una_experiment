/**
 ******************************************************************************
 * @file    DrainWatchdog.cpp
 * @brief   Sliding-window battery drain detector (see DrainWatchdog.hpp).
 ******************************************************************************
 */

#include "RideLock/DrainWatchdog.hpp"

#include <cmath>

namespace RideLock
{

DrainWatchdog::DrainWatchdog(const DrainConfig& config)
    : mConfig(config)
{
}

void DrainWatchdog::configure(const DrainConfig& config)
{
    mConfig = config;
    reset();
}

void DrainWatchdog::reset()
{
    mHead     = 0;
    mCount    = 0;
    mLatestMa = 0.0f;
    mAlerting = false;
}

DrainEvent DrainWatchdog::setCharging(bool charging, uint32_t /*nowMs*/)
{
    if (charging == mCharging) {
        return DrainEvent::None;
    }
    mCharging = charging;

    // Readings taken across a plug or unplug describe neither state.
    const bool wasAlerting = mAlerting;
    reset();
    return wasAlerting ? DrainEvent::Clear : DrainEvent::None;
}

DrainEvent DrainWatchdog::addSample(float currentMa, uint32_t nowMs)
{
    if (mCharging || !std::isfinite(currentMa)) {
        return DrainEvent::None;
    }

    const float ma = std::fabs(currentMa);
    mLatestMa = ma;

    if (mConfig.windowMs > 0) {
        dropOlderThan(nowMs - mConfig.windowMs);
    }

    // Thin readings that arrive faster than the buffer can hold a window of.
    if (mCount == 0 ||
        static_cast<uint32_t>(nowMs - at(mCount - 1).t) >= minSpacingMs()) {
        push(Sample{ nowMs, ma });
    }

    if (mConfig.thresholdMa <= 0.0f || !windowCovered()) {
        return DrainEvent::None;
    }

    const float avg = averageMa();

    if (!mAlerting) {
        if (avg >= mConfig.thresholdMa) {
            mAlerting    = true;
            mLastAlertAt = nowMs;
            return DrainEvent::Alert;
        }
        return DrainEvent::None;
    }

    if (avg < mConfig.thresholdMa * mConfig.clearRatio) {
        mAlerting = false;
        return DrainEvent::Clear;
    }
    if (mConfig.realertMs > 0 &&
        static_cast<uint32_t>(nowMs - mLastAlertAt) >= mConfig.realertMs) {
        mLastAlertAt = nowMs;
        return DrainEvent::Alert;
    }
    return DrainEvent::None;
}

float DrainWatchdog::averageMa() const
{
    if (mCount == 0) {
        return 0.0f;
    }
    float sum = 0.0f;
    for (size_t i = 0; i < mCount; ++i) {
        sum += at(i).ma;
    }
    return sum / static_cast<float>(mCount);
}

bool DrainWatchdog::windowCovered() const
{
    if (mCount < 3) {
        return false;
    }
    const uint32_t span = at(mCount - 1).t - at(0).t;
    return static_cast<float>(span) >= mConfig.minCoverage * static_cast<float>(mConfig.windowMs);
}

void DrainWatchdog::push(const Sample& s)
{
    if (mCount == kCapacity) {
        mHead = (mHead + 1) % kCapacity;
        --mCount;
    }
    mBuf[(mHead + mCount) % kCapacity] = s;
    ++mCount;
}

void DrainWatchdog::dropOlderThan(uint32_t cutoff)
{
    while (mCount > 0 && static_cast<int32_t>(at(0).t - cutoff) < 0) {
        mHead = (mHead + 1) % kCapacity;
        --mCount;
    }
}

uint32_t DrainWatchdog::minSpacingMs() const
{
    // Leave a few slots spare so a full window never evicts its own start.
    return mConfig.windowMs / static_cast<uint32_t>(kCapacity - 4);
}

} // namespace RideLock
