// seq_crosscheck.cpp - independent cross-check of accidental unlocks against
// sequence length (docs/sequence-study.md, section 5).
//
// Written from scratch, separately from JacketModel.hpp and seq_study.cpp: its
// own RNG, its own noise generators written from the models' *description*
// (random taps, road bumps), and its own kernel-event and frame-pacing
// pipeline. Only the real UnlockDetector is shared. Sequences are prefixes of
// UP SELECT BACK DOWN UP SELECT BACK DOWN.
//
//   seq_crosscheck <model: taps|bumps> <hours> <seed> <threads> [nopace]
//
// Prints one line per (rule set, length): unlocks, attempt starts, and for a
// length-8 probe sequence how often progress reached k (k = 1..8). The study
// quotes 2,000,000 h per model.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "ButtonLock/UnlockDetector.hpp"

namespace BL = ButtonLock;

// ---------------------------------------------------------------- RNG
struct Xoshiro {
    uint64_t s[4];
    static uint64_t splitmix(uint64_t& x)
    {
        uint64_t z = (x += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    explicit Xoshiro(uint64_t seed) { for (auto& v : s) v = splitmix(seed); }
    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    uint64_t next()
    {
        const uint64_t r = rotl(s[1] * 5, 7) * 9;
        const uint64_t t = s[1] << 17;
        s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3];
        s[2] ^= t; s[3] = rotl(s[3], 45);
        return r;
    }
    double u01() { return (next() >> 11) * (1.0 / 9007199254740992.0); }    // [0,1)
    uint32_t between(uint32_t lo, uint32_t hi) { return lo + static_cast<uint32_t>(u01() * (hi - lo + 1)); }
    uint32_t expo(double mean) { return static_cast<uint32_t>(-mean * std::log1p(-u01())); }
    bool coin(double p) { return u01() < p; }
};

// ---------------------------------------------------------------- noise
struct Tap { uint8_t b; uint32_t down, up; };

static constexpr uint32_t HOUR = 3600000u;

// Random taps: Poisson, mean 6 s apart, any of 4 buttons, held 40-450 ms.
static void genTaps(Xoshiro& r, std::vector<Tap>& out)
{
    for (uint32_t t = r.expo(6000.0); t < HOUR; t += r.expo(6000.0)) {
        const uint8_t b = static_cast<uint8_t>(r.between(0, 3));
        out.push_back({ b, t, t + r.between(40, 450) });
    }
}

// Bumps: bursts mean 15 s apart (measured from the end of the last burst),
// 2-6 taps each, held 30-350 ms, 60-500 ms release-to-next-press; each next
// tap keeps the button with p = 0.6, else re-draws it uniformly.
static void genBumps(Xoshiro& r, std::vector<Tap>& out)
{
    uint32_t t = 0;
    for (;;) {
        t += r.expo(15000.0);
        if (t >= HOUR) break;
        const uint32_t n = r.between(2, 6);
        uint8_t b = static_cast<uint8_t>(r.between(0, 3));
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t held = r.between(30, 350);
            out.push_back({ b, t, t + held });
            t += held + r.between(60, 500);
            if (!r.coin(0.6)) b = static_cast<uint8_t>(r.between(0, 3));
        }
    }
}

// ---------------------------------------------------------------- kernel + frames
struct Ev { uint32_t t; uint32_t ord; uint8_t b; BL::Input in; };

static void kernelEvents(const std::vector<Tap>& taps, std::vector<Ev>& ev)
{
    uint32_t freeAt[4] = { 0, 0, 0, 0 };
    uint32_t ord = 0;
    ev.clear();
    for (const Tap& p : taps) {             // taps are generated in down order
        if (p.down >= HOUR || p.down < freeAt[p.b]) continue;   // same button still down
        freeAt[p.b] = p.up + 1;
        ev.push_back({ p.down, ord++, p.b, BL::Input::Press });
        if (p.up < HOUR) {
            if (p.up - p.down < 500) ev.push_back({ p.up, ord++, p.b, BL::Input::Click });
            ev.push_back({ p.up, ord++, p.b, BL::Input::Release });
        }
    }
    std::sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) {
        return a.t != b.t ? a.t < b.t : a.ord < b.ord;
    });
}

