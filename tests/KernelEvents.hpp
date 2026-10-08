/**
 * @file    KernelEvents.hpp
 * @brief   Test helper: feeds an UnlockDetector the way the UNA kernel does.
 *
 * The kernel reports a PRESS when a button goes down and, when it comes up,
 * a CLICK (only if it was down for less than 500 ms) followed by a RELEASE
 * (Libs/Source/Simulator/LVGL/LvglHost.cpp mirrors HWButtons.cpp).
 */

#ifndef TESTS_KERNEL_EVENTS_HPP
#define TESTS_KERNEL_EVENTS_HPP

#include <cstdint>

#include "RideLock/UnlockDetector.hpp"

namespace TestSupport
{

inline constexpr uint32_t kKernelClickMaxMs = 500;

/// The decisive outcome of a group of events: the last one that was not None.
inline RideLock::Outcome merge(RideLock::Outcome acc, RideLock::Outcome next)
{
    return next == RideLock::Outcome::None ? acc : next;
}

inline RideLock::Outcome down(RideLock::UnlockDetector& d, RideLock::Button b, uint32_t t)
{
    return d.onInput(b, RideLock::Input::Press, t);
}

inline RideLock::Outcome up(RideLock::UnlockDetector& d, RideLock::Button b, uint32_t t, uint32_t heldMs)
{
    RideLock::Outcome o = RideLock::Outcome::None;
    if (heldMs < kKernelClickMaxMs) {
        o = merge(o, d.onInput(b, RideLock::Input::Click, t));
    }
    return merge(o, d.onInput(b, RideLock::Input::Release, t));
}

/// One press of @p heldMs starting at @p t, kernel event order.
inline RideLock::Outcome tap(RideLock::UnlockDetector& d, RideLock::Button b, uint32_t t, uint32_t heldMs = 120)
{
    RideLock::Outcome o = down(d, b, t);
    return merge(o, up(d, b, t + heldMs, heldMs));
}

} // namespace TestSupport

#endif // TESTS_KERNEL_EVENTS_HPP
