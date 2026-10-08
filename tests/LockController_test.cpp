#include <gtest/gtest.h>

#include "RideLock/LockController.hpp"

using namespace RideLock;

namespace
{

constexpr uint32_t kMin = 60u * 1000u;

LockPolicy policy(uint32_t relockMs, bool lockAtBoot)
{
    LockPolicy p;
    p.relockMs   = relockMs;
    p.lockAtBoot = lockAtBoot;
    return p;
}

} // namespace

TEST(LockController, BootLocksAfterTheStartupGrace)
{
    LockController c(policy(2 * kMin, true));
    c.start(1000);
    EXPECT_EQ(c.state(), LockState::Starting);
    EXPECT_EQ(c.poll(1000 + 4999), LockAction::None);
    EXPECT_EQ(c.msUntilNextDeadline(1000 + 4000), 1000u);

    EXPECT_EQ(c.poll(1000 + 5000), LockAction::ShowLock);
    EXPECT_EQ(c.state(), LockState::Requesting);
    EXPECT_EQ(c.poll(1000 + 5100), LockAction::None);   // asked once

    c.onGuiRun(1000 + 5300);
    EXPECT_TRUE(c.isLocked());
    EXPECT_EQ(c.poll(1000 + 99999), LockAction::None);
    EXPECT_EQ(c.msUntilNextDeadline(1000 + 99999), LockController::kNoDeadline);
}

TEST(LockController, OpeningTheAppLocksAtOnce)
{
    LockController c(policy(2 * kMin, true));
    c.start(0);
    c.onGuiRun(300);
    EXPECT_TRUE(c.isLocked());
    EXPECT_EQ(c.poll(10000), LockAction::None);   // no second request after the grace
}

TEST(LockController, UnlockThenRelockAfterTheConfiguredTime)
{
    LockController c(policy(2 * kMin, true));
    c.start(0);
    c.onGuiRun(300);

    c.onUnlocked(10000);
    EXPECT_EQ(c.state(), LockState::Unlocked);
    // The GUI is still closing: wait for it, whatever the time.
    EXPECT_EQ(c.poll(10000 + 3 * kMin), LockAction::None);
    EXPECT_EQ(c.msUntilNextDeadline(10050), LockController::kNoDeadline);

    c.onGuiStop(10200);
    EXPECT_EQ(c.poll(10200), LockAction::None);
    EXPECT_EQ(c.msUntilNextDeadline(10200), 2 * kMin - 200);
    EXPECT_EQ(c.poll(10000 + 2 * kMin - 1), LockAction::None);
    EXPECT_EQ(c.poll(10000 + 2 * kMin), LockAction::ShowLock);

    c.onGuiRun(10000 + 2 * kMin + 400);
    EXPECT_TRUE(c.isLocked());
}

TEST(LockController, WithoutRelockTheServiceLeavesOnceTheGuiIsGone)
{
    LockController c(policy(0, true));
    c.start(0);
    c.onGuiRun(300);
    c.onUnlocked(5000);
    EXPECT_EQ(c.poll(5001), LockAction::None);        // never while a GUI is loaded
    c.onGuiStop(5100);
    EXPECT_EQ(c.msUntilNextDeadline(5100), 0u);
    EXPECT_EQ(c.poll(5100), LockAction::Exit);
}

TEST(LockController, BootWithoutLockAtBootAndWithoutRelockExits)
{
    LockController c(policy(0, false));
    c.start(0);
    EXPECT_EQ(c.poll(4000), LockAction::None);
    EXPECT_EQ(c.poll(5000), LockAction::Exit);
}

TEST(LockController, BootWithoutLockAtBootStillRelocksLater)
{
    LockController c(policy(5 * kMin, false));
    c.start(0);
    EXPECT_EQ(c.poll(5000), LockAction::None);
    EXPECT_EQ(c.state(), LockState::Unlocked);
    EXPECT_EQ(c.poll(5000 + 5 * kMin), LockAction::ShowLock);
}

TEST(LockController, AGuiLostWithoutUnlockComesBackNoFasterThanTheRetryFloor)
{
    LockPolicy p = policy(30 * 1000, true);   // relock shorter than the floor
    LockController c(p);
    c.start(0);
    c.onGuiRun(100);
    c.onGuiStop(2000);                        // killed, not unlocked
    EXPECT_EQ(c.state(), LockState::Unlocked);
    EXPECT_EQ(c.poll(2000 + 30 * 1000), LockAction::None);
    EXPECT_EQ(c.poll(2000 + p.retryFloorMs), LockAction::ShowLock);
}

TEST(LockController, AGuiLostWithoutUnlockAndNoRelockExits)
{
    LockController c(policy(0, true));
    c.start(0);
    c.onGuiRun(100);
    c.onGuiStop(2000);
    EXPECT_EQ(c.poll(2000), LockAction::Exit);
}