// One event per 100 ms frame out of a 16-deep queue that discards its oldest.
static void framePace(const std::vector<Ev>& in, std::vector<Ev>& out)
{
    out.clear();
    Ev q[16];
    size_t head = 0, len = 0, next = 0;
    uint32_t frame = 100;
    const uint32_t lastFrame = HOUR + 1000;
    for (;;) {
        if (len == 0) {
            if (next >= in.size()) break;
            uint32_t f = (in[next].t + 99) / 100 * 100;
            if (f < 100) f = 100;
            if (f > frame) frame = f;
        }
        if (frame > lastFrame) break;
        while (next < in.size() && in[next].t <= frame) {
            if (len == 16) { head = (head + 1) % 16; --len; }
            q[(head + len) % 16] = in[next++];
            ++len;
        }
        Ev e = q[head];
        head = (head + 1) % 16;
        --len;
        e.t = frame;
        out.push_back(e);
        frame += 100;
    }
}

// ---------------------------------------------------------------- lock variants
static const uint8_t UP = 0, DOWN = 1, SELECT = 2, BACK = 3;   // ButtonLock::Button values
static const uint8_t kProbe[8] = { UP, SELECT, BACK, DOWN, UP, SELECT, BACK, DOWN };

static BL::Sequence prefix(unsigned n)
{
    BL::Sequence s;
    s.length = static_cast<uint8_t>(n);
    for (unsigned i = 0; i < n; ++i) s.steps[i] = static_cast<BL::Button>(kProbe[i]);
    return s;
}

struct RuleSet { const char* name; BL::UnlockTiming t; };

static std::vector<RuleSet> ruleSets()
{
    BL::UnlockTiming all;
    BL::UnlockTiming noCool = all;  noCool.rejectLimit = 0;
    BL::UnlockTiming noQuiet = all; noQuiet.quietMs = 0;
    BL::UnlockTiming noTiming = all; noTiming.maxGapMs = 0xFFFFFFFFu; noTiming.maxPressMs = 0xFFFFFFFFu;
    BL::UnlockTiming naive;
    naive.quietMs = 0; naive.stuckMs = 0; naive.maxGapMs = 0xFFFFFFFFu;
    naive.maxPressMs = 0xFFFFFFFFu; naive.rejectLimit = 0;
    BL::UnlockTiming gap15 = all;   gap15.maxGapMs = 1500;
    BL::UnlockTiming onlyCool = naive; onlyCool.rejectLimit = 3;   // order + cooldown, no timing/quiet
    return {
        { "all",       all },
        { "noCool",    noCool },
        { "noQuiet",   noQuiet },
        { "noTiming",  noTiming },
        { "naive",     naive },
        { "gap1.5s",   gap15 },
        { "orderCool", onlyCool },
    };
}

static constexpr unsigned kMinLen = 2, kMaxLen = 6, kLens = kMaxLen - kMinLen + 1;

struct Tally {
    // [rule][len index] unlocks, attempt starts
    std::vector<std::vector<uint64_t>> unlocks, starts;
    // [rule][k] probe (length 8) progress reached k
    std::vector<std::vector<uint64_t>> reach;
    std::vector<double> coolMs;      // [rule] time cooling down, probe detector
    uint64_t presses = 0, delivered = 0, kernel = 0, dropped = 0;
    void init(size_t nr)
    {
        unlocks.assign(nr, std::vector<uint64_t>(kLens, 0));
        starts.assign(nr, std::vector<uint64_t>(kLens, 0));
        reach.assign(nr, std::vector<uint64_t>(9, 0));
        coolMs.assign(nr, 0.0);
    }
    void add(const Tally& o)
    {
        for (size_t r = 0; r < unlocks.size(); ++r) {
            for (unsigned i = 0; i < kLens; ++i) { unlocks[r][i] += o.unlocks[r][i]; starts[r][i] += o.starts[r][i]; }
            for (unsigned k = 0; k < 9; ++k) reach[r][k] += o.reach[r][k];
            coolMs[r] += o.coolMs[r];
        }
        presses += o.presses; delivered += o.delivered; kernel += o.kernel; dropped += o.dropped;
    }
};

