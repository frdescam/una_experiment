/**
 ******************************************************************************
 * @file    UnlockDetector.cpp
 * @brief   Deliberate-sequence recogniser (see UnlockDetector.hpp).
 ******************************************************************************
 */

#include "RideLock/UnlockDetector.hpp"

namespace RideLock
{

bool isRejection(Outcome o)
{
    switch (o) {
        case Outcome::Wrong:
        case Outcome::TooLong:
        case Outcome::Chord:
        case Outcome::Timeout:
        case Outcome::NotQuiet:
        case Outcome::CoolingDown:
            return true;
        default:
            return false;
    }
}

UnlockDetector::UnlockDetector()
    : UnlockDetector(defaultSequence(), UnlockTiming{})
{
}

UnlockDetector::UnlockDetector(const Sequence& seq, const UnlockTiming& timing)
    : mSeq(seq)
    , mTiming(timing)
{
}

void UnlockDetector::configure(const Sequence& seq, const UnlockTiming& timing, uint32_t nowMs)
{
    mSeq    = seq;
    mTiming = timing;
    reset(nowMs);
}

void UnlockDetector::reset(uint32_t nowMs)
{
    for (unsigned i = 0; i < kButtonCount; ++i) {
        mDown[i]          = false;
        mReleasedValid[i] = false;
    }
    restart();
    mUnlocked    = false;
    mCoolingDown = false;
    mRejectCount = 0;
    mRejectNext  = 0;

    // Whatever happened before is unknown, so it counts as activity: the
    // first step still needs a quiet period, measured from now.
    noteActivity(nowMs);
}

uint32_t UnlockDetector::cooldownRemaining(uint32_t nowMs) const
{
    if (!mCoolingDown) {
        return 0;
    }
    const uint32_t quietFor = nowMs - mLastActivity;
    return quietFor >= mTiming.cooldownMs ? 0u : mTiming.cooldownMs - quietFor;
}

Outcome UnlockDetector::onInput(Button b, Input in, uint32_t nowMs)
{
    const unsigned i = static_cast<unsigned>(b);
    if (i >= kButtonCount || mUnlocked) {
        return Outcome::None;
    }

    if (mCoolingDown && !endCooldownIfQuiet(nowMs)) {
        // Paused: keep track of what is held, and restart the silence clock.
        if (in == Input::Press) {
            mDown[i]   = true;
            mDownAt[i] = nowMs;
        } else if (in == Input::Release) {
            mDown[i]          = false;
            mReleasedValid[i] = true;
            mReleasedAt[i]    = nowMs;
        }
        noteActivity(nowMs);
        return Outcome::CoolingDown;
    }

    switch (in) {
        case Input::Press:   return press(i, nowMs);
        case Input::Release: return release(i, nowMs);
        case Input::Click:   return click(i, nowMs);
    }
    return Outcome::None;
}

Outcome UnlockDetector::poll(uint32_t nowMs)
{
    if (mUnlocked) {
        return Outcome::None;
    }
    if (mCoolingDown) {
        return endCooldownIfQuiet(nowMs) ? Outcome::CooledDown : Outcome::None;
    }
    if (mProgress == 0 || mHasCandidate) {
        return Outcome::None;
    }
    if (static_cast<uint32_t>(nowMs - mLastStepEnd) > mTiming.maxGapMs) {
        // Giving up half way is what people do too: not counted as noise.
        restart();
        return Outcome::Timeout;
    }
    return Outcome::None;
}

Outcome UnlockDetector::press(unsigned i, uint32_t now)
{
    // An attempt left unfinished for too long is over before this press is
    // judged; poll() normally reports that first.
    if (mProgress > 0 && !mHasCandidate &&
        static_cast<uint32_t>(now - mLastStepEnd) > mTiming.maxGapMs) {
        restart();
    }

    // Another button freshly down makes this a chord. One held past stuckMs
    // is taken to be pinned by the sleeve and does not count.
    bool chord = false;
    for (unsigned j = 0; j < kButtonCount; ++j) {
        if (j != i && mDown[j] &&
            static_cast<uint32_t>(now - mDownAt[j]) < mTiming.stuckMs) {
            chord = true;
        }
    }

    const bool quiet = !mHasActivity ||
                       static_cast<uint32_t>(now - mLastActivity) >= mTiming.quietMs;

    mDown[i]   = true;
    mDownAt[i] = now;
    noteActivity(now);

    if (chord) {
        return reject(Outcome::Chord, now);
    }

    if (mProgress == 0 && !quiet) {
        return reject(Outcome::NotQuiet, now);
    }

    mHasCandidate = true;
    mCandidate    = i;
    return Outcome::None;
}

Outcome UnlockDetector::release(unsigned i, uint32_t now)
{
    const bool     wasDown = mDown[i];
    const uint32_t held    = static_cast<uint32_t>(now - mDownAt[i]);

    mDown[i]          = false;
    mReleasedValid[i] = true;
    mReleasedAt[i]    = now;
    noteActivity(now);

    if (!wasDown || !mHasCandidate || mCandidate != i) {
        return Outcome::None;
    }
    mHasCandidate = false;

    if (held > mTiming.maxPressMs) {
        return reject(Outcome::TooLong, now);
    }

    if (mSeq.length == 0 || static_cast<unsigned>(mSeq.steps[mProgress]) != i) {
        return reject(Outcome::Wrong, now);
    }

    ++mProgress;
    mLastStepEnd = now;

    if (mProgress >= mSeq.length) {
        mUnlocked = true;
        return Outcome::Unlocked;
    }
    return Outcome::Progress;
}

Outcome UnlockDetector::click(unsigned i, uint32_t now)
{
    // The kernel sends CLICK between a short press's PRESS and RELEASE; the
    // RELEASE completes the step, so the click itself adds nothing.
    if (mDown[i]) {
        return Outcome::None;
    }
    if (mReleasedValid[i] &&
        static_cast<uint32_t>(now - mReleasedAt[i]) <= mTiming.clickEchoMs) {
        return Outcome::None;
    }

    // A click with no press around it: a kernel or simulator that reports
    // clicks only. Treat it as a whole press of zero length.
    const Outcome p = press(i, now);
    const Outcome r = release(i, now);
    return (r != Outcome::None) ? r : p;
}

Outcome UnlockDetector::reject(Outcome why, uint32_t now)
{
    restart();

    const uint8_t limit = mTiming.rejectLimit < kMaxRejectLimit
                              ? mTiming.rejectLimit
                              : static_cast<uint8_t>(kMaxRejectLimit);
    if (limit == 0) {
        return why;
    }

    mRejectAt[mRejectNext] = now;
    mRejectNext = static_cast<uint8_t>((mRejectNext + 1) % limit);
    if (mRejectCount < limit) {
        ++mRejectCount;
    }

    uint8_t recent = 0;
    for (uint8_t k = 0; k < mRejectCount; ++k) {
        if (static_cast<uint32_t>(now - mRejectAt[k]) <= mTiming.rejectWindowMs) {
            ++recent;
        }
    }
    if (recent >= limit) {
        mCoolingDown = true;
        return Outcome::CoolingDown;
    }
    return why;
}

bool UnlockDetector::endCooldownIfQuiet(uint32_t now)
{
    if (!mCoolingDown) {
        return true;
    }
    if (static_cast<uint32_t>(now - mLastActivity) < mTiming.cooldownMs) {
        return false;
    }
    mCoolingDown = false;
    mRejectCount = 0;
    mRejectNext  = 0;
    return true;
}

void UnlockDetector::restart()
{
    mProgress     = 0;
    mHasCandidate = false;
}

void UnlockDetector::noteActivity(uint32_t now)
{
    mHasActivity  = true;
    mLastActivity = now;
}

} // namespace RideLock