TEST(LockController, AFailedRequestIsRetriedSlowlyThenAbandoned)
{
    LockPolicy p = policy(0, true);
    LockController c(p);
    c.start(0);
    uint32_t t = 5000;
    ASSERT_EQ(c.poll(t), LockAction::ShowLock);

    for (int attempt = 1; attempt < 3; ++attempt) {
        t += p.requestTimeoutMs;
        EXPECT_EQ(c.poll(t), LockAction::None) << attempt;   // gave up on this one
        EXPECT_EQ(c.poll(t + p.retryFloorMs - 1), LockAction::None) << attempt;
        t += p.retryFloorMs;
        EXPECT_EQ(c.poll(t), LockAction::ShowLock) << attempt;
    }
    t += p.requestTimeoutMs;
    EXPECT_EQ(c.poll(t), LockAction::Exit);
}

TEST(LockController, AFailedRequestWithRelockWaitsARelockPeriod)
{
    LockPolicy p = policy(10 * kMin, true);
    LockController c(p);
    c.start(0);
    ASSERT_EQ(c.poll(5000), LockAction::ShowLock);
    EXPECT_EQ(c.poll(5000 + p.requestTimeoutMs), LockAction::None);
    EXPECT_EQ(c.poll(5000 + p.requestTimeoutMs + 10 * kMin), LockAction::ShowLock);
}

TEST(LockController, ReopeningTheAppWhileUnlockedLocksAndCancelsTheRelock)
{
    LockController c(policy(2 * kMin, true));
    c.start(0);
    c.onGuiRun(100);
    c.onUnlocked(1000);
    c.onGuiStop(1200);
    c.onGuiRun(30000);                       // user opened it from the launcher
    EXPECT_TRUE(c.isLocked());
    EXPECT_EQ(c.poll(1000 + 2 * kMin), LockAction::None);
}

TEST(LockController, NeverExitsWhileAGuiIsLoaded)
{
    LockController c(policy(0, false));
    c.start(0);
    c.onGuiRun(100);
    for (uint32_t t = 0; t < 10 * kMin; t += 997) {
        ASSERT_NE(c.poll(t), LockAction::Exit) << t;
    }
}

TEST(LockController, DeadlinesSurviveTheTickWrap)
{
    LockController c(policy(2 * kMin, true));
    const uint32_t t0 = 0xFFFFFFFFu - 1000u;
    c.start(t0);
    c.onGuiRun(t0 + 10);
    c.onUnlocked(t0 + 20);
    c.onGuiStop(t0 + 30);
    EXPECT_EQ(c.poll(t0 + 30), LockAction::None);
    EXPECT_EQ(c.msUntilNextDeadline(t0 + 30), 2 * kMin - 10);
    EXPECT_EQ(c.poll(t0 + 20 + 2 * kMin), LockAction::ShowLock);
}

TEST(LockController, AHiddenLockComesBackToTheFront)
{
    LockPolicy p = policy(2 * kMin, true);
    LockController c(p);
    c.start(0);
    c.onGuiRun(100);
    c.onGuiShown(150);

    c.onGuiHidden(10000);                     // a sleeve found a way out
    EXPECT_TRUE(c.guiHidden());
    EXPECT_EQ(c.msUntilNextDeadline(10000), 2 * kMin);
    EXPECT_EQ(c.poll(10000 + 2 * kMin - 1), LockAction::None);
    EXPECT_EQ(c.poll(10000 + 2 * kMin), LockAction::ShowLock);
    EXPECT_TRUE(c.isLocked());                // still locked: only brought to the front

    // Still hidden (the request did nothing): asked again a period later.
    EXPECT_EQ(c.poll(10000 + 3 * kMin), LockAction::None);
    EXPECT_EQ(c.poll(10000 + 4 * kMin), LockAction::ShowLock);

    c.onGuiShown(10000 + 4 * kMin + 300);
    EXPECT_EQ(c.poll(10000 + 10 * kMin), LockAction::None);
    EXPECT_EQ(c.msUntilNextDeadline(10000 + 10 * kMin), LockController::kNoDeadline);
}

TEST(LockController, ABriefInterruptionIsLeftAlone)
{
    LockController c(policy(2 * kMin, true));
    c.start(0);
    c.onGuiRun(100);
    c.onGuiHidden(5000);                      // a notification
    c.onGuiShown(9000);                       // gone again
    EXPECT_EQ(c.poll(5000 + 2 * kMin), LockAction::None);
}

TEST(LockController, WithoutRelockAHiddenLockIsNotForcedBack)
{
    LockController c(policy(0, true));
    c.start(0);
    c.onGuiRun(100);
    c.onGuiHidden(5000);
    EXPECT_EQ(c.poll(5000 + 60 * kMin), LockAction::None);
    EXPECT_EQ(c.msUntilNextDeadline(5000), LockController::kNoDeadline);
}

TEST(LockController, HiddenOnlyCountsWhileLoaded)
{
    LockController c(policy(2 * kMin, true));
    c.start(0);
    c.onGuiHidden(100);                       // no GUI yet: ignored
    EXPECT_FALSE(c.guiHidden());
    c.onGuiRun(200);
    c.onGuiHidden(300);
    c.onUnlocked(400);                        // unlocking clears it
    EXPECT_FALSE(c.guiHidden());
}
