#include <gtest/gtest.h>

#include "KernelEvents.hpp"
#include "RideLock/UnlockDetector.hpp"

using namespace RideLock;
using TestSupport::tap;
using TestSupport::down;
using TestSupport::up;

namespace
{

constexpr uint32_t kStep = 450;   // a person's pace between taps

class UnlockDetectorTest : public ::testing::Test
{
protected:
    UnlockDetector d;            // default sequence L1 R1 R2 L2 L1 R1, default timing
    uint32_t       t = 100000;   // well past any start-up quiet period

    void SetUp() override { d.reset(0); }

    /// Tap the detector's whole sequence starting at the current time.
    Outcome enterDefault()
    {
        Outcome o = Outcome::None;
        const Sequence seq = d.sequence();
        for (uint8_t i = 0; i < seq.length; ++i) {
            o = tap(d, seq[i], t);
            t += kStep;
        }
        return o;
    }
};

} // namespace

TEST_F(UnlockDetectorTest, TheSequenceUnlocks)
{
    EXPECT_EQ(enterDefault(), Outcome::Unlocked);
    EXPECT_TRUE(d.unlocked());
}

TEST_F(UnlockDetectorTest, EachCorrectStepReportsProgressAndTheNextButton)
{
    EXPECT_EQ(d.length(), 6);
    EXPECT_EQ(d.expected(), Button::L1);
    EXPECT_EQ(tap(d, Button::L1, t), Outcome::Progress);
    EXPECT_EQ(d.progress(), 1);
    EXPECT_EQ(d.expected(), Button::R1);
    EXPECT_EQ(tap(d, Button::R1, t += kStep), Outcome::Progress);
    EXPECT_EQ(tap(d, Button::R2, t += kStep), Outcome::Progress);
    EXPECT_EQ(d.progress(), 3);
    EXPECT_EQ(d.expected(), Button::L2);
    EXPECT_EQ(tap(d, Button::L2, t += kStep), Outcome::Progress);
    EXPECT_EQ(tap(d, Button::L1, t += kStep), Outcome::Progress);
    EXPECT_EQ(d.expected(), Button::R1);
    EXPECT_EQ(tap(d, Button::R1, t += kStep), Outcome::Unlocked);
}

TEST_F(UnlockDetectorTest, AWrongButtonStartsAgain)
{
    tap(d, Button::L1, t);
    tap(d, Button::R1, t += kStep);
    tap(d, Button::R2, t += kStep);
    tap(d, Button::L2, t += kStep);
    EXPECT_EQ(tap(d, Button::R1, t += kStep), Outcome::Wrong);   // L1 was due
    EXPECT_EQ(d.progress(), 0);
    EXPECT_FALSE(d.unlocked());
}

TEST_F(UnlockDetectorTest, TheFirstStepNeedsAQuietMoment)
{
    // A wrong press, then the right first button straight after: refused.
    EXPECT_EQ(tap(d, Button::R2, t), Outcome::Wrong);
    EXPECT_EQ(tap(d, Button::L1, t += 300), Outcome::NotQuiet);
    EXPECT_EQ(d.progress(), 0);

    // After a pause the same press starts the sequence.
    t += 300 + d.timing().quietMs;
    EXPECT_EQ(enterDefault(), Outcome::Unlocked);
}

TEST_F(UnlockDetectorTest, QuietIsMeasuredFromTheLastRelease)
{
    // A long rub on R2 ends; the first step must wait quietMs after the release.
    down(d, Button::R2, t);
    up(d, Button::R2, t + 3000, 3000);
    t += 3000 + 200;
    EXPECT_EQ(tap(d, Button::L1, t), Outcome::NotQuiet);
}

TEST_F(UnlockDetectorTest, LaterStepsDoNotNeedQuiet)
{
    // Brisk entry: 150 ms between steps is fine once the attempt has started.
    EXPECT_EQ(tap(d, Button::L1, t, 80), Outcome::Progress);
    EXPECT_EQ(tap(d, Button::R1, t += 150, 80), Outcome::Progress);
    EXPECT_EQ(tap(d, Button::R2, t += 150, 80), Outcome::Progress);
    EXPECT_EQ(tap(d, Button::L2, t += 150, 80), Outcome::Progress);
    EXPECT_EQ(tap(d, Button::L1, t += 150, 80), Outcome::Progress);
    EXPECT_EQ(tap(d, Button::R1, t += 150, 80), Outcome::Unlocked);
}

TEST_F(UnlockDetectorTest, TwoButtonsAtOnceIsAChord)
{
    tap(d, Button::L1, t);
    t += kStep;
    down(d, Button::R1, t);
    EXPECT_EQ(down(d, Button::R2, t + 50), Outcome::Chord);
    EXPECT_EQ(d.progress(), 0);
    // Neither release can complete a step.
    EXPECT_EQ(up(d, Button::R1, t + 100, 100), Outcome::None);
    EXPECT_EQ(up(d, Button::R2, t + 120, 70), Outcome::None);
    EXPECT_EQ(d.progress(), 0);
}

