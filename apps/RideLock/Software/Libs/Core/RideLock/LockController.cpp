/**
 ******************************************************************************
 * @file    LockController.cpp
 * @brief   Service-side lock policy (see LockController.hpp).
 ******************************************************************************
 */

#include "RideLock/LockController.hpp"

namespace RideLock
{

namespace
{

/// Requests that may fail in a row before a service with no relock gives up.
constexpr uint8_t kMaxFailedRequests = 3;

uint32_t remaining(uint32_t nowMs, uint32_t deadline)
{
    const int32_t left = static_cast<int32_t>(deadline - nowMs);
    return left > 0 ? static_cast<uint32_t>(left) : 0u;
}

uint32_t maxOf(uint32_t a, uint32_t b)
{
    return a > b ? a : b;
}

} // namespace

LockController::LockController(const LockPolicy& policy)
    : mPolicy(policy)
{
}

bool LockController::reached(uint32_t nowMs, uint32_t deadline)
{
    return static_cast<int32_t>(nowMs - deadline) >= 0;
}

void LockController::start(uint32_t nowMs)
{
    mState          = LockState::Starting;
    mGuiLoaded      = false;
    mStartedAt      = nowMs;
    mRelockArmed    = false;
    mFailedRequests = 0;
}

void LockController::onGuiRun(uint32_t /*nowMs*/)
{
    // Whether we asked for it or the user opened the app, a loaded GUI is
    // the lock screen.
    mGuiLoaded      = true;
    mGuiHidden      = false;
    mState          = LockState::Locked;
    mRelockArmed    = false;
    mFailedRequests = 0;
}

void LockController::onGuiStop(uint32_t nowMs)
{
    mGuiLoaded = false;
    mGuiHidden = false;

    switch (mState) {
        case LockState::Locked:
        case LockState::Starting:
            // Gone without an unlock. Bring it back, but no faster than the
            // retry floor, so a GUI that dies on load costs one attempt a
            // minute rather than a loop.
            if (mPolicy.relockMs > 0) {
                enterUnlocked(nowMs, maxOf(mPolicy.relockMs, mPolicy.retryFloorMs));
            } else {
                enterUnlocked(nowMs, 0);
            }
            break;

        case LockState::Requesting:
        case LockState::Unlocked:
            break;
    }
}

void LockController::onUnlocked(uint32_t nowMs)
{
    mGuiHidden = false;
    enterUnlocked(nowMs, mPolicy.relockMs);
}

void LockController::onGuiHidden(uint32_t nowMs)
{
    if (mGuiLoaded && !mGuiHidden) {
        mGuiHidden   = true;
        mHiddenSince = nowMs;
    }
}

void LockController::onGuiShown(uint32_t /*nowMs*/)
{
    mGuiHidden = false;
}

uint32_t LockController::reassertDelay() const
{
    // As long as the relock period, and never sooner than the retry floor:
    // an alarm or a notification in front is usually gone well before.
    return mPolicy.relockMs > 0 ? maxOf(mPolicy.relockMs, mPolicy.retryFloorMs) : 0u;
}

void LockController::enterUnlocked(uint32_t nowMs, uint32_t delayMs)
{
    mState       = LockState::Unlocked;
    mRelockArmed = delayMs > 0;
    mRelockAt    = nowMs + delayMs;
}

LockAction LockController::poll(uint32_t nowMs)
{
    switch (mState) {
        case LockState::Starting:
            if (!reached(nowMs, mStartedAt + mPolicy.startupGraceMs)) {
                return LockAction::None;
            }
            // No GUI came with this start: the watch booted (APP_AUTOSTART).
            if (mPolicy.lockAtBoot) {
                mState     = LockState::Requesting;
                mRequestAt = nowMs;
                return LockAction::ShowLock;
            }
            enterUnlocked(nowMs, mPolicy.relockMs);
            return mRelockArmed ? LockAction::None : LockAction::Exit;

        case LockState::Requesting:
            if (!reached(nowMs, mRequestAt + mPolicy.requestTimeoutMs)) {
                return LockAction::None;
            }
            // The kernel did not bring the GUI up (out of memory, say).
            if (mPolicy.relockMs > 0) {
                enterUnlocked(nowMs, maxOf(mPolicy.relockMs, mPolicy.retryFloorMs));
            } else if (++mFailedRequests < kMaxFailedRequests) {
                enterUnlocked(nowMs, mPolicy.retryFloorMs);
            } else {
                enterUnlocked(nowMs, 0);
            }
            return mRelockArmed ? LockAction::None : LockAction::Exit;

        case LockState::Locked:
            if (mGuiHidden && reassertDelay() > 0 &&
                reached(nowMs, mHiddenSince + reassertDelay())) {
                // Still loaded but out of sight: bring it back to the front.
                // Asked once per period, for as long as it stays hidden.
                mHiddenSince = nowMs;
                return LockAction::ShowLock;
            }
            return LockAction::None;

        case LockState::Unlocked:
            if (mGuiLoaded) {
                // The GUI closes itself after reporting the unlock; wait for
                // it to go, since returning now would take it down mid-exit.
                return LockAction::None;
            }
            if (!mRelockArmed) {
                return LockAction::Exit;
            }
            if (reached(nowMs, mRelockAt)) {
                mState     = LockState::Requesting;
                mRequestAt = nowMs;
                return LockAction::ShowLock;
            }
            return LockAction::None;
    }
    return LockAction::None;
}

uint32_t LockController::msUntilNextDeadline(uint32_t nowMs) const
{
    switch (mState) {
        case LockState::Starting:
            return remaining(nowMs, mStartedAt + mPolicy.startupGraceMs);

        case LockState::Requesting:
            return remaining(nowMs, mRequestAt + mPolicy.requestTimeoutMs);

        case LockState::Locked:
            if (mGuiHidden && reassertDelay() > 0) {
                return remaining(nowMs, mHiddenSince + reassertDelay());
            }
            return kNoDeadline;

        case LockState::Unlocked:
            if (mGuiLoaded) {
                return kNoDeadline;   // COMMAND_APP_NOTIF_GUI_STOP will wake us
            }
            return mRelockArmed ? remaining(nowMs, mRelockAt) : 0u;
    }
    return kNoDeadline;
}

} // namespace RideLock
