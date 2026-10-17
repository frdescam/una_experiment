/**
 * @file    JacketModel.hpp
 * @brief   Monte Carlo model of a jacket cuff pressing the watch's buttons.
 *
 * Generates hours of accidental button activity under several hostile models,
 * turns it into the kernel's event stream, and replays it against
 *
 *   - ButtonLock's UnlockDetector, delivered one code per 10 Hz frame through a
 *     16-entry queue that drops the oldest, exactly as SDK::GuiCommandProcessor
 *     and SDK::LVGL::Port do, to count false unlocks; and
 *   - a model of the stock watch's menus and activity app, to count how often
 *     the same sleeve opens an activity (GPS on) or starts a recording.
 *
 * The stock model is an approximation: the kernel's home screen and launcher
 * are closed source. Its rules come from UNA's quick-start guide (SELECT
 * select / start, BACK back, UP utilities, DOWN glances), and the activity
 * app's rules from Examples/Apps/Running (START is the default item; SELECT on
 * it starts recording when GPS has a fix, or goes to a confirm screen where
 * SELECT starts anyway; the 30 s idle timeout closes the app only when START
 * is not selected). Menu timeouts and the time to a GPS fix are assumptions,
 * set in StockOptions.
 */

#ifndef TESTS_JACKET_MODEL_HPP
#define TESTS_JACKET_MODEL_HPP

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <random>
#include <vector>

#include "ButtonLock/UnlockDetector.hpp"

