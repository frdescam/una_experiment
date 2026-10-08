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
 *   - with RideLock: false unlocks, for the default lock and for variants
 *     with one of its rules switched off (the ablation).
 */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "JacketModel.hpp"
#include "RideLock/UnlockDetector.hpp"

using namespace Jacket;

namespace
{

constexpr uint32_t kHour = 3600u * 1000u;

struct Variant {
    const char*            name;
    RideLock::Sequence     seq;
    RideLock::UnlockTiming timing;
};

RideLock::Sequence seq(const char* text)
{
    RideLock::Sequence s;
    if (!RideLock::parseSequence(text, s)) {
        std::fprintf(stderr, "bad sequence %s\n", text);
        std::exit(2);
    }
    return s;
}

std::vector<Variant> variants()
{
    const RideLock::UnlockTiming def;

    RideLock::UnlockTiming noQuiet = def;
    noQuiet.quietMs = 0;

    RideLock::UnlockTiming noChord = def;
    noChord.stuckMs = 0;          // every other button counts as pinned: chords pass

    RideLock::UnlockTiming noCooldown = def;
    noCooldown.rejectLimit = 0;

    RideLock::UnlockTiming naive;  // any order-correct clicks, any timing
    naive.quietMs    = 0;
    naive.stuckMs    = 0;
    naive.maxGapMs   = 0xFFFFFFFFu;
    naive.maxPressMs = 0xFFFFFFFFu;
    naive.rejectLimit = 0;

    const RideLock::Sequence four = seq("L1 R1 R2 L2");
    return {
        { "RideLock default (L1 R1 R2 L2 L1 R1)", RideLock::defaultSequence(), def },
        { "  without the cool-down",          RideLock::defaultSequence(), noCooldown },
        { "  without the quiet rule",         RideLock::defaultSequence(), noQuiet },
        { "  without the chord rule",         RideLock::defaultSequence(), noChord },
        { "  5 steps (L1 R1 R2 L2 L1)",       seq("L1 R1 R2 L2 L1"),       def     },
        { "  4 steps (L1 R1 R2 L2)",          four,                        def     },
        { "    4 steps without the cool-down", four,                       noCooldown },
        { "    4 steps without the quiet rule", four,                      noQuiet },
        { "    4 steps without the chord rule", four,                      noChord },
        { "  2 steps (L1 R1)",                seq("L1 R1"),                def     },
        { "Naive 6-click lock (no rules)",    RideLock::defaultSequence(), naive   },
        { "Naive 4-click lock (no rules)",    four,                        naive   },
        { "Naive 2-click lock (no rules)",    seq("L1 R1"),                naive   },
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
                "| Budget + hold-to-start: GPS on | RideLock: false unlocks |\n");
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
                RideLock::UnlockDetector d(vars[v].seq, vars[v].timing);
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
    return 0;
}
