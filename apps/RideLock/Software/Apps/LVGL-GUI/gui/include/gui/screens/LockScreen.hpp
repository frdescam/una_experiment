/**
 ******************************************************************************
 * @file    LockScreen.hpp
 * @brief   RideLock's one screen: a quiet clock face that swallows every
 *          button until the unlock sequence is entered.
 *
 * Idle, it shows a padlock, the time, the date and the battery, and nothing
 * else changes until the next minute. Any button press wakes the guide: the
 * padlock turns amber, dots show how far the sequence has got, and the bezel
 * arc next to the button to press next lights up, so the sequence never has
 * to be memorised. Repeated wrong input (a sleeve) turns the padlock red and
 * pauses unlocking until the buttons have been left alone for a while. When
 * the sequence completes, the padlock opens and the GUI exits, handing the
 * watch back.
 *
 * If the service's drain watchdog fires, the date line becomes a warning.
 ******************************************************************************
 */

#ifndef LOCK_SCREEN_HPP
#define LOCK_SCREEN_HPP

#include <cstdint>
#include <memory>

#include "lvgl.h"

#include "SDK/GUI/LVGL/Buttons.hpp"

#include "RideLock/UnlockDetector.hpp"

#include "gui/model/Model.hpp"
#include "gui/model/ModelListener.hpp"

class LockScreen : public ModelListener
{
public:
    explicit LockScreen(Model& model);
    ~LockScreen() override;

    LockScreen(const LockScreen&)            = delete;
    LockScreen& operator=(const LockScreen&) = delete;

    /// The LVGL screen object, to pass to lv_screen_load().
    lv_obj_t* root() const { return mRoot; }

    // ModelListener
    void onLockConfig(const CustomMessage::LockSettings& settings) override;
    void onClock(const CustomMessage::ClockData& clock) override;
    void onPower(const CustomMessage::PowerData& power) override;
    void onResumed() override;

private:
    enum class Mode : uint8_t {
        Idle,          ///< clock face, nothing lit
        Entering,      ///< guide shown: dots and the next button lit
        CoolingDown,   ///< unlocking paused until the buttons are left alone
        Unlocked,      ///< open padlock, about to exit
    };

    static void keyEventCb(lv_event_t* e);
    static void timerCb(lv_timer_t* timer);

    void onKey(uint8_t code);
    void onTimer();
    void apply(RideLock::Outcome outcome, uint32_t now);
    void setMode(Mode mode, uint32_t now);
    void flash(const char* text, uint32_t now);

    void renderPadlock();
    void renderGuide();
    void renderClock();
    void renderStatus();
    void renderCooldown(uint32_t now);

    Model&    mModel;
    lv_obj_t* mRoot = nullptr;

    lv_obj_t* mShackle  = nullptr;
    lv_obj_t* mBody     = nullptr;
    lv_obj_t* mKeyhole  = nullptr;
    lv_obj_t* mTitle    = nullptr;
    lv_obj_t* mTime     = nullptr;
    lv_obj_t* mMeridiem = nullptr;
    lv_obj_t* mDate     = nullptr;
    lv_obj_t* mStatus   = nullptr;
    lv_obj_t* mDots[RideLock::kMaxSequence] = {};

    std::unique_ptr<SDK::LVGL::Buttons> mButtons;
    lv_timer_t*                         mTimer = nullptr;

    RideLock::UnlockDetector mDetector;
    Mode                     mMode        = Mode::Idle;
    uint32_t                 mModeSince   = 0;
    uint32_t                 mLastKeyAt   = 0;
    uint32_t                 mFlashUntil  = 0;   ///< a feedback message shows until then
    uint32_t                 mShownSecond = 0;   ///< cooldown countdown last drawn
    bool                     mShowCurrent = true;
    char                     mFlash[40]   = {};
};

#endif // LOCK_SCREEN_HPP
