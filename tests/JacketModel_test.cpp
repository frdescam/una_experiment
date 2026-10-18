#include <gtest/gtest.h>

#include "JacketModel.hpp"
#include "ButtonLock/UnlockDetector.hpp"

using namespace Jacket;
using ButtonLock::Button;
using ButtonLock::Input;

namespace
{

constexpr uint32_t kHour = 3600u * 1000u;

std::vector<Event> clicks(std::initializer_list<Button> buttons, uint32_t t0 = 1000, uint32_t step = 800)
{
    std::vector<Press> p;
    uint32_t t = t0;
    for (Button b : buttons) {
        p.push_back(Press{ b, t, t + 100 });
        t += step;
    }
    return toKernelEvents(p, t + 1000);
}

} // namespace

// --- the harness itself --------------------------------------------------------

TEST(JacketHarness, KernelEventsFollowTheKernelsOrder)
{
    const auto ev = toKernelEvents({ Press{ Button::Select, 100, 200 }, Press{ Button::Up, 1000, 1700 } }, 5000);
    ASSERT_EQ(ev.size(), 5u);
    EXPECT_EQ(ev[0].in, Input::Press);
    EXPECT_EQ(ev[1].in, Input::Click);     // short press: CLICK then RELEASE
    EXPECT_EQ(ev[2].in, Input::Release);
    EXPECT_EQ(ev[3].in, Input::Press);
    EXPECT_EQ(ev[4].in, Input::Release);   // 700 ms: no CLICK
}

TEST(JacketHarness, APressOnAButtonAlreadyDownIsDropped)
{
    const auto ev = toKernelEvents({ Press{ Button::Select, 100, 5000 }, Press{ Button::Select, 2000, 2100 } }, 10000);
    EXPECT_EQ(ev.size(), 2u);   // PRESS + RELEASE of the long one only
}

TEST(JacketHarness, FramePacingDeliversOnePerFrameAndDropsTheOldest)
{
    std::vector<Event> burst;
    for (uint32_t i = 0; i < 20; ++i) {
        burst.push_back(Event{ 10, Button::Up, Input::Click });
    }
    const auto out = paceToFrames(burst, 5000);
    EXPECT_EQ(out.size(), kKeyQueue);
    for (size_t i = 0; i < out.size(); ++i) {
        EXPECT_EQ(out[i].t, kFrameMs * (i + 1));
    }
}

TEST(JacketHarness, StockSelectTwiceOpensAnActivity)
{
    const auto r = runStock(clicks({ Button::Select, Button::Select }), 60000, StockOptions{});
    EXPECT_EQ(r.activityOpens, 1u);
    EXPECT_EQ(r.recordingsStarted, 0u);
    EXPECT_GT(r.gpsOnMs, 50000u);   // and it keeps searching
}

TEST(JacketHarness, StockSelectFourTimesStartsARecording)
{
    const auto r = runStock(clicks({ Button::Select, Button::Select, Button::Select, Button::Select }), 60000, StockOptions{});
    EXPECT_EQ(r.recordingsStarted, 1u);
}

TEST(JacketHarness, StockOpenedActivityStaysOnStartForEver)
{
    const auto r = runStock(clicks({ Button::Select, Button::Select }), kHour, StockOptions{});
    EXPECT_GT(r.gpsOnMs, kHour - 10000);
}

TEST(JacketHarness, ThePatchedAppsGiveUpAfterTheirBudget)
{
    StockOptions o;
    o.preActivityBudgetMs = 5u * 60u * 1000u;
    const auto r = runStock(clicks({ Button::Select, Button::Select }), kHour, o);
    EXPECT_EQ(r.activityOpens, 1u);
    EXPECT_LE(r.gpsOnMs, o.preActivityBudgetMs);
}

TEST(JacketHarness, TheDefaultSequenceUnlocksThroughTheFramePacing)
{
    const auto ev = paceToFrames(clicks({ Button::Up, Button::Select, Button::Back, Button::Down }, 1000, 450), 10000);
    ButtonLock::UnlockDetector d;
    EXPECT_EQ(countFalseUnlocks(d, ev, 10000), 1u);
}

TEST(JacketHarness, AHoldToFinishSequenceUnlocksThroughTheFramePacing)
{
    ButtonLock::Sequence seq;
    ASSERT_TRUE(ButtonLock::parseSequence("UP SELECT BACK", seq));
    ButtonLock::UnlockTiming timing;
    timing.finalHoldMs = 1000;
    ButtonLock::UnlockDetector d(seq, timing);

    const std::vector<Press> p = { Press{ Button::Up, 1000, 1150 }, Press{ Button::Select, 1600, 1750 },
                                   Press{ Button::Back, 2200, 3400 } };
    const auto ev = paceToFrames(toKernelEvents(p, 10000), 10000);
    EXPECT_EQ(countFalseUnlocks(d, ev, 10000), 1u);

    // The same with a click at the end does not unlock.
    const std::vector<Press> q = { Press{ Button::Up, 1000, 1150 }, Press{ Button::Select, 1600, 1750 },
                                   Press{ Button::Back, 2200, 2350 } };
    EXPECT_EQ(countFalseUnlocks(d, paceToFrames(toKernelEvents(q, 10000), 10000), 10000), 0u);
}

TEST(JacketHarness, TheRateScalesHowOftenNoiseHappens)
{
    size_t full = 0, half = 0;
    for (uint64_t ride = 0; ride < 20; ++ride) {
        Rng a(ride), b(ride);
        full += generate(Model::RandomTaps, a, kHour).size();
        half += generate(Model::RandomTaps, b, kHour, 0.5).size();
    }
    EXPECT_NEAR(static_cast<double>(half) / static_cast<double>(full), 0.5, 0.05);
}

