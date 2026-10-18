/**
 * @file    seq_study.cpp
 * @brief   How short can the unlock sequence be? Monte Carlo study
 *          (results in docs/sequence-study.md).
 *
 *   seq_study [hostile-hours] [calibrated-hours] [threads] [seed]
 *
 * Defaults: 10000 h per model at each level, every core, seed 0x5e9. Build it
 * with -DCMAKE_BUILD_TYPE=Release: about 2 min on 4 cores (a Debug build is
 * some ten times slower).
 *
 * For each lock variant (6, 5 and 4 steps, other 4-step shapes, every 3-step
 * code, 2 steps, 1 tap + hold, 2 taps + hold, naive locks) and each jacket
 * model it replays one-hour rides at two noise levels:
 *
 *   harsh       the models as written (hostile-hours per model);
 *   calibrated  each model's event rate scaled down until the stock watch
 *               opens about one activity per hour (calibrated-hours per model);
 *
 * and, for the candidate locks only, at 2x and 4x the calibrated rate (the
 * calibration band), plus a rate sweep of the random-taps model. It counts, per
 * 1000 h: false unlocks (all rules on), and what the noise then does on the
 * watch face while the lock is down (2 min until relock): activities opened
 * (GPS on) and recordings started, on the stock apps and on the patched apps
 * (5 min budget, 1.5 s hold of SELECT alone to start). It also simulates a
 * person unlocking (bare hands and gloves) through the same detector, and the
 * share of time the cool-down makes the lock deaf.
 *
 * Every Sequence is built directly, not through parseSequence(), so codes the
 * settings refuse (2 steps, codes on two buttons) are measured too; the output
 * says which ones the settings accept.
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "JacketModel.hpp"
#include "ButtonLock/Sequence.hpp"
#include "ButtonLock/UnlockDetector.hpp"

using namespace Jacket;
using ButtonLock::Button;
using ButtonLock::Outcome;
using ButtonLock::Sequence;
using ButtonLock::UnlockTiming;

namespace
{

constexpr uint32_t kHour = 3600u * 1000u;

// --- seeds -----------------------------------------------------------------------

uint64_t mix(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

/// Streams: 1-4 the four noise levels, 7 the calibration, 16 and up the rate sweep.
uint64_t rideSeed(uint64_t seed, unsigned stream, Model m, uint64_t ride)
{
    return mix(seed ^ mix((static_cast<uint64_t>(stream) << 40) ^ (static_cast<uint64_t>(m) << 32) ^ ride));
}

/// One person's attempt. The variant and the hand profile sit in the high
/// bits (with a tag no ride stream uses), the trial in the low 32, so no two
/// (variant, profile, trial) share a random stream.
uint64_t humanSeed(uint64_t seed, size_t variant, size_t profile, uint32_t trial)
{
    return mix(seed ^ mix((0xA5ull << 56) ^ (static_cast<uint64_t>(variant) << 40) ^
                          (static_cast<uint64_t>(profile) << 32) ^ trial));
}

// --- names -----------------------------------------------------------------------

std::string codeText(const Sequence& s)
{
    char buf[80];
    ButtonLock::formatSequence(s, buf, sizeof(buf));
    return buf;
}

const char* shortModel(Model m)
{
    switch (m) {
        case Model::RandomTaps:     return "Taps";
        case Model::OneSide:        return "OneSide";
        case Model::Bumps:          return "Bumps";
        case Model::Squeeze:        return "Squeeze";
        case Model::PinnedPlusTaps: return "Pinned";
        case Model::Chaos:          return "Chaos";
        case Model::TapsAndHolds:   return "Taps+Holds";
        case Model::Rocker:         return "Rocker";
    }
    return "?";
}

/// For a 3-step code: ABA row (A and B on the same row), ABA diagonal, ABA
/// side (both on one side), or ABC with each move marked - (across a row),
/// x (diagonal), | (along a side).
std::string shapeOf(const Sequence& s)
{
    auto row  = [](Button b) { return (b == Button::Up || b == Button::Select) ? 0 : 1; };
    auto side = [](Button b) { return (b == Button::Up || b == Button::Down) ? 0 : 1; };
    std::string out;
    for (size_t i = 1; i < s.length; ++i) {
        const Button a = s[i - 1], b = s[i];
        out += (side(a) == side(b)) ? '|' : (row(a) == row(b) ? '-' : 'x');
    }
    if (s.length == 3 && s[0] == s[2]) {
        return out[0] == '-' ? "ABA row" : (out[0] == 'x' ? "ABA diagonal" : "ABA side");
    }
    return s.length == 3 ? "ABC " + out : out;
}

size_t distinctButtons(const Sequence& s)
{
    bool seen[ButtonLock::kButtonCount] = {};
    size_t n = 0;
    for (size_t i = 0; i < s.length; ++i) {
        bool& b = seen[static_cast<size_t>(s[i])];
        if (!b) {
            b = true;
            ++n;
        }
    }
    return n;
}

// --- variants --------------------------------------------------------------------

enum class Family : uint8_t { Six, Five, Four, Three, Two, TapHold, TwoTapsHold, Naive };

struct Variant {
    std::string  label;
    Family       family;
    uint32_t     holdMs;
    Sequence     seq;
    UnlockTiming timing;
    const char*  settings;   ///< accepted / refused by the settings, not offered (holds), - (naive)

    uint32_t    taps() const { return holdMs > 0 ? seq.length - 1u : seq.length; }
    std::string presses() const
    {
        if (holdMs == 0) {
            return std::to_string(seq.length);
        }
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%u (%u tap%s + hold)", static_cast<unsigned>(seq.length), taps(),
                      taps() == 1 ? "" : "s");
        return buf;
    }
    bool threeButtons() const { return family != Family::Naive && distinctButtons(seq) >= 3; }
};

/// A sequence from its steps, as the detector takes it (see jacket_report.cpp).
Sequence steps(std::initializer_list<Button> list)
{
    Sequence s;
    for (Button b : list) {
        s.steps[s.length++] = b;
    }
    return s;
}

bool sameCode(const Sequence& a, const Sequence& b)
{
    if (a.length != b.length) {
        return false;
    }
    for (size_t i = 0; i < a.length; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

Sequence firstDefault()
{
    using B = Button;
    return steps({ B::Up, B::Select, B::Back, B::Down, B::Up, B::Select });
}

/// What the app's settings say, by asking its parser.
bool settingsAccept(const Sequence& s)
{
    Sequence out;
    return ButtonLock::parseSequence(codeText(s).c_str(), out);
}

/// Every code of @p len steps with no button twice in a row (a repeat is what
/// one bouncing button does; the settings never allowed it).
std::vector<Sequence> allCodes(size_t len)
{
    static constexpr Button kOrder[4] = { Button::Up, Button::Select, Button::Back, Button::Down };
    size_t total = 1;
    for (size_t i = 0; i < len; ++i) {
        total *= 4;
    }
    std::vector<Sequence> out;
    for (size_t code = 0; code < total; ++code) {
        Sequence s;
        size_t div = total / 4;
        bool ok = true;
        for (size_t i = 0; i < len; ++i, div /= 4) {
            const Button b = kOrder[(code / div) % 4];
            if (s.length > 0 && s[s.length - 1] == b) {
                ok = false;
            }
            s.steps[s.length++] = b;
        }
        if (ok) {
            out.push_back(s);
        }
    }
    return out;
}

UnlockTiming naiveTiming()   // any order-correct clicks, any timing
{
    UnlockTiming t;
    t.quietMs     = 0;
    t.stuckMs     = 0;
    t.maxGapMs    = 0xFFFFFFFFu;
    t.maxPressMs  = 0xFFFFFFFFu;
    t.rejectLimit = 0;
    return t;
}

std::vector<Variant> makeVariants()
{
    using B = Button;
    std::vector<Variant> v;
    const UnlockTiming rules;   // all of ButtonLock's rules, as shipped
    const Sequence def = ButtonLock::defaultSequence();

    auto add = [&](Family f, const Sequence& s, uint32_t hold, const UnlockTiming& base) {
        UnlockTiming t = base;
        t.finalHoldMs = hold;
        std::string label;
        const char* settings;
        if (f == Family::Naive) {
            label    = "Naive " + std::to_string(s.length) + "-click " + codeText(s) + " (no rules)";
            settings = "-";
        } else if (hold > 0) {
            Sequence prefix = s;
            --prefix.length;
            char buf[16];
            std::snprintf(buf, sizeof(buf), " (%.1f s)", hold / 1000.0);
            label    = codeText(prefix) + ", then hold " + ButtonLock::buttonName(s[s.length - 1]) + buf;
            settings = "not offered";
        } else {
            label = codeText(s);
            if (sameCode(s, def)) {
                label += " [default]";
            }
            if (sameCode(s, firstDefault())) {
                label += " [first default]";
            }
            settings = settingsAccept(s) ? "accepted" : "refused";
        }
        v.push_back(Variant{ label, f, hold, s, t, settings });
    };

    add(Family::Six, firstDefault(), 0, rules);
    add(Family::Five, steps({ B::Up, B::Select, B::Back, B::Down, B::Up }), 0, rules);
    add(Family::Four, def, 0, rules);
    for (const Sequence& s : { steps({ B::Up, B::Select, B::Back, B::Down }), steps({ B::Up, B::Back, B::Down, B::Select }),
                               steps({ B::Up, B::Back, B::Select, B::Down }), steps({ B::Up, B::Select, B::Down, B::Back }),
                               steps({ B::Up, B::Down, B::Select, B::Back }), steps({ B::Up, B::Select, B::Up, B::Select }),
                               steps({ B::Up, B::Back, B::Up, B::Back }) }) {
        if (!sameCode(s, def)) {
            add(Family::Four, s, 0, rules);
        }
    }
    for (const Sequence& s : allCodes(3)) {
        add(Family::Three, s, 0, rules);
    }
    for (const Sequence& s : { steps({ B::Up, B::Select }), steps({ B::Up, B::Back }), steps({ B::Up, B::Down }) }) {
        add(Family::Two, s, 0, rules);
    }
    for (uint32_t hold : { 800u, 1000u }) {
        for (const Sequence& s : allCodes(2)) {
            add(Family::TapHold, s, hold, rules);
        }
    }
    for (uint32_t hold : { 800u, 1000u }) {
        for (const Sequence& s : allCodes(3)) {
            add(Family::TwoTapsHold, s, hold, rules);
        }
    }
    add(Family::Naive, steps({ B::Up, B::Select, B::Back }), 0, naiveTiming());
    add(Family::Naive, def, 0, naiveTiming());
    return v;
}

size_t findVariant(const std::vector<Variant>& v, Family f, const Sequence& s, uint32_t hold)
{
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i].family == f && v[i].holdMs == hold && sameCode(v[i].seq, s)) {
            return i;
        }
    }
    std::fprintf(stderr, "no variant %s\n", codeText(s).c_str());
    std::exit(2);
}

// --- statistics ------------------------------------------------------------------

double logPoissonCdf(uint64_t k, double lam)   // log P(X <= k)
{
    double maxTerm = -1e300;
    std::vector<double> terms;
    terms.reserve(k + 1);
    for (uint64_t i = 0; i <= k; ++i) {
        const double lt = -lam + static_cast<double>(i) * std::log(lam) - std::lgamma(static_cast<double>(i) + 1.0);
        terms.push_back(lt);
        maxTerm = std::max(maxTerm, lt);
    }
    double sum = 0.0;
    for (double lt : terms) {
        sum += std::exp(lt - maxTerm);
    }
    return maxTerm + std::log(sum);
}

struct Interval {
    double lo;
    double hi;
};

/// Exact (Garwood) 95% interval for a Poisson count; Wilson-Hilferty above 2000.
/// A zero count gets the 3/N rule (one-sided 95%).
Interval poisson95(uint64_t k)
{
    if (k == 0) {
        return { 0.0, 3.0 };
    }
    const double kd = static_cast<double>(k);
    if (k > 2000) {
        const double z  = 1.959964;
        const double lo = kd * std::pow(1.0 - 1.0 / (9.0 * kd) - z / (3.0 * std::sqrt(kd)), 3.0);
        const double k1 = kd + 1.0;
        const double hi = k1 * std::pow(1.0 - 1.0 / (9.0 * k1) + z / (3.0 * std::sqrt(k1)), 3.0);
        return { lo, hi };
    }
    const double target = std::log(0.025);
    double a = kd, b = kd * 3.0 + 10.0;   // upper: P(X <= k; hi) = 0.025
    for (int it = 0; it < 80; ++it) {
        const double m = 0.5 * (a + b);
        (logPoissonCdf(k, m) > target ? a : b) = m;
    }
    const double hi = 0.5 * (a + b);
    const double target2 = std::log(0.975);   // lower: P(X <= k-1; lo) = 0.975
    a = 0.0;
    b = kd;
    for (int it = 0; it < 80; ++it) {
        const double m = 0.5 * (a + b);
        (m <= 0.0 || logPoissonCdf(k - 1, m) > target2 ? a : b) = m;
    }
    return { 0.5 * (a + b), hi };
}

std::string num(double x)
{
    char buf[32];
    if (x == 0.0) {
        return "0";
    }
    if (x >= 100.0) {
        std::snprintf(buf, sizeof(buf), "%.0f", x);
    } else if (x >= 10.0) {
        std::snprintf(buf, sizeof(buf), "%.1f", x);
    } else if (x >= 1.0) {
        std::snprintf(buf, sizeof(buf), "%.2f", x);
    } else if (x >= 0.001) {
        std::snprintf(buf, sizeof(buf), "%.2g", x);
    } else {
        std::snprintf(buf, sizeof(buf), "%.1e", x);
    }
    return buf;
}

/// A count as a rate per @p per hours of noise: "r", or with @p ci "r (lo-hi)"
/// (Poisson 95%). A zero is always "0 (<u)": u is the 3/N upper bound.
std::string rateOf(uint64_t k, double hours, double per, bool ci)
{
    const double   scale = per / hours;
    const Interval i     = poisson95(k);
    if (k == 0) {
        return "0 (<" + num(i.hi * scale) + ")";
    }
    const std::string r = num(static_cast<double>(k) * scale);
    return ci ? r + " (" + num(i.lo * scale) + "-" + num(i.hi * scale) + ")" : r;
}

std::string rate(uint64_t k, double hours, bool ci = false) { return rateOf(k, hours, 1000.0, ci); }

/// Per hour with two decimals (the no-lock baselines); a zero gets its 3/N bound.
std::string perHour(uint64_t k, double hours)
{
    if (k == 0) {
        return rateOf(0, hours, 1.0, false);
    }
    char buf[32];
    const double r = static_cast<double>(k) / hours;
    std::snprintf(buf, sizeof(buf), r >= 0.01 ? "%.2f" : "%.4f", r);
    return buf;
}

/// A share of @p n trials in percent; a zero gets its 3/N bound.
std::string percent(uint64_t k, uint64_t n)
{
    char buf[48];
    if (k == 0) {
        std::snprintf(buf, sizeof(buf), "0 (<%.3g %%)", 300.0 / static_cast<double>(n));
    } else {
        std::snprintf(buf, sizeof(buf), "%.2f %%", 100.0 * static_cast<double>(k) / static_cast<double>(n));
    }
    return buf;
}

// --- simulation ------------------------------------------------------------------

struct Cell {
    uint64_t unlocks = 0, opens = 0, recs = 0, pOpens = 0, pRecs = 0;
    uint64_t exposedMs = 0, deafMs = 0, gpsMs = 0, pGpsMs = 0;

    void add(const LockedRideResult& r)
    {
        unlocks   += r.falseUnlocks;
        opens     += r.stock.activityOpens;
        recs      += r.stock.recordingsStarted;
        gpsMs     += r.stock.gpsOnMs;
        pOpens    += r.patched.activityOpens;
        pRecs     += r.patched.recordingsStarted;
        pGpsMs    += r.patched.gpsOnMs;
        exposedMs += r.exposedMs;
        deafMs    += r.deafMs;
    }
    void merge(const Cell& o)
    {
        unlocks += o.unlocks; opens += o.opens; recs += o.recs; pOpens += o.pOpens; pRecs += o.pRecs;
        exposedMs += o.exposedMs; deafMs += o.deafMs; gpsMs += o.gpsMs; pGpsMs += o.pGpsMs;
    }
};

/// The same noise on a watch with no lock at all.
struct Baseline {
    uint64_t presses = 0, opens = 0, recs = 0, gpsMs = 0, pRecs = 0, pGpsMs = 0;
    void merge(const Baseline& o)
    {
        presses += o.presses; opens += o.opens; recs += o.recs; gpsMs += o.gpsMs;
        pRecs += o.pRecs; pGpsMs += o.pGpsMs;
    }
};

StockOptions stockOptions() { return StockOptions{}; }

StockOptions patchedOptions()
{
    StockOptions o;
    o.preActivityBudgetMs = 5u * 60u * 1000u;
    o.holdToStartMs       = 1500;
    o.holdAlone           = true;
    return o;
}

template <class F>
void parallelFor(uint32_t n, unsigned threads, F&& body)   // body(index, threadNo)
{
    std::atomic<uint32_t> next{ 0 };
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t) {
        pool.emplace_back([&, t]() {
            for (;;) {
                const uint32_t i = next.fetch_add(1);
                if (i >= n) {
                    return;
                }
                body(i, t);
            }
        });
    }
    for (auto& th : pool) {
        th.join();
    }
}

double stockOpensPerHour(Model m, double r, uint32_t hours, uint64_t seed, unsigned threads)
{
    std::vector<uint64_t> opens(threads, 0);
    const StockOptions stock = stockOptions();
    parallelFor(hours, threads, [&](uint32_t ride, unsigned th) {
        Rng rng(rideSeed(seed, 7, m, ride));
        const auto kernel = toKernelEvents(generate(m, rng, kHour, r), kHour);
        opens[th] += runStock(kernel, kHour, stock).activityOpens;
    });
    uint64_t total = 0;
    for (uint64_t o : opens) {
        total += o;
    }
    return static_cast<double>(total) / hours;
}

/// The rate scale at which the stock watch opens @p target activities per hour.
double calibrate(Model m, double target, uint32_t hours, uint64_t seed, unsigned threads)
{
    if (stockOpensPerHour(m, 1.0, hours, seed, threads) <= target) {
        return 1.0;
    }
    double lo = std::log(0.002), hi = 0.0;
    for (int it = 0; it < 18; ++it) {
        const double mid = 0.5 * (lo + hi);
        (stockOpensPerHour(m, std::exp(mid), hours, seed, threads) < target ? lo : hi) = mid;
    }
    return std::exp(0.5 * (lo + hi));
}

/// Rides of one model at rate scale @p r, replayed against the variants in
/// @p active (the others' cells stay empty) and against no lock at all.
void runModel(unsigned stream, Model m, double r, uint32_t hours, uint64_t seed, unsigned threads,
              const std::vector<Variant>& vars, const std::vector<size_t>& active, std::vector<Cell>& out,
              Baseline& base)
{
    std::vector<std::vector<Cell>> local(threads, std::vector<Cell>(vars.size()));
    std::vector<Baseline> lbase(threads);
    const StockOptions stock   = stockOptions();
    const StockOptions patched = patchedOptions();
    const LockedRideOptions lo;

    parallelFor(hours, threads, [&](uint32_t ride, unsigned th) {
        Rng rng(rideSeed(seed, stream, m, ride));
        const auto presses   = generate(m, rng, kHour, r);
        const auto kernel    = toKernelEvents(presses, kHour);
        const auto delivered = paceToFrames(kernel, kHour);

        Baseline& b = lbase[th];
        b.presses += presses.size();
        const StockResult s = runStock(kernel, kHour, stock);
        b.opens += s.activityOpens;
        b.recs  += s.recordingsStarted;
        b.gpsMs += s.gpsOnMs;
        const StockResult p = runStock(kernel, kHour, patched);
        b.pRecs  += p.recordingsStarted;
        b.pGpsMs += p.gpsOnMs;

        for (size_t v : active) {
            ButtonLock::UnlockDetector d(vars[v].seq, vars[v].timing);
            local[th][v].add(runLockedRide(d, delivered, kernel, kHour, lo, stock, patched));
        }
    });

    out.assign(vars.size(), Cell{});
    base = Baseline{};
    for (unsigned t = 0; t < threads; ++t) {
        for (size_t v : active) {
            out[v].merge(local[t][v]);
        }
        base.merge(lbase[t]);
    }
}

// --- a person unlocking ----------------------------------------------------------

struct HumanProfile {
    const char* name;
    double pressMedMs, pressSigma;     // a tap's length (lognormal)
    double gapMedMs, gapSigma;         // release to next press (lognormal)
    double hesitateP;                  // a gap gets an extra 1.5-4 s pause
    double slipP;                      // a press lands wrong: half on another button, half a chord
    double earlyP;                     // the final hold is let go early (30-95% of it)
    double reactMedMs, reactSigma;     // letting go after the hold indicator completes
    double retryMs;                    // reading the message after a rejection
};

const HumanProfile kHumans[] = {
    { "bare hand", 130, 0.35, 250, 0.45, 0.01, 0.005, 0.01, 300, 0.35, 1500 },
    { "motorcycle gloves", 250, 0.40, 350, 0.50, 0.03, 0.03, 0.03, 350, 0.40, 1500 },
};
constexpr size_t kGloves = 1;

struct HumanStats {
    uint32_t trials = 0, firstTry = 0, failed = 0, cooled = 0;
    uint64_t presses = 0;
    std::vector<double> timesS;
};

double lognormal(Rng& rng, double med, double sigma)
{
    // Box-Muller from two uniforms
    const double u1 = std::max(1e-12, rng.uniform(0.0, 1.0));
    const double u2 = rng.uniform(0.0, 1.0);
    const double z  = std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
    return med * std::exp(sigma * z);
}

HumanStats simulateHuman(const Variant& v, size_t variantIndex, size_t profile, uint32_t trials, uint64_t seed)
{
    const HumanProfile& h = kHumans[profile];
    HumanStats st;
    for (uint32_t trial = 0; trial < trials; ++trial) {
        Rng rng(humanSeed(seed, variantIndex, profile, trial));
        ButtonLock::UnlockDetector d(v.seq, v.timing);
        d.reset(0);

        uint32_t lastFrame = 0;
        auto frameOf = [&](double tMs) {
            uint32_t f = static_cast<uint32_t>(std::ceil(tMs / kFrameMs)) * kFrameMs;
            f = std::max(f, lastFrame + kFrameMs);
            lastFrame = f;
            return f;
        };

        const double start = 5000.0;   // the watch has been quiet
        double t = start;
        uint32_t attempts = 1;
        bool done = false, cooledOnce = false;
        uint32_t presses = 0;
        uint32_t unlockAt = 0;

        while (!done && t < start + 120000.0) {
            if (d.coolingDown()) {
                cooledOnce = true;
                const uint32_t now = static_cast<uint32_t>(t);
                t += d.cooldownRemaining(now) + 500.0;
                d.poll(static_cast<uint32_t>(t));
                ++attempts;
                continue;
            }
            const uint8_t step   = d.progress();
            const Button  target = v.seq[step];
            const bool    hold   = d.holdDue();

            // What the hand does.
            Button pressed = target;
            bool chord = false;
            const double u = rng.uniform(0.0, 1.0);
            if (u < h.slipP / 2) {
                do {
                    pressed = rng.anyButton();
                } while (pressed == target);
            } else if (u < h.slipP) {
                chord = true;
            }
            double held;
            if (hold && pressed == target) {
                held = rng.chance(h.earlyP) ? v.holdMs * rng.uniform(0.3, 0.95)
                                            : v.holdMs + lognormal(rng, h.reactMedMs, h.reactSigma);
            } else {
                held = lognormal(rng, h.pressMedMs, h.pressSigma);
            }
            ++presses;

            std::vector<Event> ev;   // this press, kernel order
            auto addPress = [&](Button b, double down, double len) {
                ev.push_back(Event{ static_cast<uint32_t>(down), b, ButtonLock::Input::Press });
                const double up = down + len;
                if (len < kKernelClickMs) {
                    ev.push_back(Event{ static_cast<uint32_t>(up), b, ButtonLock::Input::Click });
                }
                ev.push_back(Event{ static_cast<uint32_t>(up), b, ButtonLock::Input::Release });
            };
            addPress(pressed, t, held);
            if (chord) {
                Button other;
                do {
                    other = rng.anyButton();
                } while (other == pressed);
                addPress(other, t + rng.uniform(10, 80), lognormal(rng, h.pressMedMs, h.pressSigma));
            }
            std::stable_sort(ev.begin(), ev.end(), [](const Event& a, const Event& b) { return a.t < b.t; });

            bool rejected = false;
            uint32_t lastT = 0;
            for (const Event& e : ev) {
                const uint32_t f = frameOf(e.t);
                lastT = f;
                const Outcome o = d.onInput(e.b, e.in, f);
                if (o == Outcome::Unlocked) {
                    done = true;
                    unlockAt = f;
                } else if (ButtonLock::isRejection(o)) {
                    rejected = true;
                }
            }
            if (done) {
                break;
            }
            if (rejected) {
                t = lastT + h.retryMs;
                ++attempts;
                continue;
            }
            // On to the next step, perhaps after a pause.
            double gap = lognormal(rng, h.gapMedMs, h.gapSigma);
            if (rng.chance(h.hesitateP)) {
                gap += rng.uniform(1500.0, 4000.0);
            }
            const double next = lastT + gap;
            // In double: a naive lock's maxGapMs is "never" (0xFFFFFFFF).
            const double timeoutAt = static_cast<double>(lastT) + d.timing().maxGapMs + kFrameMs;
            if (d.progress() > 0 && next > timeoutAt) {
                // The guide says "too slow": start over after reading it.
                const uint32_t at = static_cast<uint32_t>(timeoutAt);
                d.poll(at);
                lastFrame = std::max(lastFrame, at);
                t = timeoutAt + h.retryMs;
                ++attempts;
                continue;
            }
            t = next;
        }

        ++st.trials;
        st.presses += presses;
        if (cooledOnce) {
            ++st.cooled;
        }
        if (!done) {
            ++st.failed;
            continue;
        }
        if (attempts == 1) {
            ++st.firstTry;
        }
        st.timesS.push_back((unlockAt - start) / 1000.0);
    }
    std::sort(st.timesS.begin(), st.timesS.end());
    return st;
}

double quantile(const HumanStats& st, double q)
{
    if (st.timesS.empty()) {
        return 0.0;
    }
    return st.timesS[static_cast<size_t>(q * static_cast<double>(st.timesS.size() - 1))];
}

// --- levels and printing ---------------------------------------------------------

const std::vector<Model> kModels(kStudyModels.begin(), kStudyModels.end());

struct Level {
    std::string          name;     // "Harsh", "Calibrated", ...
    std::vector<double>  rate;     // per model
    uint32_t             hours;
    unsigned             stream;
    std::vector<size_t>  active;   // variants run at this level
    std::vector<std::vector<Cell>> cells;   // [model][variant]
    std::vector<Baseline> base;            // [model]
};

void runLevel(Level& L, uint64_t seed, unsigned threads, const std::vector<Variant>& vars)
{
    L.cells.resize(kModels.size());
    L.base.resize(kModels.size());
    for (size_t m = 0; m < kModels.size(); ++m) {
        runModel(L.stream, kModels[m], L.rate[m], L.hours, seed, threads, vars, L.active, L.cells[m], L.base[m]);
        std::fprintf(stderr, "%s: %s done\n", L.name.c_str(), shortModel(kModels[m]));
    }
}

size_t worstModel(const Level& L, size_t v, uint64_t Cell::*field)
{
    size_t w = 0;
    for (size_t m = 1; m < kModels.size(); ++m) {
        if (L.cells[m][v].*field > L.cells[w][v].*field) {
            w = m;
        }
    }
    return w;
}

/// "r Model" for the worst model, or "0 (<u), every model".
std::string worst(const Level& L, size_t v, uint64_t Cell::*field, bool ci = false)
{
    const size_t   w = worstModel(L, v, field);
    const uint64_t k = L.cells[w][v].*field;
    return rate(k, L.hours, ci) + (k ? std::string(" ") + shortModel(kModels[w]) : ", every model");
}

std::string worstBase(const Level& L, uint64_t Baseline::*field, bool ci = false)
{
    size_t w = 0;
    for (size_t m = 1; m < kModels.size(); ++m) {
        if (L.base[m].*field > L.base[w].*field) {
            w = m;
        }
    }
    const uint64_t k = L.base[w].*field;
    return rate(k, L.hours, ci) + (k ? std::string(" ") + shortModel(kModels[w]) : ", every model");
}

void printModelHeader(const char* first, const char* extra)
{
    std::printf("| %s |%s Worst model |", first, extra);
    for (Model m : kModels) {
        std::printf(" %s |", shortModel(m));
    }
    std::printf("\n|---|");
    for (const char* p = extra; *p; ++p) {
        if (*p == '|') {
            std::printf("---|");
        }
    }
    for (size_t m = 0; m <= kModels.size(); ++m) {
        std::printf("---:|");
    }
    std::printf("\n");
}

void printModelRow(const Level& L, const std::string& label, const std::string& extra, size_t v, uint64_t Cell::*field)
{
    std::printf("| %s |%s %s |", label.c_str(), extra.c_str(), worst(L, v, field).c_str());
    for (size_t m = 0; m < kModels.size(); ++m) {
        std::printf(" %s |", rate(L.cells[m][v].*field, L.hours).c_str());
    }
    std::printf("\n");
}

} // namespace

int main(int argc, char** argv)
{
    const uint32_t hostileHours = argc > 1 ? static_cast<uint32_t>(std::atoi(argv[1])) : 10000u;
    const uint32_t calibHours   = argc > 2 ? static_cast<uint32_t>(std::atoi(argv[2])) : hostileHours;
    unsigned threads = argc > 3 ? static_cast<unsigned>(std::atoi(argv[3])) : std::thread::hardware_concurrency();
    if (threads == 0) {
        threads = 4;
    }
    const uint64_t seed = argc > 4 ? std::strtoull(argv[4], nullptr, 0) : 0x5E9u;
    if (hostileHours == 0 || calibHours == 0) {
        std::fprintf(stderr, "usage: seq_study [hostile-hours] [calibrated-hours] [threads] [seed]\n");
        return 2;
    }
    const auto t0 = std::chrono::steady_clock::now();

    using B = Button;
    const std::vector<Variant> vars = makeVariants();
    auto key = [&](Family f, std::initializer_list<Button> s, uint32_t hold = 0) {
        return findVariant(vars, f, steps(s), hold);
    };
    const size_t vSix     = key(Family::Six, { B::Up, B::Select, B::Back, B::Down, B::Up, B::Select });
    const size_t vFive    = key(Family::Five, { B::Up, B::Select, B::Back, B::Down, B::Up });
    const size_t vDefault = findVariant(vars, Family::Four, ButtonLock::defaultSequence(), 0);
    const size_t vFourX   = key(Family::Four, { B::Up, B::Back, B::Down, B::Select });
    const size_t vThree   = key(Family::Three, { B::Up, B::Select, B::Back });
    const size_t vAbaDiag = key(Family::Three, { B::Up, B::Back, B::Up });
    const size_t vAbaRow  = key(Family::Three, { B::Up, B::Select, B::Up });
    const size_t vTwo     = key(Family::Two, { B::Up, B::Select });
    const size_t vTapH08  = key(Family::TapHold, { B::Up, B::Select }, 800);
    const size_t vTapH10  = key(Family::TapHold, { B::Up, B::Select }, 1000);
    const size_t vTwoH08  = key(Family::TwoTapsHold, { B::Up, B::Select, B::Back }, 800);
    const size_t vTwoH10  = key(Family::TwoTapsHold, { B::Up, B::Select, B::Back }, 1000);
    const size_t vNaive3  = key(Family::Naive, { B::Up, B::Select, B::Back });
    const size_t vNaive4  = findVariant(vars, Family::Naive, ButtonLock::defaultSequence(), 0);

    const std::vector<size_t> headline = { vSix, vFive, vDefault, vFourX, vThree, vAbaDiag, vAbaRow, vTwo,
                                           vTapH08, vTapH10, vTwoH08, vTwoH10, vNaive3, vNaive4 };
    const std::vector<size_t> summary  = { vTwo, vThree, vDefault, vFive, vSix, vAbaRow, vTapH10, vTwoH10, vNaive3 };
    const std::vector<size_t> humanSet = { vSix, vFive, vDefault, vThree, vAbaRow, vTwo,
                                           vTapH08, vTapH10, vTwoH08, vTwoH10, vNaive3 };
    std::vector<size_t> everyVariant(vars.size());
    for (size_t v = 0; v < vars.size(); ++v) {
        everyVariant[v] = v;
    }

    // Calibration: the stock watch opens ~1 activity per hour.
    constexpr double   kTargetOpens = 1.0;
    constexpr uint32_t kCalibHours  = 3000;
    std::vector<double> calRate;
    for (Model m : kModels) {
        calRate.push_back(calibrate(m, kTargetOpens, kCalibHours, seed, threads));
        std::fprintf(stderr, "calibrated %s: %.4f\n", shortModel(m), calRate.back());
    }
    auto scaled = [&](double f) {
        std::vector<double> r = calRate;
        for (double& x : r) {
            x *= f;
        }
        return r;
    };

    std::vector<Level> levels = {
        { "Harsh", std::vector<double>(kModels.size(), 1.0), hostileHours, 1, everyVariant, {}, {} },
        { "Calibrated", calRate, calibHours, 2, everyVariant, {}, {} },
        { "Calibrated x2", scaled(2.0), calibHours, 3, headline, {}, {} },
        { "Calibrated x4", scaled(4.0), calibHours, 4, headline, {}, {} },
    };
    for (Level& L : levels) {
        runLevel(L, seed, threads, vars);
    }
    const Level& harsh = levels[0];
    const Level& calib = levels[1];

    // Rate sweep of the random-taps model, relative to the harsh level.
    const std::vector<double> sweepRates = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0 };
    const std::vector<size_t> sweepVars  = { vThree, vDefault, vFive, vSix };
    std::vector<std::vector<Cell>> sweepCells(sweepRates.size());
    std::vector<Baseline> sweepBase(sweepRates.size());
    for (size_t i = 0; i < sweepRates.size(); ++i) {
        runModel(16 + static_cast<unsigned>(i), Model::RandomTaps, sweepRates[i], calibHours, seed, threads, vars,
                 sweepVars, sweepCells[i], sweepBase[i]);
    }
    std::fprintf(stderr, "rate sweep done\n");

    // A person unlocking: every (variant, profile) pair is its own task.
    constexpr uint32_t kHumanTrials = 20000;
    constexpr size_t   kProfiles    = std::size(kHumans);
    std::vector<HumanStats> human(humanSet.size() * kProfiles);
    parallelFor(static_cast<uint32_t>(human.size()), threads, [&](uint32_t task, unsigned) {
        const size_t v = humanSet[task / kProfiles];
        human[task] = simulateHuman(vars[v], v, task % kProfiles, kHumanTrials, seed);
    });
    auto humanOf = [&](size_t v, size_t profile) -> const HumanStats* {
        for (size_t i = 0; i < humanSet.size(); ++i) {
            if (humanSet[i] == v) {
                return &human[i * kProfiles + profile];
            }
        }
        return nullptr;
    };

    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    // ------------------------------------------------------------------------------
    std::printf("# Unlock sequence study\n\n");
    std::printf("Generated by `tests/seq_study.cpp`: `seq_study %u %u %u 0x%llx` (%u threads, %.0f s).\n\n",
                hostileHours, calibHours, threads, static_cast<unsigned long long>(seed), threads, elapsed);
    std::printf("Every lock variant runs on the same one-hour rides (common random numbers): %u h per jacket model at the "
                "harsh level, %u h per model at the calibrated level and at 2x and 4x its rate (candidate locks only), "
                "%zu models, %zu lock variants. All of ButtonLock's rules are on (quiet 0.6 s, press < 1.2 s, "
                "gap < 2.5 s, chords end the attempt, 3 rejections in 30 s -> cool-down until 15 s of silence, pinned "
                "> 4 s ignored). The lock sees the frame-paced stream (10 Hz, 16-deep queue). After a false unlock the "
                "open padlock shows for 0.7 s, then the watch face has the buttons for **2 min** (relockMs) and the same "
                "noise is fed to the stock-watch model and to the patched-apps model (5 min pre-activity budget, start = "
                "1.5 s hold of SELECT alone); then the lock is back up (detector reset).\n\n",
                hostileHours, calibHours, kModels.size(), vars.size());
    std::printf("**The consequence window.** Each 2-min window starts on the watch face with nothing open. For a very weak "
                "lock, which opens again soon after it relocks, an activity opened in an earlier window would in fact still "
                "be open, so activities opened are overstated for such locks and recordings understated. On the stock apps "
                "an activity opened in a window stays open, GPS on, behind the relocked screen: each one is a drain that "
                "lasts until the drain alert or the wearer catches it.\n\n");
    std::printf("Rates are **per 1000 h** of that noise. `r (lo-hi)` is the Poisson 95%% interval; `0 (<u)` is a zero count "
                "with its 3/N upper bound. Buttons: UP top left, SELECT top right, DOWN bottom left, BACK bottom right. "
                "*Settings*: whether the app's settings accept the code (they refuse codes under 3 steps or on fewer than "
                "3 different buttons; holds are not offered). *Worst model*: the jacket model with the highest count "
                "for that lock and column, named. No pooled (all-model) figures are given: three of the eight models "
                "(OneSide, Squeeze, Rocker) can never produce a code on three or more different buttons, so pooling "
                "would dilute the risk.\n\n");

    // 1. Noise levels
    std::printf("## 1. Noise levels\n\n");
    std::printf("Calibration: for each model, the event rate (every interval between noise events divided by the scale) "
                "was bisected on %u h so the stock watch, with no lock, opens %.1f activity per hour. The calibrated point "
                "is weakly anchored (stock opens saturate as noise rises), hence the 2x and 4x band. Measured in the main "
                "runs:\n\n", kCalibHours, kTargetOpens);
    std::printf("| Model | Harsh: presses/h | Harsh: stock opens/h | Calibrated: rate scale | Calibrated: presses/h "
                "| Calibrated: stock opens/h | x2: presses/h | x2: stock opens/h | x4: presses/h | x4: stock opens/h |\n");
    std::printf("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n");
    for (size_t m = 0; m < kModels.size(); ++m) {
        std::printf("| %s | %.0f | %s | %.3f |", modelName(kModels[m]),
                    static_cast<double>(levels[0].base[m].presses) / levels[0].hours,
                    perHour(levels[0].base[m].opens, levels[0].hours).c_str(), calRate[m]);
        for (size_t l = 1; l < levels.size(); ++l) {
            std::printf(" %.0f | %s |", static_cast<double>(levels[l].base[m].presses) / levels[l].hours,
                        perHour(levels[l].base[m].opens, levels[l].hours).c_str());
        }
        std::printf("\n");
    }
    std::printf("\nNo lock: recordings started per hour.\n\n");
    std::printf("| Model | Harsh: stock | Harsh: patched apps | Calibrated: stock | Calibrated: patched apps "
                "| x2: stock | x4: stock |\n|---|---:|---:|---:|---:|---:|---:|\n");
    for (size_t m = 0; m < kModels.size(); ++m) {
        std::printf("| %s | %s | %s | %s | %s | %s | %s |\n", modelName(kModels[m]),
                    perHour(levels[0].base[m].recs, levels[0].hours).c_str(),
                    perHour(levels[0].base[m].pRecs, levels[0].hours).c_str(),
                    perHour(levels[1].base[m].recs, levels[1].hours).c_str(),
                    perHour(levels[1].base[m].pRecs, levels[1].hours).c_str(),
                    perHour(levels[2].base[m].recs, levels[2].hours).c_str(),
                    perHour(levels[3].base[m].recs, levels[3].hours).c_str());
    }
    std::printf("\n");

    // 2. Summary
    std::printf("## 2. Summary: candidate locks\n\n");
    std::printf("Worst model for each column, named. Time and first-try success: the gloves profile of §13 (indicative). "
                "Activities opened: by the noise during the 2-min windows after false unlocks (no lock: by the noise on "
                "the stock watch).\n\n");
    std::printf("| Lock | Settings | Presses | Gloves: median time | Gloves: first try | Harsh: false unlocks "
                "| Harsh: activities opened | Calibrated: false unlocks | Calibrated: activities opened |\n");
    std::printf("|---|---|---:|---:|---:|---:|---:|---:|---:|\n");
    for (size_t v : summary) {
        const HumanStats* g = humanOf(v, kGloves);
        char tm[32] = "-", ft[32] = "-";
        if (g != nullptr) {
            std::snprintf(tm, sizeof(tm), "%.1f s", quantile(*g, 0.5));
            std::snprintf(ft, sizeof(ft), "%.0f %%", 100.0 * g->firstTry / g->trials);
        }
        std::printf("| %s | %s | %s | %s | %s | %s | %s | %s | %s |\n", vars[v].label.c_str(), vars[v].settings,
                    vars[v].presses().c_str(), tm, ft, worst(harsh, v, &Cell::unlocks).c_str(),
                    worst(harsh, v, &Cell::opens).c_str(), worst(calib, v, &Cell::unlocks).c_str(),
                    worst(calib, v, &Cell::opens).c_str());
    }
    std::printf("| *No lock: stock watch* | - | 0 | - | - | - | %s | - | %s |\n\n",
                worstBase(harsh, &Baseline::opens).c_str(), worstBase(calib, &Baseline::opens).c_str());

    // 3. Headline per level, with intervals and every consequence
    std::printf("## 3. Candidate locks, per level (worst model, with 95%% intervals)\n\n");
    for (size_t l = 0; l < 2; ++l) {
        const Level& L = levels[l];
        std::printf("### %s (%u h per model)\n\n", L.name.c_str(), L.hours);
        std::printf("| Lock | Settings | Presses | False unlocks | Stock: activities opened | Stock: recordings started "
                    "| Patched apps: recordings started |\n|---|---|---:|---:|---:|---:|---:|\n");
        std::printf("| *No lock* | - | - | - | %s | %s | %s |\n", worstBase(L, &Baseline::opens, true).c_str(),
                    worstBase(L, &Baseline::recs, true).c_str(), worstBase(L, &Baseline::pRecs, true).c_str());
        for (size_t v : headline) {
            std::printf("| %s | %s | %s | %s | %s | %s | %s |\n", vars[v].label.c_str(), vars[v].settings,
                        vars[v].presses().c_str(), worst(L, v, &Cell::unlocks, true).c_str(),
                        worst(L, v, &Cell::opens, true).c_str(), worst(L, v, &Cell::recs, true).c_str(),
                        worst(L, v, &Cell::pRecs, true).c_str());
        }
        std::printf("\n");
    }

    // 4. Calibration band
    std::printf("## 4. Calibration band: the calibrated level, and 2x and 4x its noise rate\n\n");
    std::printf("Worst model per cell. The stock watch's own opens (first row) grow far more slowly than the noise.\n\n");
    std::printf("| Lock | Settings | False unlocks: calibrated | x2 | x4 | Activities opened: calibrated | x2 | x4 |\n");
    std::printf("|---|---|---:|---:|---:|---:|---:|---:|\n");
    std::printf("| *No lock: stock watch* | - | - | - | - | %s | %s | %s |\n", worstBase(levels[1], &Baseline::opens).c_str(),
                worstBase(levels[2], &Baseline::opens).c_str(), worstBase(levels[3], &Baseline::opens).c_str());
    for (size_t v : headline) {
        std::printf("| %s | %s | %s | %s | %s | %s | %s | %s |\n", vars[v].label.c_str(), vars[v].settings,
                    worst(levels[1], v, &Cell::unlocks).c_str(), worst(levels[2], v, &Cell::unlocks).c_str(),
                    worst(levels[3], v, &Cell::unlocks).c_str(), worst(levels[1], v, &Cell::opens).c_str(),
                    worst(levels[2], v, &Cell::opens).c_str(), worst(levels[3], v, &Cell::opens).c_str());
    }
    std::printf("\nPer model, false unlocks per 1000 h:\n\n");
    for (size_t l = 1; l < levels.size(); ++l) {
        std::printf("### %s\n\n", levels[l].name.c_str());
        printModelHeader("Lock", "");
        for (size_t v : headline) {
            printModelRow(levels[l], vars[v].label, "", v, &Cell::unlocks);
        }
        std::printf("\n");
    }

    // 5. False unlocks per model
    std::printf("## 5. False unlocks per 1000 h, per model (every lock but 2 taps + hold, which is in §10)\n\n");
    for (size_t l = 0; l < 2; ++l) {
        const Level& L = levels[l];
        std::printf("### %s (%u h per model)\n\n", L.name.c_str(), L.hours);
        printModelHeader("Lock", " Settings |");
        for (size_t v = 0; v < vars.size(); ++v) {
            if (vars[v].family != Family::TwoTapsHold) {
                printModelRow(L, vars[v].label, std::string(" ") + vars[v].settings + " |", v, &Cell::unlocks);
            }
        }
        std::printf("\n");
    }
    {
        std::string never;
        for (size_t m = 0; m < kModels.size(); ++m) {
            uint64_t k = 0;
            for (const Level& L : levels) {
                for (size_t v : L.active) {
                    if (vars[v].threeButtons()) {
                        k += L.cells[m][v].unlocks;
                    }
                }
            }
            if (k == 0) {
                never += never.empty() ? "" : ", ";
                never += shortModel(kModels[m]);
            }
        }
        std::printf("Models that never unlocked any code on three or more different buttons, at any level: %s.\n\n",
                    never.empty() ? "none" : never.c_str());
    }

    // 6. Consequences per model
    std::printf("## 6. Consequences per 1000 h, per model (candidate locks)\n\n");
    struct Metric {
        const char* title;
        uint64_t Cell::*field;
    };
    for (const Metric& metric : { Metric{ "Stock watch: activities opened (GPS on)", &Cell::opens },
                                  Metric{ "Stock watch: recordings started", &Cell::recs },
                                  Metric{ "Patched apps: recordings started", &Cell::pRecs } }) {
        for (size_t l = 0; l < 2; ++l) {
            std::printf("### %s, %s\n\n", metric.title, levels[l].name.c_str());
            printModelHeader("Lock", "");
            for (size_t v : headline) {
                printModelRow(levels[l], vars[v].label, "", v, metric.field);
            }
            std::printf("\n");
        }
    }

    // 7. Every lock, worst model
    std::printf("## 7. Every lock: false unlocks and their consequences (per 1000 h, worst model)\n\n");
    for (size_t l = 0; l < 2; ++l) {
        const Level& L = levels[l];
        std::printf("### %s\n\n", L.name.c_str());
        std::printf("| Lock | Settings | Presses | False unlocks | Stock: activities opened | Stock: recordings started "
                    "| Patched apps: recordings started |\n|---|---|---:|---:|---:|---:|---:|\n");
        for (size_t v = 0; v < vars.size(); ++v) {
            std::printf("| %s | %s | %s | %s | %s | %s | %s |\n", vars[v].label.c_str(), vars[v].settings,
                        vars[v].presses().c_str(), worst(L, v, &Cell::unlocks).c_str(), worst(L, v, &Cell::opens).c_str(),
                        worst(L, v, &Cell::recs).c_str(), worst(L, v, &Cell::pRecs).c_str());
        }
        std::printf("\n");
    }

    // 8. 3-step codes
    std::printf("## 8. Every 3-step code\n\n");
    std::vector<size_t> three;
    for (size_t v = 0; v < vars.size(); ++v) {
        if (vars[v].family == Family::Three) {
            three.push_back(v);
        }
    }
    auto worstCount = [&](const Level& L, size_t v) { return L.cells[worstModel(L, v, &Cell::unlocks)][v].unlocks; };
    std::sort(three.begin(), three.end(), [&](size_t a, size_t b) {
        const uint64_t ca = worstCount(calib, a), cb = worstCount(calib, b);
        return ca != cb ? ca < cb : worstCount(harsh, a) < worstCount(harsh, b);
    });
    std::printf("Ranked by the calibrated worst-model count, then the harsh one. Shape: ABA row (A and B on the same "
                "row), ABA diagonal, ABA side, or ABC with each move marked - (across a row), x (diagonal), "
                "| (along a side).\n\n");
    std::printf("| # | Code | Shape | Settings | Harsh: false unlocks | Calibrated: false unlocks |\n");
    std::printf("|---:|---|---|---|---:|---:|\n");
    {
        int rank = 0;
        for (size_t v : three) {
            std::printf("| %d | %s | %s | %s | %s | %s |\n", ++rank, vars[v].label.c_str(), shapeOf(vars[v].seq).c_str(),
                        vars[v].settings, worst(harsh, v, &Cell::unlocks).c_str(),
                        worst(calib, v, &Cell::unlocks).c_str());
        }
    }
    std::printf("\nBy shape class: mean false unlocks per code of the class, per 1000 h.\n\n");
    auto shapeClass = [&](size_t v) {
        const std::string s = shapeOf(vars[v].seq);
        return s.rfind("ABC", 0) == 0 ? std::string("ABC (three buttons)") : s;
    };
    for (size_t l = 0; l < 2; ++l) {
        const Level& L = levels[l];
        std::printf("### %s\n\n", L.name.c_str());
        std::printf("| Shape class | Codes |");
        for (Model m : kModels) {
            std::printf(" %s |", shortModel(m));
        }
        std::printf("\n|---|---:|");
        for (size_t m = 0; m < kModels.size(); ++m) {
            std::printf("---:|");
        }
        std::printf("\n");
        std::map<std::string, std::vector<size_t>> classes;
        for (size_t v : three) {
            classes[shapeClass(v)].push_back(v);
        }
        for (const auto& [cls, members] : classes) {
            std::printf("| %s | %zu |", cls.c_str(), members.size());
            for (size_t m = 0; m < kModels.size(); ++m) {
                uint64_t k = 0;
                for (size_t v : members) {
                    k += L.cells[m][v].unlocks;
                }
                // Mean per code; a zero gets the per-code bound (the codes share rides).
                std::printf(" %s |", k ? num(1000.0 * k / (static_cast<double>(L.hours) * members.size())).c_str()
                                       : rate(0, L.hours).c_str());
            }
            std::printf("\n");
        }
        std::printf("\n");
    }
    std::printf("**The 24 codes on three different buttons (the 3-step codes the settings accept): measured range.** "
                "Every model that can produce them treats the four buttons alike, so they have the same expected rate. "
                "They are measured on the same rides, so their counts are correlated and no independent-Poisson test "
                "applies; this is the spread that was measured. SD: the standard deviation across the 24 codes, as a "
                "share of their mean.\n\n");
    std::printf("| Level | Model | Lowest | Highest | Mean | SD | Highest/lowest - 1 |\n|---|---|---:|---:|---:|---:|---:|\n");
    for (size_t l = 0; l < 2; ++l) {
        const Level& L = levels[l];
        for (size_t m = 0; m < kModels.size(); ++m) {
            std::vector<double> k;
            for (size_t v : three) {
                if (vars[v].threeButtons()) {
                    k.push_back(static_cast<double>(L.cells[m][v].unlocks));
                }
            }
            const double lo = *std::min_element(k.begin(), k.end());
            const double hi = *std::max_element(k.begin(), k.end());
            if (hi == 0.0) {
                std::printf("| %s | %s | 0 (<%s) for all %zu | | | | |\n", L.name.c_str(), shortModel(kModels[m]),
                            num(3000.0 / L.hours).c_str(), k.size());
                continue;
            }
            double mean = 0.0, var = 0.0;
            for (double x : k) {
                mean += x / static_cast<double>(k.size());
            }
            for (double x : k) {
                var += (x - mean) * (x - mean) / static_cast<double>(k.size() - 1);
            }
            char sd[32], spread[32] = "-";
            std::snprintf(sd, sizeof(sd), "%.1f %%", 100.0 * std::sqrt(var) / mean);
            if (lo > 0.0) {
                std::snprintf(spread, sizeof(spread), "%.0f %%", 100.0 * (hi / lo - 1.0));
            }
            std::printf("| %s | %s | %s | %s | %s | %s | %s |\n", L.name.c_str(), shortModel(kModels[m]),
                        rate(static_cast<uint64_t>(lo), L.hours).c_str(), rate(static_cast<uint64_t>(hi), L.hours).c_str(),
                        num(1000.0 * mean / L.hours).c_str(), sd, spread);
        }
    }
    std::printf("\n");

    // 9. By family, and the per-step factor
    struct Group {
        std::string         name;
        uint32_t            hold;
        std::vector<size_t> members;
    };
    auto groupOf = [&](const std::string& name, Family f, uint32_t hold, int buttons) {
        Group g{ name, hold, {} };
        for (size_t v = 0; v < vars.size(); ++v) {
            if (vars[v].family != f || vars[v].holdMs != hold) {
                continue;
            }
            const size_t n = distinctButtons(vars[v].seq);
            if ((buttons == 2 && n > 2) || (buttons == 3 && n != 3) || (buttons == 4 && n != 4)) {
                continue;
            }
            g.members.push_back(v);
        }
        return g;
    };
    const std::vector<Group> groups = {
        groupOf("2 taps", Family::Two, 0, 0),
        groupOf("3 taps, three buttons", Family::Three, 0, 3),
        groupOf("3 taps, two buttons", Family::Three, 0, 2),
        groupOf("4 taps, four buttons", Family::Four, 0, 4),
        groupOf("4 taps, two buttons", Family::Four, 0, 2),
        groupOf("5 taps", Family::Five, 0, 0),
        groupOf("6 taps", Family::Six, 0, 0),
        groupOf("1 tap + hold", Family::TapHold, 800, 0),
        groupOf("1 tap + hold", Family::TapHold, 1000, 0),
        groupOf("2 taps + hold, three buttons", Family::TwoTapsHold, 800, 3),
        groupOf("2 taps + hold, three buttons", Family::TwoTapsHold, 1000, 3),
        groupOf("2 taps + hold, two buttons", Family::TwoTapsHold, 800, 2),
        groupOf("2 taps + hold, two buttons", Family::TwoTapsHold, 1000, 2),
    };
    auto groupCount = [&](const Level& L, const Group& g, size_t m) {
        uint64_t k = 0;
        for (size_t v : g.members) {
            k += L.cells[m][v].unlocks;
        }
        return k;
    };
    auto groupMean = [&](const Level& L, const Group& g, size_t m) {   // per code, per 1000 h
        return 1000.0 * static_cast<double>(groupCount(L, g, m)) / (static_cast<double>(L.hours) * g.members.size());
    };
    std::printf("## 9. By family: mean false unlocks per code, per 1000 h\n\n");
    std::printf("A family's codes are alike to every model that treats the four buttons alike (all but OneSide and "
                "Rocker), so its mean is a steadier figure than any one code's. A zero is shown with one code's 3/N "
                "bound (the codes share rides).\n\n");
    for (size_t l = 0; l < 2; ++l) {
        const Level& L = levels[l];
        std::printf("### %s\n\n| Family | H | Codes | Worst code on its worst model |", L.name.c_str());
        for (Model m : kModels) {
            std::printf(" %s |", shortModel(m));
        }
        std::printf("\n|---|---:|---:|---|");
        for (size_t m = 0; m < kModels.size(); ++m) {
            std::printf("---:|");
        }
        std::printf("\n");
        for (const Group& g : groups) {
            uint64_t worstK = 0;
            size_t worstV = g.members.front(), worstM = 0;
            for (size_t v : g.members) {
                for (size_t m = 0; m < kModels.size(); ++m) {
                    if (L.cells[m][v].unlocks > worstK) {
                        worstK = L.cells[m][v].unlocks;
                        worstV = v;
                        worstM = m;
                    }
                }
            }
            const std::string w = worstK ? rate(worstK, L.hours) + " " + vars[worstV].label + ", " + shortModel(kModels[worstM])
                                         : rate(0, L.hours) + ", every code";
            std::printf("| %s | %s | %zu | %s |", g.name.c_str(), g.hold ? (g.hold == 800 ? "0.8 s" : "1.0 s") : "-",
                        g.members.size(), w.c_str());
            for (size_t m = 0; m < kModels.size(); ++m) {
                std::printf(" %s |", groupCount(L, g, m) ? num(groupMean(L, g, m)).c_str() : rate(0, L.hours).c_str());
            }
            std::printf("\n");
        }
        std::printf("\n");
    }
    std::printf("**Per-step factor**: the family mean at n taps divided by the mean at n + 1 taps (2 taps; 3 taps on "
                "three buttons; 4 taps on four buttons; 5; 6). `>x` when the longer one had no false unlock (against "
                "its 3/N bound) and that bound says something; `-` otherwise. Models that never open a code on three "
                "buttons at that level are left out.\n\n");
    std::printf("| Level | Model | 2 -> 3 | 3 -> 4 | 4 -> 5 | 5 -> 6 |\n|---|---|---:|---:|---:|---:|\n");
    {
        const Group* chain[5] = { &groups[0], &groups[1], &groups[3], &groups[5], &groups[6] };
        for (size_t l = 0; l < 2; ++l) {
            const Level& L = levels[l];
            for (size_t m = 0; m < kModels.size(); ++m) {
                if (groupCount(L, *chain[1], m) == 0) {
                    continue;
                }
                std::printf("| %s | %s |", L.name.c_str(), shortModel(kModels[m]));
                for (size_t i = 0; i + 1 < 5; ++i) {
                    const uint64_t a = groupCount(L, *chain[i], m), b = groupCount(L, *chain[i + 1], m);
                    char buf[32] = "-";
                    if (a > 0 && b > 0) {
                        std::snprintf(buf, sizeof(buf), "%.1f", groupMean(L, *chain[i], m) / groupMean(L, *chain[i + 1], m));
                    } else if (a > 0 && groupMean(L, *chain[i], m) / (3000.0 / L.hours) >= 1.0) {
                        std::snprintf(buf, sizeof(buf), ">%.1f", groupMean(L, *chain[i], m) / (3000.0 / L.hours));
                    }
                    std::printf(" %s |", buf);
                }
                std::printf("\n");
            }
        }
    }
    std::printf("\n");

    // 10. Hold to finish
    std::printf("## 10. Hold to finish\n\n");
    std::printf("The last step must be held alone for H and released before 4 s (stuckMs); another press during it is a "
                "chord. *1 tap + hold* is e.g. \"UP, then hold SELECT\" (2 presses); *2 taps + hold* is e.g. \"UP SELECT, "
                "then hold BACK\" (3 presses). Only `Taps+Holds`, `OneSide` and `Chaos` produce presses over 0.5 s on "
                "their own, and `OneSide` cannot produce a two-sided code, so the hold is tested mainly by `Taps+Holds` "
                "(a quarter of all random presses held 0.5-3 s). Family means are in §9. A hold also needs an indicator "
                "on screen that says when it is long enough.\n\n");
    for (size_t l = 0; l < 2; ++l) {
        std::printf("### %s: every 2 taps + hold code, per model (false unlocks per 1000 h)\n\n", levels[l].name.c_str());
        printModelHeader("Lock", "");
        for (size_t v = 0; v < vars.size(); ++v) {
            if (vars[v].family == Family::TwoTapsHold) {
                printModelRow(levels[l], vars[v].label, "", v, &Cell::unlocks);
            }
        }
        std::printf("\n");
    }

    // 11. Rate sweep
    std::printf("## 11. Rate sweep: random taps (any button), %u h per point\n\n", calibHours);
    std::printf("The harsh level is one tap every 6 s on average (scale 1). False unlocks per 1000 h.\n\n");
    std::printf("| Mean gap between taps | Scale | Presses/h | Stock opens/h |");
    for (size_t v : sweepVars) {
        std::printf(" %s |", vars[v].label.c_str());
    }
    std::printf("\n|---:|---:|---:|---:|");
    for (size_t i = 0; i < sweepVars.size(); ++i) {
        std::printf("---:|");
    }
    std::printf("\n");
    for (size_t i = 0; i < sweepRates.size(); ++i) {
        std::printf("| %.1f s | %.2f | %.0f | %s |", 6.0 / sweepRates[i], sweepRates[i],
                    static_cast<double>(sweepBase[i].presses) / calibHours, perHour(sweepBase[i].opens, calibHours).c_str());
        for (size_t v : sweepVars) {
            std::printf(" %s |", rate(sweepCells[i][v].unlocks, calibHours).c_str());
        }
        std::printf("\n");
    }
    std::printf("\n");

    // 12. Deafness
    std::printf("## 12. Share of time the lock is cooling down (a person is told to wait)\n\n");
    for (size_t l = 0; l < 2; ++l) {
        const Level& L = levels[l];
        std::printf("### %s\n\n| Lock |", L.name.c_str());
        for (Model m : kModels) {
            std::printf(" %s |", shortModel(m));
        }
        std::printf("\n|---|");
        for (size_t m = 0; m < kModels.size(); ++m) {
            std::printf("---:|");
        }
        std::printf("\n");
        for (size_t v : { vSix, vFive, vDefault, vThree, vTapH10, vTwoH10 }) {
            std::printf("| %s |", vars[v].label.c_str());
            for (size_t m = 0; m < kModels.size(); ++m) {
                std::printf(" %.1f %% |", 100.0 * L.cells[m][v].deafMs / (static_cast<double>(L.hours) * kHour));
            }
            std::printf("\n");
        }
        std::printf("\n");
    }

    // 13. Human cost
    std::printf("## 13. Cost to a person\n\n");
    std::printf("Formula: 0.6 s quiet + 0.5 s per tap (gloves) + the hold and ~0.3 s to let go. Simulation: %u attempts "
                "per lock and profile through the same detector and frame pacing, starting from a quiet watch, each "
                "with its own random stream. A person retries after a rejection (1.5 s to read it), restarts after a "
                "\"too slow\" timeout, and waits out a cool-down. The profiles are invented, not measured.\n\n",
                kHumanTrials);
    for (const HumanProfile& h : kHumans) {
        std::printf("- **%s**: tap %.0f ms median (sigma %.2f), gap %.0f ms median (sigma %.2f), %.0f%% of gaps get an "
                    "extra 1.5-4 s pause, %.1f%% of presses slip (half another button, half a chord), %.0f%% of holds let "
                    "go early, let go %.0f ms after the hold indicator completes.\n",
                    h.name, h.pressMedMs, h.pressSigma, h.gapMedMs, h.gapSigma, 100 * h.hesitateP, 100 * h.slipP,
                    100 * h.earlyP, h.reactMedMs);
    }
    std::printf("\n| Lock | Presses | Formula (gloves) | Profile | First try succeeds | Median time | 95th pct time "
                "| Mean presses used | Hit a cool-down | Not unlocked in 2 min |\n");
    std::printf("|---|---:|---:|---|---:|---:|---:|---:|---:|---:|\n");
    for (size_t i = 0; i < humanSet.size(); ++i) {
        const Variant& var = vars[humanSet[i]];
        const double formula = 0.6 + 0.5 * var.taps() + (var.holdMs ? var.holdMs / 1000.0 + 0.3 : 0.0);
        for (size_t p = 0; p < kProfiles; ++p) {
            const HumanStats& st = human[i * kProfiles + p];
            std::printf("| %s | %s | %.1f s | %s | %.1f %% | %.1f s | %.1f s | %.2f | %s | %s |\n", var.label.c_str(),
                        var.presses().c_str(), formula, kHumans[p].name, 100.0 * st.firstTry / st.trials,
                        quantile(st, 0.5), quantile(st, 0.95), static_cast<double>(st.presses) / st.trials,
                        percent(st.cooled, st.trials).c_str(), percent(st.failed, st.trials).c_str());
        }
    }
    std::printf("\nTimes run from the first press to the unlock; add up to 0.6 s if a button was touched just before "
                "(the quiet rule). Ways the rules reject a careful person: a pause over 2.5 s between steps (timeout), "
                "a press over 1.2 s (gloves), a second button brushed while pressing (chord), starting within 0.6 s of "
                "touching anything (not quiet), and with a hold: letting go before H, or holding past 4 s. Three of these "
                "within 30 s start a cool-down: 15 s of not touching anything. A hold also needs an indicator on screen "
                "that says when it is long enough.\n\n");

    // 14. Notes
    std::printf("## 14. Model notes\n\n");
    std::printf("- The harsh level is the models as written; the calibrated level divides every interval between noise "
                "events by the rate scale in §1 (press lengths and taps within a burst unchanged); x2 and x4 multiply "
                "that scale.\n");
    std::printf("- Each 2-min consequence window starts on the watch face with nothing open, so activities opened are "
                "overstated for very weak locks (an earlier one would still be open) and recordings understated. On the "
                "stock apps an activity opened in a window stays open, GPS on, behind the relocked screen; only its "
                "opening is counted here, not its GPS time.\n");
    std::printf("- Consequence intervals are Poisson intervals on the count, slightly narrow because opens cluster in "
                "windows.\n");
    std::printf("- Every lock variant sees the same rides, so differences between locks are not blurred by different "
                "noise, but their counts are correlated.\n");
    std::printf("- `Taps+Holds` and `Rocker` are new in this study; the other six are the models of "
                "docs/jacket-report.md. All eight are invented; nobody has recorded what a cuff does.\n");
    return 0;
}