int main(int argc, char** argv)
{
    if (argc < 5) {
        std::fprintf(stderr, "usage: seq_crosscheck taps|bumps hours seed threads [nopace]\n");
        return 2;
    }
    const bool bumps = std::strcmp(argv[1], "bumps") == 0;
    const uint64_t hours = std::strtoull(argv[2], nullptr, 0);
    const uint64_t seed = std::strtoull(argv[3], nullptr, 0);
    const unsigned nth = static_cast<unsigned>(std::atoi(argv[4]));
    const bool pace = !(argc > 5 && std::strcmp(argv[5], "nopace") == 0);

    const auto rules = ruleSets();
    const size_t nr = rules.size();

    std::vector<Tally> tallies(nth);
    std::atomic<uint64_t> nextHour{ 0 };
    std::vector<std::thread> pool;
    for (unsigned th = 0; th < nth; ++th) {
        pool.emplace_back([&, th]() {
            Tally& T = tallies[th];
            T.init(nr);
            std::vector<Tap> taps;
            std::vector<Ev> kev, dev;
            std::vector<BL::UnlockDetector> dets;   // [rule*kLens + len] then probes
            for (size_t r = 0; r < nr; ++r)
                for (unsigned n = kMinLen; n <= kMaxLen; ++n) dets.emplace_back(prefix(n), rules[r].t);
            for (size_t r = 0; r < nr; ++r) dets.emplace_back(prefix(8), rules[r].t);
            const size_t probe0 = nr * kLens;

            for (;;) {
                const uint64_t h = nextHour.fetch_add(1);
                if (h >= hours) break;
                Xoshiro rng(seed ^ (h * 0xD1B54A32D192ED03ull) ^ (bumps ? 0xABCDEFull : 0x123457ull));
                taps.clear();
                if (bumps) genBumps(rng, taps); else genTaps(rng, taps);
                kernelEvents(taps, kev);
                T.presses += taps.size();
                T.kernel += kev.size();
                const std::vector<Ev>* feed = &kev;
                if (pace) { framePace(kev, dev); feed = &dev; }
                T.delivered += feed->size();

                for (auto& d : dets) d.reset(0);
                std::vector<uint32_t> lastT(nr, 0);

                for (const Ev& e : *feed) {
                    const auto btn = static_cast<BL::Button>(e.b);
                    for (size_t r = 0; r < nr; ++r) {
                        for (unsigned i = 0; i < kLens; ++i) {
                            BL::UnlockDetector& d = dets[r * kLens + i];
                            d.poll(e.t);
                            const BL::Outcome o = d.onInput(btn, e.in, e.t);
                            if (o == BL::Outcome::Unlocked) {
                                ++T.unlocks[r][i];
                                d.reset(e.t);
                            } else if (o == BL::Outcome::Progress && d.progress() == 1) {
                                ++T.starts[r][i];
                            }
                        }
                        BL::UnlockDetector& p = dets[probe0 + r];
                        // time spent cooling down since the previous event
                        if (p.coolingDown()) {
                            const uint32_t rem = p.cooldownRemaining(lastT[r]);
                            T.coolMs[r] += std::min<uint32_t>(rem, e.t - lastT[r]);
                        }
                        lastT[r] = e.t;
                        p.poll(e.t);
                        const BL::Outcome o = p.onInput(btn, e.in, e.t);
                        if (o == BL::Outcome::Progress || o == BL::Outcome::Unlocked) ++T.reach[r][p.progress()];
                        if (o == BL::Outcome::Unlocked) p.reset(e.t);
                    }
                }
                for (size_t r = 0; r < nr; ++r) {
                    BL::UnlockDetector& p = dets[probe0 + r];
                    if (p.coolingDown()) {
                        const uint32_t rem = p.cooldownRemaining(lastT[r]);
                        T.coolMs[r] += std::min<uint32_t>(rem, HOUR - lastT[r]);
                    }
                }
            }
        });
    }
    for (auto& t : pool) t.join();

    Tally all;
    all.init(nr);
    for (auto& t : tallies) all.add(t);

    std::printf("# model=%s hours=%llu seed=%llu pace=%d presses/h=%.1f kernelEv/h=%.1f deliveredEv/h=%.1f\n",
                bumps ? "bumps" : "taps", (unsigned long long)hours, (unsigned long long)seed, pace ? 1 : 0,
                double(all.presses) / hours, double(all.kernel) / hours, double(all.delivered) / hours);
    for (size_t r = 0; r < nr; ++r) {
        for (unsigned i = 0; i < kLens; ++i) {
            std::printf("U %s %u %llu %llu\n", rules[r].name, kMinLen + i,
                        (unsigned long long)all.unlocks[r][i], (unsigned long long)all.starts[r][i]);
        }
        std::printf("R %s", rules[r].name);
        for (unsigned k = 1; k <= 8; ++k) std::printf(" %llu", (unsigned long long)all.reach[r][k]);
        std::printf(" cool%%=%.1f\n", 100.0 * all.coolMs[r] / (double(hours) * HOUR));
    }
    return 0;
}