TEST_F(UnlockDetectorTest, AHoldIsNotAStep)
{
    EXPECT_EQ(tap(d, Button::L1, t, d.timing().maxPressMs + 1), Outcome::TooLong);
    EXPECT_EQ(d.progress(), 0);
}

TEST_F(UnlockDetectorTest, AGloveSlowPressStillCounts)
{
    // Slower than the kernel's 500 ms click: no CLICK, only PRESS/RELEASE.
    EXPECT_EQ(tap(d, Button::L1, t, 900), Outcome::Progress);
}

TEST_F(UnlockDetectorTest, AnAbandonedAttemptTimesOut)
{
    tap(d, Button::L1, t);
    tap(d, Button::R1, t += kStep);
    EXPECT_EQ(d.poll(t + 1000), Outcome::None);
    EXPECT_EQ(d.poll(t + 120 + d.timing().maxGapMs + 1), Outcome::Timeout);
    EXPECT_EQ(d.progress(), 0);
    EXPECT_EQ(d.poll(t + 10000), Outcome::None);  // reported once
}

TEST_F(UnlockDetectorTest, ALateStepStartsAFreshAttemptEvenWithoutPoll)
{
    tap(d, Button::L1, t);
    t += 120 + d.timing().maxGapMs + 100;
    // Too late for step two; but quiet enough to be a new step one.
    EXPECT_EQ(tap(d, Button::L1, t), Outcome::Progress);
    EXPECT_EQ(d.progress(), 1);
}

TEST_F(UnlockDetectorTest, APinnedButtonDoesNotLockTheWearerOut)
{
    Sequence s;
    ASSERT_TRUE(parseSequence("L1 R1", s));
    d.configure(s, UnlockTiming{}, 0);

    // The cuff pins R2 down for good.
    down(d, Button::R2, t);

    // While it is fresh, any other press is a chord.
    EXPECT_EQ(down(d, Button::L1, t + 500), Outcome::Chord);
    up(d, Button::L1, t + 600, 100);

    // Once it has been down past stuckMs it is ignored.
    t += d.timing().stuckMs + 1000;
    EXPECT_EQ(tap(d, Button::L1, t), Outcome::Progress);
    EXPECT_EQ(tap(d, Button::R1, t + kStep), Outcome::Unlocked);
}

TEST_F(UnlockDetectorTest, AKernelThatOnlySendsClicksStillWorks)
{
    Outcome o = Outcome::None;
    const Sequence seq = d.sequence();
    for (uint8_t i = 0; i < seq.length; ++i) {
        o = d.onInput(seq[i], Input::Click, t);
        t += kStep;
    }
    EXPECT_EQ(o, Outcome::Unlocked);
}

TEST_F(UnlockDetectorTest, AClickArrivingAfterItsReleaseIsNotASecondStep)
{
    // Some ports deliver PRESS, RELEASE, CLICK. The late CLICK must not count
    // again (that would be "L1 L1" and a Wrong).
    EXPECT_EQ(down(d, Button::L1, t), Outcome::None);
    EXPECT_EQ(d.onInput(Button::L1, Input::Release, t + 100), Outcome::Progress);
    EXPECT_EQ(d.onInput(Button::L1, Input::Click, t + 200), Outcome::None);
    EXPECT_EQ(d.progress(), 1);
}

TEST_F(UnlockDetectorTest, AReleaseWithoutAPressIsIgnored)
{
    EXPECT_EQ(d.onInput(Button::L1, Input::Release, t), Outcome::None);
    EXPECT_EQ(d.progress(), 0);
}

TEST_F(UnlockDetectorTest, NothingCountsOnceUnlockedUntilReset)
{
    ASSERT_EQ(enterDefault(), Outcome::Unlocked);
    EXPECT_EQ(tap(d, Button::R2, t += 5000), Outcome::None);
    EXPECT_TRUE(d.unlocked());

    d.reset(t);
    EXPECT_FALSE(d.unlocked());
    EXPECT_EQ(d.progress(), 0);
}

TEST_F(UnlockDetectorTest, ResetForgetsHeldButtonsAndRestartsTheQuietPeriod)
{
    down(d, Button::R1, t);         // its release is lost while the screen was away
    d.reset(t + 100);
    // Too soon after the reset to start.
    EXPECT_EQ(tap(d, Button::L1, t + 300), Outcome::NotQuiet);
    // Later, and no chord with the forgotten R1.
    t += 100 + 300 + 120 + d.timing().quietMs;
    EXPECT_EQ(enterDefault(), Outcome::Unlocked);
}

