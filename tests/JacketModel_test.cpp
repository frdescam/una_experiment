#include <gtest/gtest.h>

#include "JacketModel.hpp"
#include "RideLock/UnlockDetector.hpp"

using namespace Jacket;
using RideLock::Button;
using RideLock::Input;

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
    const auto ev = toKernelEvents({ Press{ Button::R1, 100, 200 }, Press{ Button::L1, 1000, 1700 } }, 5000);
    ASSERT_EQ(ev.size(), 5u);
    EXPECT_EQ(ev[0].in, Input::Press);
    EXPECT_EQ(ev[1].in, Input::Click);     // short press: CLICK then RELEASE
    EXPECT_EQ(ev[2].in, Input::Release);
    EXPECT_EQ(ev[3].in, Input::Press);
    EXPECT_EQ(ev[4].in, Input::Release);   // 700 ms: no CLICK
}

TEST(JacketHarness, APressOnAButtonAlreadyDownIsDropped)
{
    const auto ev = toKernelEvents({ Press{ Button::R1, 100, 5000 }, Press{ Button::R1, 2000, 2100 } }, 10000);
    EXPECT_EQ(ev.size(), 2u);   // PRESS + RELEASE of the long one only
}

TEST(JacketHarness, FramePacingDeliversOnePerFrameAndDropsTheOldest)
{
    std::vector<Event> burst;
    for (uint32_t i = 0; i < 20; ++i) {
        burst.push_back(Event{ 10, Button::L1, Input::Click });
    }
    const auto out = paceToFrames(burst, 5000);
    EXPECT_EQ(out.size(), kKeyQueue);
    for (size_t i = 0; i < out.size(); ++i) {
        EXPECT_EQ(out[i].t, kFrameMs * (i + 1));
    }
}

TEST(JacketHarness, StockR1TwiceOpensAnActivity)
{
    const auto r = runStock(clicks({ Button::R1, Button::R1 }), 60000, StockOptions{});
    EXPECT_EQ(r.activityOpens, 1u);
    EXPECT_EQ(r.recordingsStarted, 0u);
    EXPECT_GT(r.gpsOnMs, 50000u);   // and it keeps searching
}

TEST(JacketHarness, StockR1FourTimesStartsARecording)
{
    const auto r = runStock(clicks({ Button::R1, Button::R1, Button::R1, Button::R1 }), 60000, StockOptions{});
    EXPECT_EQ(r.recordingsStarted, 1u);
}

TEST(JacketHarness, StockOpenedActivityStaysOnStartForEver)
{
    const auto r = runStock(clicks({ Button::R1, Button::R1 }), kHour, StockOptions{});
    EXPECT_GT(r.gpsOnMs, kHour - 10000);
}

TEST(JacketHarness, ThePatchedAppsGiveUpAfterTheirBudget)
{
    StockOptions o;
    o.preActivityBudgetMs = 5u * 60u * 1000u;
    const auto r = runStock(clicks({ Button::R1, Button::R1 }), kHour, o);
    EXPECT_EQ(r.activityOpens, 1u);
    EXPECT_LE(r.gpsOnMs, o.preActivityBudgetMs);
}

TEST(JacketHarness, TheDefaultSequenceUnlocksThroughTheFramePacing)
{
    const auto ev = paceToFrames(
        clicks({ Button::L1, Button::R1, Button::R2, Button::L2, Button::L1, Button::R1 }, 1000, 450), 10000);
    RideLock::UnlockDetector d;
    EXPECT_EQ(countFalseUnlocks(d, ev, 10000), 1u);
}

// --- the result that matters ---------------------------------------------------

class JacketModelTest : public ::testing::TestWithParam<Model>
{
};

TEST_P(JacketModelTest, TheDefaultLockSurvivesFiftyHoursOfIt)
{
    // 50 one-hour rides per model, fixed seeds so the test is repeatable.
    // jacket_report runs the long version and the comparisons.
    uint32_t unlocks = 0;
    uint32_t opens   = 0;
    for (uint64_t ride = 0; ride < 50; ++ride) {
        Rng rng(0x5EED0000u + ride * 977u + static_cast<uint64_t>(GetParam()));
        const auto kernel = toKernelEvents(generate(GetParam(), rng, kHour), kHour);
        RideLock::UnlockDetector d;
        unlocks += countFalseUnlocks(d, paceToFrames(kernel, kHour), kHour);
        opens   += runStock(kernel, kHour, StockOptions{}).activityOpens;
    }
    EXPECT_EQ(unlocks, 0u) << modelName(GetParam());
    RecordProperty("stock_activity_opens", static_cast<int>(opens));
}

INSTANTIATE_TEST_SUITE_P(AllModels, JacketModelTest, ::testing::ValuesIn(kAllModels));
