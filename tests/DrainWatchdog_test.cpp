#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "RideLock/DrainWatchdog.hpp"

using namespace RideLock;

namespace
{

constexpr uint32_t kSec = 1000;
constexpr uint32_t kMin = 60 * kSec;

DrainConfig cfg(float thresholdMa = 8.0f, uint32_t windowMs = 10 * kMin)
{
    DrainConfig c;
    c.thresholdMa = thresholdMa;
    c.windowMs    = windowMs;
    c.realertMs   = 30 * kMin;
    return c;
}

/// Feed a constant current every @p periodMs from @p t to @p t + @p durMs.
/// Returns the number of Alert events and leaves @p t at the end.
int feed(DrainWatchdog& w, uint32_t& t, uint32_t durMs, float ma, uint32_t periodMs = 30 * kSec,
         int* clears = nullptr)
{
    int alerts = 0;
    for (uint32_t end = t + durMs; static_cast<int32_t>(t - end) < 0; t += periodMs) {
        const DrainEvent e = w.addSample(ma, t);
        if (e == DrainEvent::Alert) {
            ++alerts;
        }
        if (e == DrainEvent::Clear && clears != nullptr) {
            ++*clears;
        }
    }
    return alerts;
}

} // namespace

TEST(DrainWatchdog, AnIdleWatchNeverAlerts)
{
    DrainWatchdog w(cfg());
    uint32_t t = 0;
    EXPECT_EQ(feed(w, t, 12 * 60 * kMin, 1.2f), 0);
    EXPECT_NEAR(w.averageMa(), 1.2f, 1e-4);
    EXPECT_FALSE(w.alerting());
}

TEST(DrainWatchdog, AGpsSearchBehindTheLockAlertsAfterOneWindow)
{
    DrainWatchdog w(cfg());
    uint32_t t = 0;
    // Not before the window is covered, however high the reading.
    EXPECT_EQ(feed(w, t, 7 * kMin, 25.0f), 0);
    EXPECT_FALSE(w.windowCovered());
    // Once covered, one alert.
    EXPECT_EQ(feed(w, t, 3 * kMin, 25.0f), 1);
    EXPECT_TRUE(w.alerting());
    EXPECT_NEAR(w.latestMa(), 25.0f, 1e-4);
}

TEST(DrainWatchdog, AShortBurstDoesNotAlert)
{
    // Two minutes of a 30 mA burst (a sync, a backlight) in an idle window.
    DrainWatchdog w(cfg());
    uint32_t t = 0;
    feed(w, t, 10 * kMin, 1.0f);
    EXPECT_EQ(feed(w, t, 2 * kMin, 30.0f), 0);
    EXPECT_LT(w.averageMa(), 8.0f);
    EXPECT_EQ(feed(w, t, 20 * kMin, 1.0f), 0);
}

TEST(DrainWatchdog, RemindsWhileTheDrainLastsThenClearsWithHysteresis)
{
    DrainWatchdog w(cfg());
    uint32_t t = 0;
    int clears = 0;
    // First alert once 80 % of the window is covered (8 min), then reminders
    // every 30 min: at 8, 38 and 68 min.
    EXPECT_EQ(feed(w, t, 69 * kMin, 20.0f, 30 * kSec, &clears), 3);
    EXPECT_EQ(clears, 0);

    // Just under the threshold is not enough to clear (hysteresis).
    feed(w, t, 15 * kMin, 7.0f, 30 * kSec, &clears);
    EXPECT_EQ(clears, 0);
    EXPECT_TRUE(w.alerting());

    // Back to idle: clears once.
    feed(w, t, 15 * kMin, 1.0f, 30 * kSec, &clears);
    EXPECT_EQ(clears, 1);
    EXPECT_FALSE(w.alerting());
}

TEST(DrainWatchdog, UsesTheMagnitudeWhateverTheGaugesSign)
{
    DrainWatchdog w(cfg());
    uint32_t t = 0;
    EXPECT_EQ(feed(w, t, 11 * kMin, -18.0f), 1);
}

TEST(DrainWatchdog, ChargingSuspendsAndClears)
{
    DrainWatchdog w(cfg());
    uint32_t t = 0;
    ASSERT_EQ(feed(w, t, 11 * kMin, 20.0f), 1);

    EXPECT_EQ(w.setCharging(true, t), DrainEvent::Clear);
    EXPECT_EQ(w.setCharging(true, t), DrainEvent::None);   // no change, no event
    EXPECT_EQ(feed(w, t, 30 * kMin, 400.0f), 0);           // charge current ignored
    EXPECT_FALSE(w.hasEstimate());

    EXPECT_EQ(w.setCharging(false, t), DrainEvent::None);
    // Starts from scratch: a full window again before any alert.
    EXPECT_EQ(feed(w, t, 7 * kMin, 20.0f), 0);
}

TEST(DrainWatchdog, AZeroThresholdOnlyMeasures)
{
    DrainWatchdog w(cfg(0.0f));
    uint32_t t = 0;
    EXPECT_EQ(feed(w, t, 60 * kMin, 50.0f), 0);
    EXPECT_NEAR(w.averageMa(), 50.0f, 1e-3);
}

TEST(DrainWatchdog, IgnoresNonFiniteReadings)
{
    DrainWatchdog w(cfg());
    EXPECT_EQ(w.addSample(std::numeric_limits<float>::quiet_NaN(), 0), DrainEvent::None);
    EXPECT_EQ(w.addSample(std::numeric_limits<float>::infinity(), 1000), DrainEvent::None);
    EXPECT_FALSE(w.hasEstimate());
}

TEST(DrainWatchdog, FastReadingsAreThinnedAndTheAverageHolds)
{
    // One reading a second for an hour: far more than the buffer holds.
    DrainWatchdog w(cfg());
    uint32_t t = 0;
    EXPECT_EQ(feed(w, t, 60 * kMin, 3.0f, kSec), 0);
    EXPECT_TRUE(w.windowCovered());
    EXPECT_NEAR(w.averageMa(), 3.0f, 1e-4);
    // And a step change shows up within a window.
    EXPECT_EQ(feed(w, t, 11 * kMin, 12.0f, kSec), 1);
}

TEST(DrainWatchdog, SurvivesTheTickWrap)
{
    DrainWatchdog w(cfg());
    uint32_t t = 0xFFFFFFFFu - 5 * kMin;
    EXPECT_EQ(feed(w, t, 11 * kMin, 20.0f), 1);
}