namespace Jacket
{

using ButtonLock::Button;
using ButtonLock::Input;

inline constexpr uint32_t kFrameMs        = 100;   // SDK::GUI::Config::kFrameRate = 10
inline constexpr size_t   kKeyQueue       = 16;    // GuiCommandProcessor::mButtonCodes
inline constexpr uint32_t kKernelClickMs  = 500;   // HWButtons: CLICK only under this

enum class Model : uint8_t {
    RandomTaps,      ///< taps on any button, every ~6 s
    OneSide,         ///< taps and holds on one side only (the cuff's side), every ~4 s
    Bumps,           ///< road bumps: bursts of 2-6 taps every ~15 s
    Squeeze,         ///< 2-4 buttons pressed together, held, every ~12 s
    PinnedPlusTaps,  ///< one button pinned for minutes, plus random taps
    Chaos,           ///< taps, holds and chords every ~1.5 s, any button
    // Extra models for the sequence study (not in kAllModels, so the original
    // report and tests are unchanged):
    TapsAndHolds,    ///< random taps every ~6 s, a quarter of them held 0.5-3 s (tests hold-to-finish)
    Rocker,          ///< bursts of taps rocking across two neighbouring buttons (a row or a column)
};

inline const char* modelName(Model m)
{
    switch (m) {
        case Model::RandomTaps:     return "Random taps (1 per 6 s, any button)";
        case Model::OneSide:        return "One-sided rubbing (1 per 4 s)";
        case Model::Bumps:          return "Road bumps (bursts of 2-6 taps)";
        case Model::Squeeze:        return "Squeezes (2-4 buttons at once)";
        case Model::PinnedPlusTaps: return "Pinned button + random taps";
        case Model::Chaos:          return "Chaos (taps/holds/chords, 1 per 1.5 s)";
        case Model::TapsAndHolds:   return "Taps and holds (1 per 6 s, 25% held 0.5-3 s)";
        case Model::Rocker:         return "Rocker (bursts across 2 neighbouring buttons)";
    }
    return "?";
}

inline constexpr std::array<Model, 6> kAllModels = {
    Model::RandomTaps, Model::OneSide, Model::Bumps,
    Model::Squeeze, Model::PinnedPlusTaps, Model::Chaos,
};

/// The sequence study's models: the six above plus two that probe what the
/// six cannot (noise that holds buttons on every side; a cuff rocking).
inline constexpr std::array<Model, 8> kStudyModels = {
    Model::RandomTaps, Model::OneSide, Model::Bumps, Model::Squeeze,
    Model::PinnedPlusTaps, Model::Chaos, Model::TapsAndHolds, Model::Rocker,
};

class Rng
{
public:
    explicit Rng(uint64_t seed) : mGen(seed) {}
    double   uniform(double a, double b) { return std::uniform_real_distribution<double>(a, b)(mGen); }
    uint32_t uniformInt(uint32_t a, uint32_t b) { return std::uniform_int_distribution<uint32_t>(a, b)(mGen); }
    uint32_t expMs(double meanMs) { return static_cast<uint32_t>(std::exponential_distribution<double>(1.0 / meanMs)(mGen)); }
    bool     chance(double p) { return uniform(0.0, 1.0) < p; }
    Button   anyButton() { return static_cast<Button>(uniformInt(0, 3)); }

private:
    std::mt19937_64 mGen;
};

/// One physical press: the button is down from @c down to @c up.
struct Press {
    Button   b;
    uint32_t down;
    uint32_t up;
};

struct Event {
    uint32_t t;
    Button   b;
    Input    in;
};

inline void addTap(std::vector<Press>& out, Button b, uint32_t t, uint32_t held)
{
    out.push_back(Press{ b, t, t + held });
}

/**
 * @brief Physical presses for one ride of @p durationMs under @p model.
 *
 * @p rate scales how often things happen: every interval *between* noise
 * events (taps, bursts, squeezes, pins) is divided by it, while what happens
 * inside one event (press lengths, the taps within a burst) is unchanged.
 * 1.0 is the original, deliberately hostile model; the sequence study lowers
 * it until the stock watch opens about one activity per hour.
 */
inline std::vector<Press> generate(Model model, Rng& rng, uint32_t durationMs, double rate = 1.0)
{
    std::vector<Press> out;
    uint32_t t = 0;
    auto gap = [&](double meanMs) { return rng.expMs(meanMs / rate); };

    switch (model) {
        case Model::RandomTaps:
            while ((t += gap(6000)) < durationMs) {
                addTap(out, rng.anyButton(), t, rng.uniformInt(40, 450));
            }
            break;

        case Model::OneSide: {
            const bool right = rng.chance(0.5);
            const Button a = right ? Button::Select : Button::Up;
            const Button b = right ? Button::Back : Button::Down;
            while ((t += gap(4000)) < durationMs) {
                const uint32_t held = rng.chance(0.2) ? rng.uniformInt(500, 3000) : rng.uniformInt(40, 450);
                addTap(out, rng.chance(0.5) ? a : b, t, held);
            }
        } break;

        case Model::Bumps:
            while ((t += gap(15000)) < durationMs) {
                const uint32_t n = rng.uniformInt(2, 6);
                Button b = rng.anyButton();
                uint32_t bt = t;
                for (uint32_t i = 0; i < n; ++i) {
                    const uint32_t held = rng.uniformInt(30, 350);
                    addTap(out, b, bt, held);
                    bt += held + rng.uniformInt(60, 500);
                    if (!rng.chance(0.6)) {
                        b = rng.anyButton();
                    }
                }
                t = bt;
            }
            break;

        case Model::Squeeze:
            while ((t += gap(12000)) < durationMs) {
                std::array<Button, 4> all = { Button::Up, Button::Down, Button::Select, Button::Back };
                for (size_t i = 3; i > 0; --i) {
                    std::swap(all[i], all[rng.uniformInt(0, static_cast<uint32_t>(i))]);
                }
                const uint32_t k = rng.uniformInt(2, 4);
                uint32_t last = t;
                for (uint32_t i = 0; i < k; ++i) {
                    const uint32_t d = t + rng.uniformInt(0, 120);
                    const uint32_t u = d + rng.uniformInt(150, 2500);
                    addTap(out, all[i], d, u - d);
                    last = std::max(last, u);
                }
                t = last;
            }
            break;

        case Model::PinnedPlusTaps: {
            // Long pins on one button...
            uint32_t p = 0;
            while (p < durationMs) {
                const Button b = rng.anyButton();
                const uint32_t held = rng.uniformInt(30000, 300000);
                addTap(out, b, p, held);
                p += held + static_cast<uint32_t>(rng.uniformInt(5000, 60000) / rate);
            }
            // ...and random taps on top (presses on a pinned button are dropped later).
            while ((t += gap(6000)) < durationMs) {
                addTap(out, rng.anyButton(), t, rng.uniformInt(40, 450));
            }
        } break;

        case Model::Chaos:
            while ((t += gap(1500)) < durationMs) {
                const double r = rng.uniform(0.0, 1.0);
                if (r < 0.6) {
                    addTap(out, rng.anyButton(), t, rng.uniformInt(30, 450));
                } else if (r < 0.8) {
                    addTap(out, rng.anyButton(), t, rng.uniformInt(500, 4000));
                } else {
                    const Button a = rng.anyButton();
                    Button b = rng.anyButton();
                    while (b == a) {
                        b = rng.anyButton();
                    }
                    addTap(out, a, t, rng.uniformInt(80, 1500));
                    addTap(out, b, t + rng.uniformInt(0, 150), rng.uniformInt(80, 1500));
                }
            }
            break;

        case Model::TapsAndHolds:
            while ((t += gap(6000)) < durationMs) {
                const uint32_t held = rng.chance(0.25) ? rng.uniformInt(500, 3000) : rng.uniformInt(40, 450);
                addTap(out, rng.anyButton(), t, held);
            }
            break;

        case Model::Rocker: {
            // The cuff's edge sits across two neighbouring buttons (fixed for
            // the ride): the top row, the bottom row, or one side. Every
            // ~15 s it rocks: 2-6 taps, alternating with p = 0.8.
            static constexpr Button kPairs[4][2] = {
                { Button::Up, Button::Select }, { Button::Down, Button::Back },
                { Button::Up, Button::Down }, { Button::Select, Button::Back },
            };
            const auto& pair = kPairs[rng.uniformInt(0, 3)];
            while ((t += gap(15000)) < durationMs) {
                const uint32_t n = rng.uniformInt(2, 6);
                unsigned k = rng.uniformInt(0, 1);
                uint32_t bt = t;
                for (uint32_t i = 0; i < n; ++i) {
                    const uint32_t held = rng.uniformInt(30, 350);
                    addTap(out, pair[k], bt, held);
                    bt += held + rng.uniformInt(60, 500);
                    if (rng.chance(0.8)) {
                        k ^= 1u;
                    }
                }
                t = bt;
            }
        } break;
    }
    return out;
}

/**
 * @brief The kernel's view of the presses: PRESS when a button goes down, and
 *        on release a CLICK (if held under 500 ms) then a RELEASE.
 *
 * A press that starts while its button is still down is physically the same
 * press and is dropped. Releases past the end of the ride are kept: a pinned
 * button simply has no release inside the window.
 */
inline std::vector<Event> toKernelEvents(std::vector<Press> presses, uint32_t durationMs)
{
    std::sort(presses.begin(), presses.end(), [](const Press& a, const Press& b) { return a.down < b.down; });

    std::array<uint32_t, 4> busyUntil = {};
    std::vector<Event> ev;
    for (const Press& p : presses) {
        const auto i = static_cast<size_t>(p.b);
        if (p.down < busyUntil[i] || p.down >= durationMs) {
            continue;
        }
        busyUntil[i] = p.up + 1;
        ev.push_back(Event{ p.down, p.b, Input::Press });
        if (p.up < durationMs) {
            if (p.up - p.down < kKernelClickMs) {
                ev.push_back(Event{ p.up, p.b, Input::Click });
            }
            ev.push_back(Event{ p.up, p.b, Input::Release });
        }
    }
    std::stable_sort(ev.begin(), ev.end(), [](const Event& a, const Event& b) { return a.t < b.t; });
    return ev;
}

/**
 * @brief What an LVGL screen actually receives: one code per frame, from a
 *        16-entry queue that drops its oldest entry when full. Each delivered
 *        event carries the frame time at which the screen sees it.
 */
inline std::vector<Event> paceToFrames(const std::vector<Event>& ev, uint32_t durationMs)
{
    std::vector<Event> out;
    std::deque<Event> queue;
    size_t next = 0;
    for (uint32_t frame = kFrameMs; frame <= durationMs + 10 * kFrameMs; frame += kFrameMs) {
        while (next < ev.size() && ev[next].t <= frame) {
            if (queue.size() == kKeyQueue) {
                queue.pop_front();
            }
            queue.push_back(ev[next++]);
        }
        if (!queue.empty()) {
            Event e = queue.front();
            queue.pop_front();
            e.t = frame;
            out.push_back(e);
        }
    }
    return out;
}

/// False unlocks of a detector fed @p delivered (already frame-paced). After
/// each one the detector is reset, as if the lock went straight back up.
inline uint32_t countFalseUnlocks(ButtonLock::UnlockDetector& d, const std::vector<Event>& delivered,
                                  uint32_t durationMs)
{
    d.reset(0);
    uint32_t unlocks = 0;
    size_t   next    = 0;
    for (uint32_t frame = kFrameMs; frame <= durationMs + 10 * kFrameMs; frame += kFrameMs) {
        while (next < delivered.size() && delivered[next].t <= frame) {
            const Event& e = delivered[next++];
            if (d.onInput(e.b, e.in, e.t) == ButtonLock::Outcome::Unlocked) {
                ++unlocks;
                d.reset(e.t);
            }
        }
        d.poll(frame);
    }
    return unlocks;
}

// --- The stock watch -----------------------------------------------------------

struct StockOptions {
    uint32_t menuIdleMs        = 20000;  ///< assumed: a launcher menu falls back home after this
    uint32_t appIdleMs         = 30000;  ///< Running: App::Config::kScreenTimeoutSteps
    uint32_t gpsFixAfterMs     = 60000;  ///< assumed: time to a fix under a sleeve
    uint32_t preActivityBudgetMs = 0;    ///< 0: stock. Otherwise the patched apps' budget (the patch: 5 min)
    uint32_t holdToStartMs     = 0;      ///< 0: stock (a click starts). Otherwise SELECT must be held this long (the patch: 1500)
    bool     holdAlone         = false;  ///< the hold must be SELECT alone: another button cancels or prevents it
};

struct StockResult {
    uint32_t activityOpens     = 0;   ///< activity app opened (GPS starts searching)
    uint32_t recordingsStarted = 0;
    uint64_t gpsOnMs           = 0;
};

inline StockResult runStock(const std::vector<Event>& ev, uint32_t durationMs, const StockOptions& o)
{
    enum class S { Home, ActivityMenu, UtilityMenu, UtilityApp, Glances, ActPre, ActConfirm, ActSub, Recording };

    StockResult r;
    S        s        = S::Home;
    int      sel      = 0;          // pre-activity menu: 0 START, 1 INTERVALS, 2 SETTINGS
    uint32_t lastIn   = 0;          // last click, or when the current screen appeared
    uint32_t gpsSince = 0;
    uint32_t openedAt = 0;
    bool     gpsOn    = false;

    auto gps = [&](bool on, uint32_t t) {
        if (on && !gpsOn) {
            gpsSince = t;
        } else if (!on && gpsOn) {
            r.gpsOnMs += t - gpsSince;
        }
        gpsOn = on;
    };
    auto enter = [&](S next, uint32_t t) {
        s      = next;
        lastIn = t;
        const bool on = (s == S::ActPre || s == S::ActConfirm || s == S::ActSub || s == S::Recording);
        gps(on, t);
    };
    auto hasFix = [&](uint32_t t) { return gpsOn && t - gpsSince >= o.gpsFixAfterMs; };

    // Apply every timeout that falls due before time t.
    auto settle = [&](uint32_t t) {
        for (;;) {
            const bool pre = (s == S::ActPre || s == S::ActConfirm || s == S::ActSub);
            if (pre && o.preActivityBudgetMs > 0 && t - openedAt >= o.preActivityBudgetMs) {
                enter(S::Home, openedAt + o.preActivityBudgetMs);
                continue;
            }
            switch (s) {
                case S::ActivityMenu:
                case S::UtilityMenu:
                case S::Glances:
                    if (t - lastIn >= o.menuIdleMs) { enter(S::Home, lastIn + o.menuIdleMs); continue; }
                    return;
                case S::ActConfirm:
                case S::ActSub:
                    if (t - lastIn >= o.appIdleMs) { enter(S::ActPre, lastIn + o.appIdleMs); continue; }
                    return;
                case S::ActPre:
                    if (sel != 0 && t - lastIn >= o.appIdleMs) { enter(S::Home, lastIn + o.appIdleMs); continue; }
                    return;
                default:
                    return;
            }
        }
    };

    // Hold-to-start: the countdown runs from SELECT's PRESS on a start screen
    // and fires when it completes with SELECT still down, as the stock apps'
    // existing hold-to-finish screen does.
    std::array<bool, 4> othersDown = {};   // buttons other than SELECT currently down
    bool     r1Down   = false;   // SELECT went down on a start screen and is still down
    uint32_t r1DownAt = 0;
    auto isStartScreen = [&]() { return (s == S::ActPre && sel == 0) || s == S::ActConfirm; };
    auto holdFires = [&](uint32_t upAt) {
        if (o.holdToStartMs == 0 || !r1Down || !isStartScreen() || upAt - r1DownAt < o.holdToStartMs) {
            return;
        }
        // patches/activity-apps.patch: a completed hold starts the activity,
        // fix or not (the hold is the confirmation).
        ++r.recordingsStarted;
        enter(S::Recording, r1DownAt + o.holdToStartMs);
    };

    for (const Event& e : ev) {
        if (e.t >= durationMs) {
            break;
        }
        settle(e.t);
        if (e.in == Input::Press) {
            othersDown[static_cast<size_t>(e.b)] = (e.b != Button::Select);
        } else if (e.in == Input::Release) {
            othersDown[static_cast<size_t>(e.b)] = false;
        }
        const bool anotherDown = othersDown[0] || othersDown[1] || othersDown[3];
        if (e.b == Button::Select && e.in == Input::Press) {
            r1Down   = isStartScreen() && !(o.holdAlone && anotherDown);
            r1DownAt = e.t;
        } else if (e.b == Button::Select && e.in == Input::Release) {
            holdFires(e.t);
            r1Down = false;
        } else if (o.holdAlone && r1Down && e.in == Input::Press) {
            r1Down = false;   // another button during the hold cancels it
        }
        if (e.in != Input::Click) {
            continue;   // the launcher and the activity screens act on clicks
        }
        const uint32_t t = e.t;
        lastIn = t;
        switch (s) {
            case S::Home:
                if (e.b == Button::Select) { enter(S::ActivityMenu, t); }
                else if (e.b == Button::Up) { enter(S::UtilityMenu, t); }
                else if (e.b == Button::Down) { enter(S::Glances, t); }
                break;
            case S::ActivityMenu:
                if (e.b == Button::Select) {
                    sel      = 0;
                    openedAt = t;
                    ++r.activityOpens;
                    enter(S::ActPre, t);
                } else if (e.b == Button::Back) {
                    enter(S::Home, t);
                }
                break;
            case S::UtilityMenu:
                if (e.b == Button::Select) { enter(S::UtilityApp, t); }
                else if (e.b == Button::Back) { enter(S::Home, t); }
                break;
            case S::UtilityApp:
                if (e.b == Button::Back) { enter(S::UtilityMenu, t); }
                break;
            case S::Glances:
                if (e.b == Button::Back) { enter(S::Home, t); }
                break;
            case S::ActPre:
                if (e.b == Button::Up) { sel = (sel + 2) % 3; }
                else if (e.b == Button::Down) { sel = (sel + 1) % 3; }
                else if (e.b == Button::Back) { enter(S::Home, t); }
                else if (e.b == Button::Select) {
                    if (sel != 0) { enter(S::ActSub, t); }
                    else if (o.holdToStartMs > 0) { /* a click is not enough */ }
                    else if (hasFix(t)) { ++r.recordingsStarted; enter(S::Recording, t); }
                    else { enter(S::ActConfirm, t); }
                }
                break;
            case S::ActConfirm:
                if (e.b == Button::Select && o.holdToStartMs == 0) { ++r.recordingsStarted; enter(S::Recording, t); }
                else if (e.b == Button::Back) { enter(S::ActPre, t); }
                break;
            case S::ActSub:
                if (e.b == Button::Back) { enter(S::ActPre, t); }
                break;
            case S::Recording:
                // Ending a recording needs SELECT held for the hold-to-confirm
                // countdown, which a click never is: the recording runs on.
                break;
        }
    }
    settle(durationMs);
    holdFires(durationMs);   // a button still pinned at the end of the ride
    settle(durationMs);
    gps(false, durationMs);
    return r;
}

// --- A lock that stays down after a false unlock ------------------------------

struct LockedRideOptions {
    uint32_t relockMs   = 2u * 60u * 1000u;  ///< LockPolicy::relockMs (the lock comes back after this)
    uint32_t handBackMs = 700;               ///< LockScreen kUnlockedShowMs: the open padlock, then the watch face
};

struct LockedRideResult {
    uint32_t    falseUnlocks = 0;
    uint64_t    exposedMs    = 0;   ///< time the watch face had the buttons
    uint64_t    deafMs       = 0;   ///< time the lock spent cooling down (a person would be ignored)
    StockResult stock;              ///< what the same noise did to the stock watch while the lock was down
    StockResult patched;            ///< the same windows with the patched activity apps
};

/**
 * @brief Replays one ride against a lock that, after a false unlock, stays
 *        down for relockMs while the noise goes on reaching the watch face.
 *
 * The lock sees the frame-paced stream (@p delivered); the watch face sees the
 * kernel stream (@p kernel) of each exposed window, starting at Home, fed to
 * runStock() with @p stock and with @p patched. The detector is reset when the
 * lock comes back. Event-driven: poll() runs at each delivered event rather
 * than every frame, which decides exactly the same (a test checks it against
 * countFalseUnlocks with relockMs = handBackMs = 0).
 */
inline LockedRideResult runLockedRide(ButtonLock::UnlockDetector& d, const std::vector<Event>& delivered,
                                      const std::vector<Event>& kernel, uint32_t durationMs,
                                      const LockedRideOptions& lo, const StockOptions& stock,
                                      const StockOptions& patched)
{
    LockedRideResult r;
    d.reset(0);

    bool     cooling     = false;
    uint32_t coolStart   = 0;
    uint32_t coolLastAct = 0;
    const uint32_t cooldownMs = d.timing().cooldownMs;
    auto endCooling = [&](uint32_t cap) {
        const uint32_t end = std::min(coolLastAct + cooldownMs, cap);
        r.deafMs += end > coolStart ? end - coolStart : 0;
        cooling = false;
    };

    std::vector<Event> window;
    size_t next = 0;
    while (next < delivered.size()) {
        const Event& e = delivered[next++];
        const ButtonLock::Outcome o = d.onInput(e.b, e.in, e.t);
        d.poll(e.t);

        const bool nowCooling = d.coolingDown();
        if (!cooling && nowCooling) {
            cooling   = true;
            coolStart = coolLastAct = e.t;
        } else if (cooling && nowCooling) {
            coolLastAct = e.t;
        } else if (cooling && !nowCooling) {
            endCooling(e.t);
        }

        if (o != ButtonLock::Outcome::Unlocked) {
            continue;
        }
        ++r.falseUnlocks;

        const uint32_t from     = e.t + lo.handBackMs;
        const uint32_t relockAt = from + lo.relockMs;
        if (from < durationMs) {
            const uint32_t to = std::min(relockAt, durationMs);
            auto first = std::lower_bound(kernel.begin(), kernel.end(), from,
                                          [](const Event& k, uint32_t v) { return k.t < v; });
            window.clear();
            for (auto it = first; it != kernel.end() && it->t < to; ++it) {
                Event w = *it;
                w.t -= from;
                window.push_back(w);
            }
            const StockResult s = runStock(window, to - from, stock);
            r.stock.activityOpens     += s.activityOpens;
            r.stock.recordingsStarted += s.recordingsStarted;
            r.stock.gpsOnMs           += s.gpsOnMs;
            const StockResult p = runStock(window, to - from, patched);
            r.patched.activityOpens     += p.activityOpens;
            r.patched.recordingsStarted += p.recordingsStarted;
            r.patched.gpsOnMs           += p.gpsOnMs;
            r.exposedMs += to - from;
        }
        while (next < delivered.size() && delivered[next].t < relockAt) {
            ++next;
        }
        d.reset(relockAt);
    }
    if (cooling) {
        endCooling(durationMs);
    }
    return r;
}

} // namespace Jacket

#endif // TESTS_JACKET_MODEL_HPP
