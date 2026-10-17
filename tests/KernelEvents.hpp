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

#include "ButtonLock/UnlockDetector.hpp"

namespace TestSupport
{

inline constexpr uint32_t kKernelClickMaxMs = 500;

/// The decisive outcome of a group of events: the last one that was not None.
inline ButtonLock::Outcome merge(ButtonLock::Outcome acc, ButtonLock::Outcome next)
{
    return next == ButtonLock::Outcome::None ? acc : next;
}

inline ButtonLock::Outcome down(ButtonLock::UnlockDetector& d, ButtonLock::Button b, uint32_t t)
{
    return d.onInput(b, ButtonLock::Input::Press, t);
}

inline ButtonLock::Outcome up(ButtonLock::UnlockDetector& d, ButtonLock::Button b, uint32_t t, uint32_t heldMs)
{
    ButtonLock::Outcome o = ButtonLock::Outcome::None;
    if (heldMs < kKernelClickMaxMs) {
        o = merge(o, d.onInput(b, ButtonLock::Input::Click, t));
    }
    return merge(o, d.onInput(b, ButtonLock::Input::Release, t));
}

/// One press of @p heldMs starting at @p t, kernel event order.
inline ButtonLock::Outcome tap(ButtonLock::UnlockDetector& d, ButtonLock::Button b, uint32_t t, uint32_t heldMs = 120)
{
    ButtonLock::Outcome o = down(d, b, t);
    return merge(o, up(d, b, t + heldMs, heldMs));
}

} // namespace TestSupport

#endif // TESTS_KERNEL_EVENTS_HPP