TEST(JacketHarness, TheEventDrivenLockedRideCountsLikeTheFrameLoop)
{
    // With no relock window, runLockedRide must decide exactly as the frame
    // loop of countFalseUnlocks does. Weak locks so there is plenty to count.
    ButtonLock::Sequence two, three;
    two.length = 2;                                   // shorter than the settings accept
    two.steps[0] = Button::Up;
    two.steps[1] = Button::Select;
    ASSERT_TRUE(ButtonLock::parseSequence("UP SELECT BACK", three));
    ButtonLock::UnlockTiming hold;
    hold.finalHoldMs = 800;
    LockedRideOptions immediate;
    immediate.relockMs   = 0;
    immediate.handBackMs = 0;

    uint32_t total = 0;
    for (Model m : kStudyModels) {
        for (uint64_t ride = 0; ride < 6; ++ride) {
            Rng rng(0xABCD00u + ride * 31u + static_cast<uint64_t>(m) * 1009u);
            const auto kernel    = toKernelEvents(generate(m, rng, kHour), kHour);
            const auto delivered = paceToFrames(kernel, kHour);
            for (const auto& [seq, timing] : { std::pair{ two, ButtonLock::UnlockTiming{} },
                                               std::pair{ three, ButtonLock::UnlockTiming{} },
                                               std::pair{ two, hold } }) {
                ButtonLock::UnlockDetector a(seq, timing), b(seq, timing);
                const uint32_t frameLoop = countFalseUnlocks(a, delivered, kHour);
                const auto     locked    = runLockedRide(b, delivered, kernel, kHour, immediate,
                                                         StockOptions{}, StockOptions{});
                EXPECT_EQ(locked.falseUnlocks, frameLoop) << modelName(m) << " ride " << ride;
                total += frameLoop;
            }
        }
    }
    EXPECT_GT(total, 50u);    // the comparison had something to compare
}

TEST(JacketHarness, AfterAFalseUnlockTheWatchFaceGetsTheNoiseUntilRelock)
{
    // The lock's sequence, then SELECT SELECT on the watch face (opens an
    // activity): one false unlock, one activity, and the lock back at relock.
    ButtonLock::UnlockDetector d;
    std::vector<Press> p;
    uint32_t t = 2000;
    for (uint8_t i = 0; i < d.length(); ++i) {
        p.push_back(Press{ d.sequence()[i], t, t + 100 });
        t += 400;
    }
    p.push_back(Press{ Button::Select, t + 5000, t + 5100 });
    p.push_back(Press{ Button::Select, t + 6000, t + 6100 });
    const auto kernel    = toKernelEvents(p, kHour);
    const auto delivered = paceToFrames(kernel, kHour);

    const auto r = runLockedRide(d, delivered, kernel, kHour, LockedRideOptions{}, StockOptions{}, StockOptions{});
    EXPECT_EQ(r.falseUnlocks, 1u);
    EXPECT_EQ(r.stock.activityOpens, 1u);
    EXPECT_EQ(r.exposedMs, LockedRideOptions{}.relockMs);
}

TEST(JacketHarness, RepeatedRejectionsMakeTheLockDeafForAWhile)
{
    // Three wrong taps 1 s apart, then silence: cooling down from the third
    // tap until 15 s after it.
    std::vector<Press> p = { Press{ Button::Back, 2000, 2100 }, Press{ Button::Back, 3000, 3100 },
                             Press{ Button::Back, 4000, 4100 } };
    const auto kernel    = toKernelEvents(p, 60000);
    const auto delivered = paceToFrames(kernel, 60000);
    ButtonLock::UnlockDetector d;
    const auto r = runLockedRide(d, delivered, kernel, 60000, LockedRideOptions{}, StockOptions{}, StockOptions{});
    EXPECT_EQ(r.falseUnlocks, 0u);
    EXPECT_NEAR(static_cast<double>(r.deafMs), 15000.0, 300.0);
}

// --- the result that matters ---------------------------------------------------

class JacketModelTest : public ::testing::TestWithParam<Model>
{
};

TEST_P(JacketModelTest, TheDefaultLockStaysFarAheadOfIt)
{
    // 200 one-hour rides per model, fixed seeds so the test is repeatable;
    // jacket_report and seq_study run the long versions. The default lock
    // averages up to about 21 false unlocks per 1000 h under the harshest
    // model (docs/sequence-study.md): here at most 6 in 200 h, against the
    // hundreds of activities the same noise opens on a stock watch. The bound
    // leaves room for chance. Removing the cool-down trips it (random taps
    // 13, chaos 42), as does removing the gap limit (random taps 76); the
    // quiet, chord and press-length rules have their own tests in
    // UnlockDetector_test.
    constexpr uint32_t kRides = 200;
    uint32_t unlocks = 0;
    uint32_t opens   = 0;
    for (uint64_t ride = 0; ride < kRides; ++ride) {
        Rng rng(0x5EED0000u + ride * 977u + static_cast<uint64_t>(GetParam()));
        const auto kernel = toKernelEvents(generate(GetParam(), rng, kHour), kHour);
        ButtonLock::UnlockDetector d;
        unlocks += countFalseUnlocks(d, paceToFrames(kernel, kHour), kHour);
        opens   += runStock(kernel, kHour, StockOptions{}).activityOpens;
    }
    EXPECT_LE(unlocks, 12u) << modelName(GetParam());
    RecordProperty("stock_activity_opens", static_cast<int>(opens));
}

INSTANTIATE_TEST_SUITE_P(AllModels, JacketModelTest, ::testing::ValuesIn(kAllModels));
