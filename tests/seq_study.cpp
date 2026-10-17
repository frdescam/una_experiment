/**
 * @file    seq_study.cpp
 * @brief   How short can the unlock sequence be? Monte Carlo study.
 *
 *   seq_study [hostile-hours] [calibrated-hours] [threads] [seed]
 *
 * For each lock variant (6, 5, 4 steps; every valid 3-step sequence; 2 and 3
 * steps finished by a hold; a naive 3-click lock) and each jacket model it
 * replays one-hour rides at two noise levels:
 *
 *   hostile     the models as written (an hour of sustained noise);
 *   calibrated  each model's event rate scaled down until the stock watch
 *               opens about one activity per hour.
 *
 * and counts, per 1000 h: false unlocks (all rules on), and what the noise
 * then does on the watch face while the lock is down (2 min until relock):
 * activities opened (GPS on) and recordings started, on the stock apps and on
 * the patched apps (5 min budget, 1.5 s hold of R1 alone to start).
 * It also simulates a person unlocking (bare hands and gloves) through the
 * same detector, and the share of time the cool-down makes the lock deaf.
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "JacketModel.hpp"
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

uint64_t rideSeed(uint64_t seed, unsigned stream, Model m, uint64_t ride)
{
    return mix(seed ^ mix((static_cast<uint64_t>(stream) << 40) ^ (static_cast<uint64_t>(m) << 32) ^ ride));
}

// --- names -----------------------------------------------------------------------

const char* sdkName(Button b)
{
    switch (b) {
        case Button::Up: return "UP";
        case Button::Select: return "SELECT";
        case Button::Down: return "DOWN";
        case Button::Back: return "BACK";
    }
    return "?";
}

std::string seqText(const Sequence& s)
{
    char buf[40];
    ButtonLock::formatSequence(s, buf, sizeof(buf));
    return buf;
}

std::string seqSdk(const Sequence& s)
{
    std::string out;
    for (size_t i = 0; i < s.length; ++i) {
        if (i > 0) {
            out += ' ';
        }
        out += sdkName(s[i]);
    }
    return out;
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

/// ABA with A/B in the same row, ABA on a diagonal, or three different buttons.
std::string shapeOf(const Sequence& s)
{
    auto row  = [](Button b) { return (b == Button::Up || b == Button::Select) ? 0 : 1; };
    auto side = [](Button b) { return (b == Button::Up || b == Button::Down) ? 0 : 1; };
    std::string out;
    for (size_t i = 1; i < s.length; ++i) {
        const Button a = s[i - 1], b = s[i];
        const char c = (side(a) == side(b)) ? '|' : (row(a) == row(b) ? '-' : 'x');
        out += c;
    }
    if (s.length == 3) {
        if (s[0] == s[2]) {
            return out[0] == '-' ? "ABA row" : "ABA diagonal";
        }
        return "ABC " + out;
    }
    return out;
}

// --- variants --------------------------------------------------------------------

enum class Family : uint8_t { Six, Five, Four, Three, Two, TwoHold, ThreeHold, Naive };

struct Variant {
    std::string  label;
    Family       family;
    uint32_t     holdMs;
    Sequence     seq;
    UnlockTiming timing;
    uint32_t     clicks() const { return holdMs > 0 ? seq.length - 1u : seq.length; }
    std::string  presses() const
    {
        char buf[40];
        if (holdMs > 0) {
            std::snprintf(buf, sizeof(buf), "%u (%u tap%s + hold)", seq.length, clicks(), clicks() == 1 ? "" : "s");
        } else {
            std::snprintf(buf, sizeof(buf), "%u", seq.length);
        }
        return buf;
    }
};

Sequence parse(const char* text)
{
    Sequence s;
    if (!ButtonLock::parseSequence(text, s)) {
        std::fprintf(stderr, "bad sequence %s\n", text);
        std::exit(2);
    }
    return s;
}

/// Every sequence of @p len steps the app accepts (both sides, no repeats).
std::vector<Sequence> allSequences(size_t len)
{
    static const char* names[4] = { "L1", "R1", "L2", "R2" };
    size_t total = 1;
    for (size_t i = 0; i < len; ++i) {
        total *= 4;
    }
    std::vector<Sequence> out;
    for (size_t code = 0; code < total; ++code) {
        std::string text;
        size_t div = total / 4;
        for (size_t i = 0; i < len; ++i, div /= 4) {
            text += names[(code / div) % 4];
            text += ' ';
        }
        Sequence s;
        if (ButtonLock::parseSequence(text.c_str(), s)) {
            out.push_back(s);
        }
    }
    return out;
}

std::vector<Variant> makeVariants()
{
    std::vector<Variant> v;
    const UnlockTiming def;

    auto add = [&](Family f, const Sequence& s, uint32_t hold, const UnlockTiming& base) {
        UnlockTiming t = base;
        t.finalHoldMs = hold;
        std::string label = seqText(s);
        if (hold > 0) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), " + hold %.1f s", hold / 1000.0);
            label += buf;
        }
        v.push_back(Variant{ label, f, hold, s, t });
    };

    add(Family::Six, ButtonLock::defaultSequence(), 0, def);
    add(Family::Five, parse("L1 R1 R2 L2 L1"), 0, def);
    for (const char* four : { "L1 R1 R2 L2", "L1 R2 L2 R1", "L1 R2 R1 L2", "L1 R1 L2 R2",
                              "L1 L2 R1 R2", "L1 R1 L1 R1", "L1 R2 L1 R2" }) {
        add(Family::Four, parse(four), 0, def);
    }
    for (const Sequence& s : allSequences(3)) {
        add(Family::Three, s, 0, def);
    }
    add(Family::Two, parse("L1 R1"), 0, def);
    add(Family::Two, parse("L1 R2"), 0, def);
    for (uint32_t hold : { 800u, 1000u }) {
        for (const Sequence& s : allSequences(2)) {
            add(Family::TwoHold, s, hold, def);
        }
    }
    for (uint32_t hold : { 800u, 1000u }) {
        for (const Sequence& s : allSequences(3)) {
            add(Family::ThreeHold, s, hold, def);
        }
    }

    UnlockTiming naive;   // any three order-correct clicks, any timing
    naive.quietMs     = 0;
    naive.stuckMs     = 0;
    naive.maxGapMs    = 0xFFFFFFFFu;
    naive.maxPressMs  = 0xFFFFFFFFu;
    naive.rejectLimit = 0;
    add(Family::Naive, parse("L1 R1 R2"), 0, naive);
    v.back().label = "Naive 3-click L1 R1 R2 (no rules)";
    return v;
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
Interval poisson95(uint64_t k)
{
    if (k == 0) {
        return { 0.0, 3.0 };   // the 3/N rule (one-sided 95%)
    }
    const double kd = static_cast<double>(k);
    if (k > 2000) {
        const double z = 1.959964;
        const double lo = kd * std::pow(1.0 - 1.0 / (9.0 * kd) - z / (3.0 * std::sqrt(kd)), 3.0);
        const double k1 = kd + 1.0;
        const double hi = k1 * std::pow(1.0 - 1.0 / (9.0 * k1) + z / (3.0 * std::sqrt(k1)), 3.0);
        return { lo, hi };
    }
    const double target = std::log(0.025);
    // upper: P(X <= k; hi) = 0.025
    double a = kd, b = kd * 3.0 + 10.0;
    for (int it = 0; it < 80; ++it) {
        const double m = 0.5 * (a + b);
        if (logPoissonCdf(k, m) > target) {
            a = m;
        } else {
            b = m;
        }
    }
    const double hi = 0.5 * (a + b);
    // lower: P(X >= k; lo) = 0.025, i.e. P(X <= k-1; lo) = 0.975
    const double target2 = std::log(0.975);
    a = 0.0;
    b = kd;
    for (int it = 0; it < 80; ++it) {
        const double m = 0.5 * (a + b);
        if (m <= 0.0 || logPoissonCdf(k - 1, m) > target2) {
            a = m;
        } else {
            b = m;
        }
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

/// "r (lo-hi)" per 1000 h, or "0 (<u)" for a zero count.
std::string rate(uint64_t k, double hours, bool withInterval = true)
{
    const double per = 1000.0 / hours;
    const Interval ci = poisson95(k);
    if (k == 0) {
        return withInterval ? "0 (<" + num(ci.hi * per) + ")" : "0";
    }
    const std::string r = num(static_cast<double>(k) * per);
    return withInterval ? r + " (" + num(ci.lo * per) + "-" + num(ci.hi * per) + ")" : r;
}

/// Upper end of the 95% interval per 1000 h (the 3/N bound for zero).
double upper(uint64_t k, double hours)
{
    return poisson95(k).hi * 1000.0 / hours;
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
        if (stockOpensPerHour(m, std::exp(mid), hours, seed, threads) < target) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return std::exp(0.5 * (lo + hi));
}

void runModel(unsigned level, Model m, double r, uint32_t hours, uint64_t seed, unsigned threads,
              const std::vector<Variant>& vars, std::vector<Cell>& out, Baseline& base)
{
    std::vector<std::vector<Cell>> local(threads, std::vector<Cell>(vars.size()));
    std::vector<Baseline> lbase(threads);
    const StockOptions stock   = stockOptions();
    const StockOptions patched = patchedOptions();
    const LockedRideOptions lo;

    parallelFor(hours, threads, [&](uint32_t ride, unsigned th) {
        Rng rng(rideSeed(seed, level, m, ride));
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

        for (size_t v = 0; v < vars.size(); ++v) {
            ButtonLock::UnlockDetector d(vars[v].seq, vars[v].timing);
            local[th][v].add(runLockedRide(d, delivered, kernel, kHour, lo, stock, patched));
        }
    });

    out.assign(vars.size(), Cell{});
    base = Baseline{};
    for (unsigned t = 0; t < threads; ++t) {
        for (size_t v = 0; v < vars.size(); ++v) {
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

HumanStats simulateHuman(const Variant& v, const HumanProfile& h, uint32_t trials, uint64_t seed)
{
    HumanStats st;
    for (uint32_t trial = 0; trial < trials; ++trial) {
        Rng rng(mix(seed + trial));
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
            const uint32_t timeoutAt = lastT + d.timing().maxGapMs + kFrameMs;
            if (d.progress() > 0 && next > timeoutAt) {
                // The guide says "too slow": start over after reading it.
                d.poll(timeoutAt);
                lastFrame = std::max(lastFrame, timeoutAt);
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
    return st;
}

// --- printing --------------------------------------------------------------------

struct Level {
    const char*          name;
    std::vector<double>  rate;    // per model
    uint32_t             hours;
    std::vector<std::vector<Cell>> cells;   // [model][variant]
    std::vector<Baseline> base;            // [model]
};

const std::vector<Model> kModels(kStudyModels.begin(), kStudyModels.end());

uint64_t sumUnlocks(const Level& L, size_t v)
{
    uint64_t s = 0;
    for (size_t m = 0; m < kModels.size(); ++m) {
        s += L.cells[m][v].unlocks;
    }
    return s;
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

void printHeader(const char* first)
{
    std::printf("| %s |", first);
    for (Model m : kModels) {
        std::printf(" %s |", shortModel(m));
    }
    std::printf(" all models pooled |\n|---|");
    for (size_t m = 0; m <= kModels.size(); ++m) {
        std::printf("---:|");
    }
    std::printf("\n");
}

void printUnlockRow(const Level& L, const Variant& var, size_t v, const std::string& label)
{
    std::printf("| %s |", label.c_str());
    for (size_t m = 0; m < kModels.size(); ++m) {
        std::printf(" %s |", rate(L.cells[m][v].unlocks, L.hours).c_str());
    }
    std::printf(" %s |\n", rate(sumUnlocks(L, v), static_cast<double>(L.hours) * kModels.size()).c_str());
    (void)var;
}

void printConsequenceRow(const Level& L, size_t v, const std::string& label, uint64_t Cell::*field)
{
    std::printf("| %s |", label.c_str());
    uint64_t total = 0;
    for (size_t m = 0; m < kModels.size(); ++m) {
        std::printf(" %s |", rate(L.cells[m][v].*field, L.hours).c_str());
        total += L.cells[m][v].*field;
    }
    std::printf(" %s |\n", rate(total, static_cast<double>(L.hours) * kModels.size()).c_str());
}

double chiDispersionP(const std::vector<uint64_t>& counts, double* ratio)
{
    // Index of dispersion: sum (c - mean)^2 / mean ~ chi2(n-1) under equal rates.
    const double n = static_cast<double>(counts.size());
    double mean = 0.0;
    for (uint64_t c : counts) {
        mean += static_cast<double>(c);
    }
    mean /= n;
    if (mean <= 0.0) {
        *ratio = 0.0;
        return 1.0;
    }
    double d = 0.0;
    for (uint64_t c : counts) {
        d += (static_cast<double>(c) - mean) * (static_cast<double>(c) - mean) / mean;
    }
    const double df = n - 1.0;
    *ratio = d / df;
    const double z = (std::cbrt(d / df) - (1.0 - 2.0 / (9.0 * df))) / std::sqrt(2.0 / (9.0 * df));
    return 0.5 * std::erfc(z / std::sqrt(2.0));
}

} // namespace

int main(int argc, char** argv)
{
    const uint32_t hostileHours = argc > 1 ? static_cast<uint32_t>(std::atoi(argv[1])) : 2000u;
    const uint32_t calibHours   = argc > 2 ? static_cast<uint32_t>(std::atoi(argv[2])) : hostileHours;
    unsigned threads = argc > 3 ? static_cast<unsigned>(std::atoi(argv[3])) : std::thread::hardware_concurrency();
    if (threads == 0) {
        threads = 4;
    }
    const uint64_t seed = argc > 4 ? std::strtoull(argv[4], nullptr, 0) : 0x5E9u;
    const auto t0 = std::chrono::steady_clock::now();

    const std::vector<Variant> vars = makeVariants();

    // Calibration: the stock watch opens ~1 activity per hour.
    constexpr double   kTargetOpens = 1.0;
    constexpr uint32_t kCalibHours  = 3000;
    std::vector<double> calRate;
    for (Model m : kModels) {
        calRate.push_back(calibrate(m, kTargetOpens, kCalibHours, seed, threads));
    }

    std::vector<Level> levels = {
        { "hostile (models as written)", std::vector<double>(kModels.size(), 1.0), hostileHours, {}, {} },
        { "calibrated (stock watch opens ~1 activity/h)", calRate, calibHours, {}, {} },
    };
    for (unsigned l = 0; l < levels.size(); ++l) {
        Level& L = levels[l];
        L.cells.resize(kModels.size());
        L.base.resize(kModels.size());
        for (size_t m = 0; m < kModels.size(); ++m) {
            runModel(l + 1, kModels[m], L.rate[m], L.hours, seed, threads, vars, L.cells[m], L.base[m]);
            std::fprintf(stderr, "level %u model %s done\n", l, shortModel(kModels[m]));
        }
    }

    // Human trials.
    std::vector<size_t> humanVars;
    auto findVar = [&](const std::string& label) {
        for (size_t v = 0; v < vars.size(); ++v) {
            if (vars[v].label == label) {
                return v;
            }
        }
        std::fprintf(stderr, "no variant %s\n", label.c_str());
        std::exit(2);
    };
    for (const char* label : { "L1 R1 R2 L2 L1 R1", "L1 R1 R2 L2 L1", "L1 R1 R2 L2", "L1 R1 R2", "L1 R1",
                               "L1 R1 + hold 0.8 s", "L1 R1 + hold 1.0 s", "L1 R1 R2 + hold 0.8 s",
                               "L1 R1 R2 + hold 1.0 s" }) {
        humanVars.push_back(findVar(label));
    }
    constexpr uint32_t kHumanTrials = 20000;
    std::vector<std::vector<HumanStats>> human(humanVars.size());
    for (size_t i = 0; i < humanVars.size(); ++i) {
        for (const HumanProfile& h : kHumans) {
            human[i].push_back(simulateHuman(vars[humanVars[i]], h, kHumanTrials, seed + 0x1000 * i));
        }
    }

    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    // ------------------------------------------------------------------------------
    std::printf("# Unlock sequence study\n\n");
    std::printf("Generated by `tests/seq_study.cpp`: `seq_study %u %u %u 0x%llx` (%u threads, %.0f s).\n\n",
                hostileHours, calibHours, threads, static_cast<unsigned long long>(seed), threads, elapsed);
    std::printf("Every variant runs on the same one-hour rides (common random numbers), %u h per jacket model "
                "at the hostile level and %u h per model at the calibrated level: %u models, %zu lock variants. "
                "All of ButtonLock's rules are on (quiet 0.6 s, press < 1.2 s, gap < 2.5 s, chords end the attempt, "
                "3 rejections in 30 s -> cool-down until 15 s of silence, pinned > 4 s ignored). The lock sees the "
                "frame-paced stream (10 Hz, 16-deep queue). After a false unlock the open padlock shows for 0.7 s, then the "
                "watch face has the buttons for **2 min** (relockMs) and the same noise is fed to the stock-watch model and "
                "to the patched-apps model (5 min pre-activity budget, start = 1.5 s hold of R1 alone); the lock is then "
                "back up (detector reset).\n\n",
                hostileHours, calibHours, static_cast<unsigned>(kModels.size()), vars.size());
    std::printf("Rates are **per 1000 h** of that noise. `r (lo-hi)` is the Poisson 95%% interval; `0 (<u)` is a zero count "
                "with the 3/N upper bound. Button names: L1 = UP (top left), R1 = SELECT (top right), "
                "L2 = DOWN (bottom left), R2 = BACK (bottom right).\n\n");

    // 1. Noise levels
    std::printf("## 1. Noise levels\n\n");
    std::printf("Calibration: for each model, the event rate (all intervals between noise events divided by the scale) "
                "was bisected on %u h so the stock watch, with no lock, opens %.1f activity per hour. The table shows "
                "what the main runs then measured.\n\n", kCalibHours, kTargetOpens);
    std::printf("| Model | Hostile: presses/h | Hostile: stock opens/h | Hostile: stock recordings/h | Hostile: patched recordings/h "
                "| Calibrated: rate scale | Calibrated: presses/h | Calibrated: stock opens/h | Calibrated: stock recordings/h "
                "| Calibrated: patched recordings/h |\n");
    std::printf("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n");
    for (size_t m = 0; m < kModels.size(); ++m) {
        const Baseline& a = levels[0].base[m];
        const Baseline& b = levels[1].base[m];
        const double ha = levels[0].hours, hb = levels[1].hours;
        std::printf("| %s | %.0f | %.2f | %.3f | %.3f | %.3f | %.0f | %.2f | %.3f | %.4f |\n", modelName(kModels[m]),
                    a.presses / ha, a.opens / ha, a.recs / ha, a.pRecs / ha, levels[1].rate[m], b.presses / hb,
                    b.opens / hb, b.recs / hb, b.pRecs / hb);
    }
    std::printf("\n");

    // 2. Headline
    const std::vector<std::string> headline = {
        "L1 R1 R2 L2 L1 R1", "L1 R1 R2 L2 L1", "L1 R1 R2 L2", "L1 R2 L2 R1", "L1 R1 R2", "L1 R2 L1", "L1 R1 L1",
        "L1 R1", "L1 R1 + hold 0.8 s", "L1 R1 + hold 1.0 s", "L1 R2 + hold 1.0 s",
        "L1 R1 R2 + hold 0.8 s", "L1 R1 R2 + hold 1.0 s", "Naive 3-click L1 R1 R2 (no rules)",
    };
    std::printf("## 2. Headline: candidate locks\n\n");
    std::printf("Worst model = the jacket model with the highest count for that column. Pooled = all %zu models "
                "together (an even mix). Consequences are what the noise did on the watch face during the 2-min "
                "windows after false unlocks.\n\n", kModels.size());
    for (const Level& L : levels) {
        std::printf("### %s\n\n", L.name);
        std::printf("| Lock | Presses | False unlocks, worst model | False unlocks, pooled | Stock: activities opened, worst model "
                    "| Stock: activities opened, pooled | Stock: recordings started, worst model | Patched apps: recordings, worst model |\n");
        std::printf("|---|---:|---:|---:|---:|---:|---:|---:|\n");
        {
            size_t wo = 0, wr = 0, wp = 0;
            uint64_t oAll = 0;
            for (size_t m = 0; m < kModels.size(); ++m) {
                if (L.base[m].opens > L.base[wo].opens) wo = m;
                if (L.base[m].recs > L.base[wr].recs) wr = m;
                if (L.base[m].pRecs > L.base[wp].pRecs) wp = m;
                oAll += L.base[m].opens;
            }
            std::printf("| *No lock: stock watch* | - | - | - | %s %s | %s | %s %s | %s %s |\n",
                        rate(L.base[wo].opens, L.hours).c_str(), shortModel(kModels[wo]),
                        rate(oAll, static_cast<double>(L.hours) * kModels.size()).c_str(),
                        rate(L.base[wr].recs, L.hours).c_str(), shortModel(kModels[wr]),
                        rate(L.base[wp].pRecs, L.hours).c_str(), L.base[wp].pRecs ? shortModel(kModels[wp]) : "");
        }
        for (const std::string& label : headline) {
            const size_t v = findVar(label);
            const size_t wu = worstModel(L, v, &Cell::unlocks);
            const size_t wo = worstModel(L, v, &Cell::opens);
            const size_t wr = worstModel(L, v, &Cell::recs);
            const size_t wp = worstModel(L, v, &Cell::pRecs);
            uint64_t opensAll = 0;
            for (size_t m = 0; m < kModels.size(); ++m) {
                opensAll += L.cells[m][v].opens;
            }
            std::printf("| %s | %s | %s %s | %s | %s %s | %s | %s %s | %s %s |\n", label.c_str(), vars[v].presses().c_str(),
                        rate(L.cells[wu][v].unlocks, L.hours).c_str(), L.cells[wu][v].unlocks ? shortModel(kModels[wu]) : "",
                        rate(sumUnlocks(L, v), static_cast<double>(L.hours) * kModels.size()).c_str(),
                        rate(L.cells[wo][v].opens, L.hours).c_str(), L.cells[wo][v].opens ? shortModel(kModels[wo]) : "",
                        rate(opensAll, static_cast<double>(L.hours) * kModels.size()).c_str(),
                        rate(L.cells[wr][v].recs, L.hours).c_str(), L.cells[wr][v].recs ? shortModel(kModels[wr]) : "",
                        rate(L.cells[wp][v].pRecs, L.hours).c_str(), L.cells[wp][v].pRecs ? shortModel(kModels[wp]) : "");
        }
        std::printf("\n");
    }

    // 3. False unlocks per model, every variant except 3+hold (summarised in §5)
    std::printf("## 3. False unlocks per 1000 h, per model\n\n");
    for (const Level& L : levels) {
        std::printf("### %s (%u h per model)\n\n", L.name, L.hours);
        printHeader("Lock");
        for (size_t v = 0; v < vars.size(); ++v) {
            if (vars[v].family == Family::ThreeHold) {
                continue;
            }
            printUnlockRow(L, vars[v], v, vars[v].label);
        }
        std::printf("\n");
    }

    // 3b. Every variant: false unlocks and consequences, pooled and worst model
    std::printf("## 3b. Every lock: false unlocks and their consequences (per 1000 h; worst model, pooled)\n\n");
    for (const Level& L : levels) {
        std::printf("### %s\n\n", L.name);
        std::printf("| Lock | False unlocks, worst | pooled | Stock opens, worst | pooled | Stock recordings, worst | pooled "
                    "| Patched recordings, worst | pooled |\n|---|---:|---:|---:|---:|---:|---:|---:|---:|\n");
        const double pooledH = static_cast<double>(L.hours) * kModels.size();
        for (size_t v = 0; v < vars.size(); ++v) {
            std::printf("| %s |", vars[v].label.c_str());
            for (uint64_t Cell::*f : { &Cell::unlocks, &Cell::opens, &Cell::recs, &Cell::pRecs }) {
                const size_t w = worstModel(L, v, f);
                uint64_t all = 0;
                for (size_t m = 0; m < kModels.size(); ++m) {
                    all += L.cells[m][v].*f;
                }
                std::printf(" %s %s | %s |", rate(L.cells[w][v].*f, L.hours, false).c_str(),
                            L.cells[w][v].*f ? shortModel(kModels[w]) : "", rate(all, pooledH).c_str());
            }
            std::printf("\n");
        }
        std::printf("\n");
    }

    // 4. Consequences per model for the headline variants
    std::printf("## 4. Consequences per 1000 h, per model (headline locks)\n\n");
    struct Metric {
        const char* title;
        uint64_t Cell::*field;
    };
    for (const Metric& metric : { Metric{ "Stock watch: activities opened (GPS on)", &Cell::opens },
                                  Metric{ "Stock watch: recordings started", &Cell::recs },
                                  Metric{ "Patched apps: recordings started", &Cell::pRecs } }) {
        for (const Level& L : levels) {
            std::printf("### %s, %s\n\n", metric.title, L.name);
            printHeader("Lock");
            for (const std::string& label : headline) {
                printConsequenceRow(L, findVar(label), label, metric.field);
            }
            std::printf("\n");
        }
    }

    // 5. 3-step ranking
    std::printf("## 5. Every valid 3-step sequence, ranked\n\n");
    std::printf("Ranked by the calibrated pooled count, then the hostile pooled count. Shape: ABA row (A, B on the same row), "
                "ABA diagonal, or ABC with each move marked - (across a row), x (diagonal), | (same side).\n\n");
    {
        std::vector<size_t> three;
        for (size_t v = 0; v < vars.size(); ++v) {
            if (vars[v].family == Family::Three) {
                three.push_back(v);
            }
        }
        std::sort(three.begin(), three.end(), [&](size_t a, size_t b) {
            const uint64_t ca = sumUnlocks(levels[1], a), cb = sumUnlocks(levels[1], b);
            if (ca != cb) {
                return ca < cb;
            }
            return sumUnlocks(levels[0], a) < sumUnlocks(levels[0], b);
        });
        const double pooledH0 = static_cast<double>(levels[0].hours) * kModels.size();
        const double pooledH1 = static_cast<double>(levels[1].hours) * kModels.size();
        std::printf("| # | Sequence | SDK names | Shape | Hostile pooled | Hostile worst | Hostile Rocker | Calibrated pooled "
                    "| Calibrated worst | Calibrated stock opens, pooled |\n");
        std::printf("|---:|---|---|---|---:|---:|---:|---:|---:|---:|\n");
        int rank = 0;
        for (size_t v : three) {
            const size_t w0 = worstModel(levels[0], v, &Cell::unlocks);
            const size_t w1 = worstModel(levels[1], v, &Cell::unlocks);
            size_t rocker = 0;
            for (size_t m = 0; m < kModels.size(); ++m) {
                if (kModels[m] == Model::Rocker) {
                    rocker = m;
                }
            }
            uint64_t opens1 = 0;
            for (size_t m = 0; m < kModels.size(); ++m) {
                opens1 += levels[1].cells[m][v].opens;
            }
            std::printf("| %d | %s | %s | %s | %s | %s %s | %s | %s | %s %s | %s |\n", ++rank, vars[v].label.c_str(),
                        seqSdk(vars[v].seq).c_str(), shapeOf(vars[v].seq).c_str(),
                        rate(sumUnlocks(levels[0], v), pooledH0).c_str(),
                        rate(levels[0].cells[w0][v].unlocks, levels[0].hours).c_str(), shortModel(kModels[w0]),
                        rate(levels[0].cells[rocker][v].unlocks, levels[0].hours).c_str(),
                        rate(sumUnlocks(levels[1], v), pooledH1).c_str(),
                        rate(levels[1].cells[w1][v].unlocks, levels[1].hours).c_str(),
                        levels[1].cells[w1][v].unlocks ? shortModel(kModels[w1]) : "",
                        rate(opens1, pooledH1).c_str());
        }
        std::printf("\n**Do the 3-step sequences really differ?** Index of dispersion of the 32 counts per model "
                    "(variance/mean; 1 = what equal rates give) and the p-value of equal rates (chi-square, Wilson-Hilferty). "
                    "The sequences share the same rides, so this is indicative.\n\n");
        std::printf("| Level | Model | All 32: dispersion | All 32: p | The 24 ABC only: dispersion | ABC only: p |\n");
        std::printf("|---|---|---:|---:|---:|---:|\n");
        for (const Level& L : levels) {
            for (size_t m = 0; m < kModels.size(); ++m) {
                std::vector<uint64_t> all, noRow;
                for (size_t v : three) {
                    all.push_back(L.cells[m][v].unlocks);
                    if (shapeOf(vars[v].seq).rfind("ABC", 0) == 0) {
                        noRow.push_back(L.cells[m][v].unlocks);
                    }
                }
                double r1 = 0, r2 = 0;
                const double p1 = chiDispersionP(all, &r1);
                const double p2 = chiDispersionP(noRow, &r2);
                std::printf("| %s | %s | %.2f | %.2g | %.2f | %.2g |\n", L.name, shortModel(kModels[m]), r1, p1, r2, p2);
            }
        }
        std::printf("\n");

        // shape classes pooled
        std::printf("By shape (mean per sequence of the class, per 1000 h, pooled over models):\n\n");
        std::printf("| Shape class | Sequences | Hostile | Calibrated |\n|---|---:|---:|---:|\n");
        std::map<std::string, std::pair<uint64_t, uint64_t>> byClass;
        std::map<std::string, int> nClass;
        for (size_t v : three) {
            std::string cls = shapeOf(vars[v].seq);
            if (cls.rfind("ABC", 0) == 0) {
                cls = "ABC (three buttons)";
            }
            byClass[cls].first += sumUnlocks(levels[0], v);
            byClass[cls].second += sumUnlocks(levels[1], v);
            nClass[cls]++;
        }
        for (const auto& [cls, c] : byClass) {
            const int n = nClass[cls];
            std::printf("| %s | %d | %s | %s |\n", cls.c_str(), n, rate(c.first, pooledH0 * n).c_str(),
                        rate(c.second, pooledH1 * n).c_str());
        }
        std::printf("\n");
    }

    // 6. Hold to finish
    std::printf("## 6. Hold to finish\n\n");
    std::printf("The last step must be held alone for H and released before 4 s (stuckMs); another press during it is a "
                "chord. Only `Taps+Holds`, `OneSide` and `Chaos` ever produce presses over 0.5 s on their own, and "
                "`OneSide` cannot produce a two-sided sequence, so the hold is tested mainly by `Taps+Holds` "
                "(a quarter of all random presses held 0.5-3 s).\n\n");
    for (const Level& L : levels) {
        std::printf("### %s: family summary (per sequence, per 1000 h)\n\n", L.name);
        std::printf("| Family | H | Sequences | Pooled per sequence | Worst sequence on its worst model | Taps+Holds per sequence |\n");
        std::printf("|---|---:|---:|---:|---:|---:|\n");
        size_t th = 0;
        for (size_t m = 0; m < kModels.size(); ++m) {
            if (kModels[m] == Model::TapsAndHolds) {
                th = m;
            }
        }
        for (Family f : { Family::Three, Family::TwoHold, Family::ThreeHold }) {
            for (uint32_t hold : { 0u, 800u, 1000u }) {
                if ((f == Family::Three) != (hold == 0)) {
                    continue;
                }
                uint64_t pooled = 0, thCount = 0, worst = 0;
                size_t worstV = 0, worstM = 0;
                int n = 0;
                for (size_t v = 0; v < vars.size(); ++v) {
                    if (vars[v].family != f || vars[v].holdMs != hold) {
                        continue;
                    }
                    ++n;
                    pooled += sumUnlocks(L, v);
                    thCount += L.cells[th][v].unlocks;
                    for (size_t m = 0; m < kModels.size(); ++m) {
                        if (L.cells[m][v].unlocks >= worst) {
                            worst = L.cells[m][v].unlocks;
                            worstV = v;
                            worstM = m;
                        }
                    }
                }
                const char* fname = f == Family::Three ? "3 clicks" : (f == Family::TwoHold ? "2 + hold" : "3 + hold");
                std::printf("| %s | %s | %d | %s | %s (%s, %s) | %s |\n", fname, hold ? (hold == 800 ? "0.8 s" : "1.0 s") : "-",
                            n, rate(pooled, static_cast<double>(L.hours) * kModels.size() * n).c_str(),
                            rate(worst, L.hours).c_str(), vars[worstV].label.c_str(), shortModel(kModels[worstM]),
                            rate(thCount, static_cast<double>(L.hours) * n).c_str());
            }
        }
        std::printf("\n");
    }
    for (const Level& L : levels) {
        std::printf("### %s: 3 + hold, per model (false unlocks per 1000 h)\n\n", L.name);
        printHeader("Lock");
        for (size_t v = 0; v < vars.size(); ++v) {
            if (vars[v].family == Family::ThreeHold) {
                printUnlockRow(L, vars[v], v, vars[v].label);
            }
        }
        std::printf("\n");
    }

    // 7. Deafness
    std::printf("## 7. Share of time the lock is cooling down (a person is told to wait)\n\n");
    for (const Level& L : levels) {
        std::printf("### %s\n\n", L.name);
        std::printf("| Lock |");
        for (Model m : kModels) {
            std::printf(" %s |", shortModel(m));
        }
        std::printf("\n|---|");
        for (size_t m = 0; m < kModels.size(); ++m) {
            std::printf("---:|");
        }
        std::printf("\n");
        for (const char* label : { "L1 R1 R2 L2 L1 R1", "L1 R1 R2 L2", "L1 R1 R2", "L1 R1 + hold 1.0 s", "L1 R1 R2 + hold 1.0 s" }) {
            const size_t v = findVar(label);
            std::printf("| %s |", label);
            for (size_t m = 0; m < kModels.size(); ++m) {
                std::printf(" %.1f %% |", 100.0 * L.cells[m][v].deafMs / (static_cast<double>(L.hours) * kHour));
            }
            std::printf("\n");
        }
        std::printf("\n");
    }

    // 8. Human cost
    std::printf("## 8. Cost to a person\n\n");
    std::printf("Formula: 0.6 s quiet + 0.5 s per click (gloves) + the hold and ~0.3 s to let go. Simulation: %u attempts "
                "per profile through the same detector and frame pacing, starting from a quiet watch. A person retries "
                "after a rejection (1.5 s to read it), restarts after a \"too slow\" timeout, and waits out a cool-down.\n\n",
                kHumanTrials);
    for (const HumanProfile& h : kHumans) {
        std::printf("- **%s**: tap %.0f ms median (sigma %.2f), gap %.0f ms median (sigma %.2f), %.0f%% of gaps get an "
                    "extra 1.5-4 s pause, %.1f%% of presses slip (half another button, half a chord), %.0f%% of holds let go "
                    "early, let go %.0f ms after the hold indicator completes.\n",
                    h.name, h.pressMedMs, h.pressSigma, h.gapMedMs, h.gapSigma, 100 * h.hesitateP, 100 * h.slipP,
                    100 * h.earlyP, h.reactMedMs);
    }
    std::printf("\n| Lock | Presses | Formula (gloves) | Profile | First try succeeds | Median time | 95th pct time "
                "| Mean presses used | Hit a cool-down | Not unlocked in 2 min |\n");
    std::printf("|---|---:|---:|---|---:|---:|---:|---:|---:|---:|\n");
    for (size_t i = 0; i < humanVars.size(); ++i) {
        const Variant& var = vars[humanVars[i]];
        const double formula = 0.6 + 0.5 * var.clicks() + (var.holdMs ? var.holdMs / 1000.0 + 0.3 : 0.0);
        for (size_t p = 0; p < std::size(kHumans); ++p) {
            HumanStats st = human[i][p];
            std::sort(st.timesS.begin(), st.timesS.end());
            const double med = st.timesS.empty() ? 0 : st.timesS[st.timesS.size() / 2];
            const double p95 = st.timesS.empty() ? 0 : st.timesS[st.timesS.size() * 95 / 100];
            std::printf("| %s | %s | %.1f s | %s | %.1f %% | %.1f s | %.1f s | %.2f | %.2f %% | %.3f %% |\n",
                        var.label.c_str(), var.presses().c_str(), formula, kHumans[p].name, 100.0 * st.firstTry / st.trials, med, p95,
                        static_cast<double>(st.presses) / st.trials, 100.0 * st.cooled / st.trials,
                        100.0 * st.failed / st.trials);
        }
    }
    std::printf("\nTimes run from the first press to the unlock; add up to 0.6 s if a button was touched just before "
                "(the quiet rule). Ways the rules reject a careful person: a pause over 2.5 s between steps (timeout), "
                "a press over 1.2 s (gloves), a second button brushed while pressing (chord), starting within 0.6 s of "
                "touching anything (not quiet), and with a hold: letting go before H, or holding past 4 s. Three of these "
                "within 30 s start a cool-down: 15 s of not touching anything.\n\n");

    // 9. Notes
    std::printf("## 9. Model notes\n\n");
    std::printf("- The hostile level is the original models; the calibrated level divides every interval between noise events "
                "by the rate scale in §1 (press lengths and taps within a burst unchanged).\n");
    std::printf("- Each 2-min window starts on the watch face (Home) with nothing open; an activity opened in a window stays "
                "open behind the lock when it comes back (only its opening is counted, not its GPS time).\n");
    std::printf("- Consequence intervals are Poisson intervals on the count, slightly narrow because opens cluster in "
                "windows.\n");
    std::printf("- `Taps+Holds` and `Rocker` are new in this study; the other six are the models of docs/jacket-report.md.\n");
    return 0;
}
