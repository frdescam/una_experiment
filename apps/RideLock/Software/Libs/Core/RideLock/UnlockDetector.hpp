/**
 ******************************************************************************
 * @file    UnlockDetector.hpp
 * @brief   Recognises the deliberate unlock sequence in a stream of button
 *          events, and nothing a jacket cuff is likely to produce.
 *
 * The input is the kernel's raw button stream: a PRESS when a button goes
 * down, and on the way up a CLICK (only when it was held under 500 ms) then a
 * RELEASE. A step of the sequence is one button pressed and released on its
 * own. The rules that keep a sleeve out:
 *
 *   one at a time  A press while another button is freshly down is a chord
 *                  and ends the attempt. Squeezing the case does this.
 *   quiet first    The first step only counts after quietMs with no button
 *                  activity at all. A cuff that is rubbing produces bursts;
 *                  a person looks at the watch, then presses.
 *   brisk          Each press is released within maxPressMs and the next step
 *                  starts within maxGapMs of the last one ending.
 *   exact order    Any other button ends the attempt, and the attempt does
 *                  not restart on that same press (it is not quiet).
 *   stuck buttons  A button held longer than stuckMs (a cuff pinning it) is
 *                  ignored by the chord rule, so a pinned button cannot lock
 *                  the wearer out.
 *   cool down      rejectLimit rejected attempts within rejectWindowMs is
 *                  what a sleeve does and a person does not: unlocking then
 *                  pauses until no button has been touched for cooldownMs.
 *                  Noise that keeps going keeps the lock deaf; a person who
 *                  fumbles waits a few seconds.
 *
 * Time is any monotonic millisecond clock; differences are taken modulo 2^32
 * so the 49-day wrap of a 32-bit tick is harmless.
 ******************************************************************************
 */

#ifndef RIDELOCK_UNLOCK_DETECTOR_HPP
#define RIDELOCK_UNLOCK_DETECTOR_HPP

#include <cstdint>

#include "RideLock/Sequence.hpp"

namespace RideLock
{

struct UnlockTiming {
    uint32_t maxPressMs  = 1200;   ///< longer is a hold, not a step (gloves are slow)
    uint32_t maxGapMs    = 2500;   ///< from one step's release to the next press
    uint32_t quietMs     = 600;    ///< silence required before the first step
    uint32_t stuckMs     = 4000;   ///< a button down this long is treated as pinned
    uint32_t clickEchoMs = 400;    ///< a CLICK this soon after its RELEASE is the same press

    uint8_t  rejectLimit    = 3;      ///< rejections that start a cool-down; 0 disables it
    uint32_t rejectWindowMs = 30000;  ///< ...when they fall within this window
    uint32_t cooldownMs     = 15000;  ///< silence that ends a cool-down
};

enum class Input : uint8_t {
    Press,
    Release,
    Click,
};

enum class Outcome : uint8_t {
    None,       ///< nothing decided yet (a press, or input that does not count)
    Progress,   ///< a correct step: progress() went up by one
    Unlocked,   ///< the last step: the sequence is complete
    Wrong,      ///< a step on the wrong button; progress is back to 0
    TooLong,    ///< a button held past maxPressMs; progress is back to 0
    Chord,      ///< two buttons down at once; progress is back to 0
    Timeout,    ///< too long between steps; progress is back to 0 (from poll())
    NotQuiet,   ///< a first step started too soon after other activity
    CoolingDown,///< input while unlocking is paused (or the pause just began)
    CooledDown, ///< from poll(): the pause is over, unlocking is possible again
};

/// True for the outcomes that end an attempt or refuse to start one.
bool isRejection(Outcome o);

class UnlockDetector
{
public:
    UnlockDetector();
    UnlockDetector(const Sequence& seq, const UnlockTiming& timing);

    /// Replace the sequence and timing; also does reset(nowMs).
    void configure(const Sequence& seq, const UnlockTiming& timing, uint32_t nowMs);

    /**
     * @brief Forget everything: held buttons, progress, the unlocked latch.
     *
     * Call when the screen comes back (releases may have been missed while it
     * was away). The quiet period counts from @p nowMs.
     */
    void reset(uint32_t nowMs);

    /// Feed one button event.
    Outcome onInput(Button b, Input in, uint32_t nowMs);

    /// Call regularly (every frame is fine) to time out an abandoned attempt.
    Outcome poll(uint32_t nowMs);

    uint8_t  progress() const { return mProgress; }
    uint8_t  length() const { return mSeq.length; }
    Button   expected() const { return mSeq.steps[mProgress < mSeq.length ? mProgress : 0]; }
    bool     unlocked() const { return mUnlocked; }
    bool     coolingDown() const { return mCoolingDown; }

    /// While cooling down: milliseconds of silence still needed.
    uint32_t cooldownRemaining(uint32_t nowMs) const;

    const Sequence&     sequence() const { return mSeq; }
    const UnlockTiming& timing() const { return mTiming; }

private:
    Outcome press(unsigned i, uint32_t now);
    Outcome release(unsigned i, uint32_t now);
    Outcome click(unsigned i, uint32_t now);
    Outcome reject(Outcome why, uint32_t now);
    bool    endCooldownIfQuiet(uint32_t now);
    void    restart();
    void    noteActivity(uint32_t now);

    static constexpr size_t kMaxRejectLimit = 8;

    Sequence     mSeq;
    UnlockTiming mTiming;

    bool     mDown[kButtonCount]          = {};
    uint32_t mDownAt[kButtonCount]        = {};
    bool     mReleasedValid[kButtonCount] = {};
    uint32_t mReleasedAt[kButtonCount]    = {};

    bool     mHasCandidate = false;   ///< the button now down may become a step
    unsigned mCandidate    = 0;

    uint8_t  mProgress     = 0;
    uint32_t mLastStepEnd  = 0;

    bool     mHasActivity  = false;
    uint32_t mLastActivity = 0;

    uint32_t mRejectAt[kMaxRejectLimit] = {};   ///< ring of recent rejection times
    uint8_t  mRejectCount  = 0;                 ///< valid entries in mRejectAt
    uint8_t  mRejectNext   = 0;                 ///< next slot to write
    bool     mCoolingDown  = false;

    bool     mUnlocked     = false;
};

} // namespace RideLock

#endif // RIDELOCK_UNLOCK_DETECTOR_HPP
