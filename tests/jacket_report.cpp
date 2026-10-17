/**
 * @file    jacket_report.cpp
 * @brief   Prints the jacket-model comparison used in docs/STUDY.md.
 *
 *   jacket_report [hours-per-model] [seed]
 *
 * For every jacket model it simulates that many one-hour rides and reports:
 *   - on the stock watch: activities opened, recordings started, and the
 *     share of the ride with the GPS on;
 *   - with the activity-app patch (5 min pre-activity budget, and optionally
 *     a 1.5 s hold to start): recordings started and GPS-on share;
 *   - with ButtonLock: false unlocks, for the default lock and for variants
 *     with one of its rules switched off (the ablation).
 */

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <vector>

#include "JacketModel.hpp"
#include "ButtonLock/UnlockDetector.hpp"

using namespace Jacket;

namespace
{

constexpr uint32_t kHour = 3600u * 1000u;

struct Variant {
    const char*            name;
    ButtonLock::Sequence     seq;
    ButtonLock::UnlockTiming timing;
};

/// A sequence from its steps, as the detector takes it. Not through
/// parseSequence(): the report also measures codes the settings refuse.
ButtonLock::Sequence steps(std::initializer_list<Button> list)
{
    ButtonLock::Sequence s;
    for (Button b : list) {
        s.steps[s.length++] = b;
    }
    return s;
}

std::vector<Variant> variants()
{
    using B = Button;
    const ButtonLock::UnlockTiming def;

    ButtonLock::UnlockTiming noQuiet = def;
    noQuiet.quietMs = 0;

    ButtonLock::UnlockTiming noChord = def;
    noChord.stuckMs = 0;          // every other button counts as pinned: chords pass

    ButtonLock::UnlockTiming noCooldown = def;
    noCooldown.rejectLimit = 0;

    ButtonLock::UnlockTiming naive;  // any order-correct clicks, any timing
    naive.quietMs    = 0;
    naive.stuckMs    = 0;
    naive.maxGapMs   = 0xFFFFFFFFu;
    naive.maxPressMs = 0xFFFFFFFFu;
    naive.rejectLimit = 0;

    const ButtonLock::Sequence six   = steps({ B::Up, B::Select, B::Back, B::Down, B::Up, B::Select });
    const ButtonLock::Sequence five  = steps({ B::Up, B::Select, B::Back, B::Down, B::Up });
    const ButtonLock::Sequence four  = steps({ B::Up, B::Select, B::Back, B::Down });
    const ButtonLock::Sequence three = steps({ B::Up, B::Select, B::Back });
    const ButtonLock::Sequence two   = steps({ B::Up, B::Select });
    return {
        { "ButtonLock default (UP SELECT BACK DOWN)", ButtonLock::defaultSequence(), def },
        { "  without the cool-down",           ButtonLock::defaultSequence(), noCooldown },
        { "  without the quiet rule",          ButtonLock::defaultSequence(), noQuiet },
        { "  without the chord rule",          ButtonLock::defaultSequence(), noChord },
        { "  6 steps (UP SELECT BACK DOWN UP SELECT, the first default)", six, def },
        { "  5 steps (UP SELECT BACK DOWN UP)", five,                  def     },
        { "  3 steps (UP SELECT BACK)",        three,                  def     },
        { "    3 steps without the cool-down", three,                  noCooldown },
        { "  2 steps (UP SELECT, refused by the settings)", two,       def     },
        { "Naive 6-click lock (no rules)",     six,                    naive   },
        { "Naive 4-click lock (no rules)",     four,                   naive   },
        { "Naive 3-click lock (no rules)",     three,                  naive   },
        { "Naive 2-click lock (no rules)",     two,                    naive   },
    };
}

} // namespace

int main(int argc, char** argv)
{
    const uint32_t hours = argc > 1 ? static_cast<uint32_t>(std::atoi(argv[1])) : 500u;
    const uint64_t seed  = argc > 2 ? std::strtoull(argv[2], nullptr, 0) : 0xC0FFEEu;

    StockOptions stock;
    StockOptions patched;
    patched.preActivityBudgetMs = 5u * 60u * 1000u;
    StockOptions patchedHold = patched;
    patchedHold.holdToStartMs = 1500;
    patchedHold.holdAlone     = true;

    const auto vars = variants();

    std::printf("Jacket model: %u one-hour rides per model, seed 0x%llx\n\n",
                hours, static_cast<unsigned long long>(seed));

    std::printf("| Jacket model | Stock: activities opened /h | Stock: recordings started /h "
                "| Stock: GPS on | Budget patch: GPS on | Budget + hold-to-start: recordings /h "
                "| Budget + hold-to-start: GPS on | ButtonLock: false unlocks |\n");
    std::printf("|---|---:|---:|---:|---:|---:|---:|---:|\n");

    std::vector<std::vector<uint32_t>> ablation(vars.size(), std::vector<uint32_t>(kAllModels.size(), 0));

    for (size_t m = 0; m < kAllModels.size(); ++m) {
        const Model model = kAllModels[m];
        uint64_t opens = 0, recs = 0, gpsStock = 0, gpsPatched = 0, recsHold = 0, gpsHold = 0;

        for (uint32_t ride = 0; ride < hours; ++ride) {
            Rng rng(seed + ride * 7919u + m * 104729u);
            const auto kernel    = toKernelEvents(generate(model, rng, kHour), kHour);
            const auto delivered = paceToFrames(kernel, kHour);

            const StockResult s = runStock(kernel, kHour, stock);
            opens    += s.activityOpens;
            recs     += s.recordingsStarted;
            gpsStock += s.gpsOnMs;
            gpsPatched += runStock(kernel, kHour, patched).gpsOnMs;
            const StockResult h = runStock(kernel, kHour, patchedHold);
            recsHold += h.recordingsStarted;
            gpsHold  += h.gpsOnMs;

            for (size_t v = 0; v < vars.size(); ++v) {
                ButtonLock::UnlockDetector d(vars[v].seq, vars[v].timing);
                ablation[v][m] += countFalseUnlocks(d, delivered, kHour);
            }
        }

        const double h = static_cast<double>(hours);
        std::printf("| %s | %.1f | %.2f | %.0f %% | %.0f %% | %.2f | %.0f %% | %u in %u h |\n",
                    modelName(model), opens / h, recs / h,
                    100.0 * gpsStock / (h * kHour), 100.0 * gpsPatched / (h * kHour),
                    recsHold / h, 100.0 * gpsHold / (h * kHour),
                    ablation[0][m], hours);
    }

    std::printf("\nFalse unlocks in %u hours, per lock variant (ablation):\n\n", hours);
    std::printf("| Lock variant |");
    for (Model model : kAllModels) {
        std::printf(" %s |", modelName(model));
    }
    std::printf("\n|---|");
    for (size_t m = 0; m < kAllModels.size(); ++m) {
        std::printf("---:|");
    }
    std::printf("\n");
    for (size_t v = 0; v < vars.size(); ++v) {
        std::printf("| %s |", vars[v].name);
        for (size_t m = 0; m < kAllModels.size(); ++m) {
            std::printf(" %u |", ablation[v][m]);
        }
        std::printf("\n");
    }
    std::printf("\nA 0 is not \"never\": it means below about %.1f per 1000 h, at 95 %% confidence.\n",
                3000.0 / hours);
    return 0;
}
