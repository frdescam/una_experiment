/**
 ******************************************************************************
 * @file    LockController.hpp
 * @brief   The service's lock policy: when to put the lock screen up, and
 *          when the service has nothing left to do.
 *
 * The kernel gives buttons only to the GUI on screen, so the lock is simply
 * this app's GUI being in front: while it is, no press reaches the launcher or
 * an activity. The service decides when it should be in front:
 *
 *   boot      The app is built with APP_AUTOSTART, so the service starts with
 *             the watch and no GUI. If no GUI appears within the startup
 *             grace, this was a boot (or the end of USB mode), and with
 *             lockAtBoot the lock goes up at once. A reboot caused by a long
 *             press therefore cannot leave the watch unlocked.
 *   opened    The user opened the app from the launcher: the GUI appears
 *             inside the grace, and the watch is locked.
 *   unlocked  The GUI reports the sequence and closes itself. With relockMs
 *             set, the service stays resident and puts the lock back up when
 *             that time has passed; without it, the service exits.
 *   gui lost  A GUI that disappears without unlocking (killed, crashed) is
 *             treated as an unlock, so a failing GUI is relaunched at most
 *             once per relock period, never in a tight loop.
 *   hidden    The lock screen is still loaded but another screen is in front
 *             of it (an alarm, a notification, or any kernel gesture that
 *             switches away, which a sleeve might find). If it stays hidden
 *             for the relock period, it is brought back to the front.
 *
 * Pure logic: the service feeds it events and the millisecond clock, and
 * carries out the Action that poll() returns.
 ******************************************************************************
 */

#ifndef RIDELOCK_LOCK_CONTROLLER_HPP
#define RIDELOCK_LOCK_CONTROLLER_HPP

#include <cstdint>

namespace RideLock
{

struct LockPolicy {
    uint32_t relockMs         = 2u * 60u * 1000u;  ///< 0: never relock on its own
    bool     lockAtBoot       = true;
    uint32_t startupGraceMs   = 5000;   ///< how long a launch takes to bring its GUI
    uint32_t requestTimeoutMs = 10000;  ///< a lock request that shows nothing is retried after this
    uint32_t retryFloorMs     = 60000;  ///< never retry a failed request sooner than this
};

enum class LockState : uint8_t {
    Starting,     ///< waiting to learn whether a GUI comes with this launch
    Requesting,   ///< asked the kernel for the GUI, waiting for it
    Locked,       ///< the lock screen is loaded
    Unlocked,     ///< the user unlocked; relock pending (or exit)
};

enum class LockAction : uint8_t {
    None,
    ShowLock,     ///< send RequestAppRunGui
    Exit,         ///< return from the service's run(); no GUI is loaded
};

class LockController
{
public:
    static constexpr uint32_t kNoDeadline = 0xFFFFFFFFu;

    explicit LockController(const LockPolicy& policy = LockPolicy{});

    void setPolicy(const LockPolicy& policy) { mPolicy = policy; }
    const LockPolicy& policy() const { return mPolicy; }

    /// The service has started.
    void start(uint32_t nowMs);

    /// COMMAND_APP_NOTIF_GUI_RUN: the lock screen is loaded.
    void onGuiRun(uint32_t nowMs);

    /// COMMAND_APP_NOTIF_GUI_STOP: the GUI process has gone.
    void onGuiStop(uint32_t nowMs);

    /// The GUI reported a completed unlock sequence.
    void onUnlocked(uint32_t nowMs);

    /// The lock screen went behind another screen (COMMAND_APP_GUI_SUSPEND).
    void onGuiHidden(uint32_t nowMs);

    /// The lock screen is in front again (COMMAND_APP_GUI_RESUME).
    void onGuiShown(uint32_t nowMs);

    /**
     * @brief What the service must do now. Call after every event and
     *        whenever the wait from msUntilNextDeadline() expires.
     *
     * ShowLock is returned once per request; Exit is returned only while no
     * GUI is loaded, because a service that returns takes its GUI with it.
     */
    LockAction poll(uint32_t nowMs);

    /// Milliseconds until poll() may have something to do, or kNoDeadline.
    uint32_t msUntilNextDeadline(uint32_t nowMs) const;

    LockState state() const { return mState; }
    bool      guiLoaded() const { return mGuiLoaded; }
    bool      guiHidden() const { return mGuiHidden; }
    bool      isLocked() const { return mState == LockState::Locked; }

    /// When the lock will go back up (valid in Unlocked with relock enabled).
    uint32_t  relockAt() const { return mRelockAt; }

private:
    void enterUnlocked(uint32_t nowMs, uint32_t delayMs);
    uint32_t reassertDelay() const;
    static bool reached(uint32_t nowMs, uint32_t deadline);

    LockPolicy mPolicy;
    LockState  mState      = LockState::Starting;
    bool       mGuiLoaded  = false;
    uint32_t   mStartedAt  = 0;
    uint32_t   mRequestAt  = 0;
    uint32_t   mRelockAt   = 0;
    bool       mRelockArmed = false;
    uint8_t    mFailedRequests = 0;
    bool       mGuiHidden   = false;
    uint32_t   mHiddenSince = 0;
};

} // namespace RideLock

#endif // RIDELOCK_LOCK_CONTROLLER_HPP
