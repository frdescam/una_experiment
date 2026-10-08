/**
 ******************************************************************************
 * @file    DrainWatchdog.hpp
 * @brief   Notices a battery drain that the lock screen alone does not explain.
 *
 * A lock only stops presses from now on. An activity opened before the lock
 * went up (in the minutes after an unlock, say) keeps its GPS running behind
 * the lock screen, out of sight. The kernel gives no list of running apps, but
 * the fuel gauge's current is visible to any app (SDK BATTERY_METRICS), so the
 * service samples it slowly while locked and feeds it here.
 *
 * The watchdog averages the magnitude of the current over a sliding window and
 * raises an alert when that average stays at or above a threshold for a whole
 * window. The alert clears with hysteresis, repeats as a reminder while the
 * drain lasts, and is suppressed while charging, when the gauge's current says
 * nothing about consumption.
 *
 * The sign convention of the gauge's current is not documented, so only its
 * magnitude is used; that is why charging must be reported separately.
 ******************************************************************************
 */

#ifndef RIDELOCK_DRAIN_WATCHDOG_HPP
#define RIDELOCK_DRAIN_WATCHDOG_HPP

#include <cstddef>
#include <cstdint>

namespace RideLock
{

struct DrainConfig {
    float    thresholdMa = 8.0f;                 ///< alert level; 0 disables alerts
    uint32_t windowMs    = 10u * 60u * 1000u;    ///< averaging window, and minimum duration
    uint32_t realertMs   = 30u * 60u * 1000u;    ///< reminder period while the drain lasts
    float    clearRatio  = 0.6f;                 ///< clear below threshold * clearRatio
    float    minCoverage = 0.8f;                 ///< samples must span this share of the window
};

enum class DrainEvent : uint8_t {
    None,
    Alert,    ///< drain detected (or reminder): tell the wearer
    Clear,    ///< drain over, or charging started
};

class DrainWatchdog
{
public:
    /// Samples kept. Sized for one sample every 15 s over a 15 min window;
    /// faster input is thinned, so any window fits.
    static constexpr size_t kCapacity = 64;

    explicit DrainWatchdog(const DrainConfig& config = DrainConfig{});

    /// Replace the configuration and forget all samples and alert state.
    void configure(const DrainConfig& config);

    /// Forget all samples and alert state.
    void reset();

    /// Charging (or a cable) suspends the watchdog and clears any alert.
    DrainEvent setCharging(bool charging, uint32_t nowMs);

    /// One current reading in mA, either sign. Non-finite values are ignored.
    DrainEvent addSample(float currentMa, uint32_t nowMs);

    bool  hasEstimate() const { return mCount > 0; }
    float latestMa() const { return mLatestMa; }
    float averageMa() const;

    /// True when the samples cover enough of the window to be judged.
    bool  windowCovered() const;

    bool  alerting() const { return mAlerting; }
    bool  charging() const { return mCharging; }
    const DrainConfig& config() const { return mConfig; }

private:
    struct Sample {
        uint32_t t;
        float    ma;
    };

    const Sample& at(size_t i) const { return mBuf[(mHead + i) % kCapacity]; }
    void          push(const Sample& s);
    void          dropOlderThan(uint32_t cutoff);
    uint32_t      minSpacingMs() const;

    DrainConfig mConfig;
    Sample      mBuf[kCapacity] = {};
    size_t      mHead     = 0;   ///< index of the oldest sample
    size_t      mCount    = 0;
    float       mLatestMa = 0.0f;
    bool        mCharging = false;
    bool        mAlerting = false;
    uint32_t    mLastAlertAt = 0;
};

} // namespace RideLock

#endif // RIDELOCK_DRAIN_WATCHDOG_HPP