TEST_F(UnlockDetectorTest, SurvivesTheTickCounterWrapping)
{
    d.reset(0xFFFFFC00u - 5000u);
    t = 0xFFFFFC00u;   // 1 s before the wrap: steps three and four come after it
    EXPECT_EQ(enterDefault(), Outcome::Unlocked);
}

TEST_F(UnlockDetectorTest, RepeatedRejectionsPauseUnlocking)
{
    // Three wrong presses in a few seconds: a sleeve, not a person.
    EXPECT_EQ(tap(d, Button::R2, t), Outcome::Wrong);
    EXPECT_EQ(tap(d, Button::R1, t += 1000), Outcome::Wrong);
    EXPECT_EQ(tap(d, Button::L2, t += 1000), Outcome::CoolingDown);
    EXPECT_TRUE(d.coolingDown());

    // Now even the right sequence does nothing.
    t += 1000;
    EXPECT_EQ(enterDefault(), Outcome::CoolingDown);
    EXPECT_FALSE(d.unlocked());
    EXPECT_GT(d.cooldownRemaining(t), 0u);

    // Hands off for cooldownMs, and it is over.
    const uint32_t quietFrom = t - kStep + 120;   // the last release
    EXPECT_EQ(d.poll(quietFrom + d.timing().cooldownMs - 1), Outcome::None);
    EXPECT_EQ(d.poll(quietFrom + d.timing().cooldownMs), Outcome::CooledDown);
    EXPECT_FALSE(d.coolingDown());
    EXPECT_EQ(d.poll(quietFrom + d.timing().cooldownMs + 100), Outcome::None);   // once

    t = quietFrom + d.timing().cooldownMs + 1000;
    EXPECT_EQ(enterDefault(), Outcome::Unlocked);
}

TEST_F(UnlockDetectorTest, TouchingAButtonRestartsTheCooldown)
{
    tap(d, Button::R2, t);
    tap(d, Button::R2, t += 1000);
    ASSERT_EQ(tap(d, Button::R2, t += 1000), Outcome::CoolingDown);

    // Every bump restarts the silence clock.
    for (int i = 0; i < 5; ++i) {
        t += d.timing().cooldownMs - 2000;
        EXPECT_EQ(d.poll(t), Outcome::None);
        EXPECT_EQ(tap(d, Button::R1, t), Outcome::CoolingDown);
    }
    EXPECT_TRUE(d.coolingDown());

    // A first press after enough silence ends the pause and counts as a step.
    t += 120 + d.timing().cooldownMs;
    EXPECT_EQ(tap(d, Button::L1, t), Outcome::Progress);
    EXPECT_FALSE(d.coolingDown());
}

TEST_F(UnlockDetectorTest, RejectionsFarApartDoNotPause)
{
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(tap(d, Button::R2, t), Outcome::Wrong) << i;
        t += d.timing().rejectWindowMs / 2 + 1000;
    }
    EXPECT_FALSE(d.coolingDown());
    EXPECT_EQ(enterDefault(), Outcome::Unlocked);
}

TEST_F(UnlockDetectorTest, GivingUpHalfWayIsNotCountedAsNoise)
{
    for (int i = 0; i < 5; ++i) {
        tap(d, Button::L1, t);
        t += 120 + d.timing().maxGapMs + 1;
        EXPECT_EQ(d.poll(t), Outcome::Timeout);
        t += 1000;
    }
    EXPECT_FALSE(d.coolingDown());
}

TEST_F(UnlockDetectorTest, TheCooldownCanBeSwitchedOff)
{
    UnlockTiming timing;
    timing.rejectLimit = 0;
    d.configure(defaultSequence(), timing, t);
    t += 1000;
    for (int i = 0; i < 20; ++i) {
        EXPECT_EQ(tap(d, Button::R2, t), i == 0 ? Outcome::Wrong : Outcome::NotQuiet);
        t += 300;
    }
    EXPECT_FALSE(d.coolingDown());
}

TEST(UnlockDetectorRejections, Classification)
{
    EXPECT_FALSE(isRejection(Outcome::None));
    EXPECT_FALSE(isRejection(Outcome::Progress));
    EXPECT_FALSE(isRejection(Outcome::Unlocked));
    EXPECT_TRUE(isRejection(Outcome::Wrong));
    EXPECT_TRUE(isRejection(Outcome::TooLong));
    EXPECT_TRUE(isRejection(Outcome::Chord));
    EXPECT_TRUE(isRejection(Outcome::Timeout));
    EXPECT_TRUE(isRejection(Outcome::NotQuiet));
    EXPECT_TRUE(isRejection(Outcome::CoolingDown));
    EXPECT_FALSE(isRejection(Outcome::CooledDown));
}
