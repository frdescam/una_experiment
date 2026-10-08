/**
 ******************************************************************************
 * @file    LockScreen.cpp
 * @brief   RideLock's lock screen (see LockScreen.hpp).
 *
 * Layout for the round 240 x 240 display, top to bottom:
 *   padlock (y 29-54), title (58), time (70, 60 px), date or drain warning
 *   (142), status or guide text (168), sequence dots (centre y 198).
 ******************************************************************************
 */

#include "gui/screens/LockScreen.hpp"

#include <cstdio>

#include "SDK/GUI/Button.hpp"
#include "SDK/GUI/Color.hpp"
#include "SDK/GUI/LVGL/Draw.hpp"

#include "gui/Assets.hpp"

namespace Draw  = SDK::LVGL::Draw;
namespace Color = SDK::GUI::Color;

using RideLock::Button;
using RideLock::Input;
using RideLock::Outcome;

namespace
{

constexpr uint32_t kTimerPeriodMs  = 200;    ///< guide and countdown refresh
constexpr uint32_t kGuideMs        = 8000;   ///< the guide stays this long after the last press
constexpr uint32_t kFlashMs        = 2500;   ///< feedback after a rejected attempt
constexpr uint32_t kUnlockedShowMs = 700;    ///< the open padlock, before handing back

constexpr int32_t  kDotY       = 198;
constexpr int32_t  kDotSpacing = 18;
constexpr int32_t  kDotRadius  = 5;

constexpr uint32_t kAmber = Color::YELLOW_DARK;

const char* const kWeekdays[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
const char* const kMonths[]   = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

/// Map an SDK::GUI::Button code (click, press or release) to the detector's input.
bool decodeKey(uint8_t code, Button& button, Input& input)
{
    namespace Btn = SDK::GUI::Button;
    switch (code) {
        case Btn::L1:         button = Button::L1; input = Input::Click;   return true;
        case Btn::L2:         button = Button::L2; input = Input::Click;   return true;
        case Btn::R1:         button = Button::R1; input = Input::Click;   return true;
        case Btn::R2:         button = Button::R2; input = Input::Click;   return true;
        case Btn::L1_PRESS:   button = Button::L1; input = Input::Press;   return true;
        case Btn::L2_PRESS:   button = Button::L2; input = Input::Press;   return true;
        case Btn::R1_PRESS:   button = Button::R1; input = Input::Press;   return true;
        case Btn::R2_PRESS:   button = Button::R2; input = Input::Press;   return true;
        case Btn::L1_RELEASE: button = Button::L1; input = Input::Release; return true;
        case Btn::L2_RELEASE: button = Button::L2; input = Input::Release; return true;
        case Btn::R1_RELEASE: button = Button::R1; input = Input::Release; return true;
        case Btn::R2_RELEASE: button = Button::R2; input = Input::Release; return true;
        default:              return false;
    }
}

lv_obj_t* textLabel(lv_obj_t* parent, const lv_font_t* font, int32_t y, uint32_t color)
{
    return Draw::label(parent, font, "", 20, y, 200, LV_TEXT_ALIGN_CENTER, color);
}

void setText(lv_obj_t* label, const char* text, uint32_t color)
{
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, Draw::rgb(color), LV_PART_MAIN);
}

/// One decimal place without printf's float support.
void formatMilliamps(char* buf, size_t size, float ma)
{
    const uint32_t deci = static_cast<uint32_t>(ma * 10.0f + 0.5f);
    std::snprintf(buf, size, "%u.%u mA", static_cast<unsigned>(deci / 10u), static_cast<unsigned>(deci % 10u));
}

} // namespace

LockScreen::LockScreen(Model& model)
    : mModel(model)
{
    mRoot = lv_obj_create(nullptr);
    Draw::applyScreen(mRoot);
    lv_obj_add_event_cb(mRoot, &LockScreen::keyEventCb, LV_EVENT_KEY, this);

    // Padlock: a shackle arc over a body with a keyhole.
    mShackle = Draw::arc(mRoot, SDK::LVGL::kCx, 38, 8, 4, -90, 90, Color::GRAY);
    mBody    = Draw::box(mRoot, 108, 38, 24, 17, Color::GRAY, 3);
    mKeyhole = Draw::dot(mRoot, SDK::LVGL::kCx, 46, 2, Color::BLACK);

    mTitle = textLabel(mRoot, &poppins_regular_14, 58, Color::GRAY);

    // The time: a content-sized label centred on the screen, and the AM/PM
    // suffix placed just after it when the watch uses a 12-hour clock.
    mTime = lv_label_create(mRoot);
    lv_obj_set_style_text_font(mTime, &poppins_semibold_60, LV_PART_MAIN);
    lv_obj_set_style_text_color(mTime, Draw::rgb(Color::WHITE), LV_PART_MAIN);
    lv_label_set_text(mTime, "--:--");
    lv_obj_align(mTime, LV_ALIGN_TOP_MID, 0, 70);

    mMeridiem = lv_label_create(mRoot);
    lv_obj_set_style_text_font(mMeridiem, &poppins_regular_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(mMeridiem, Draw::rgb(Color::GRAY), LV_PART_MAIN);
    lv_label_set_text(mMeridiem, "");

    mDate   = textLabel(mRoot, &poppins_regular_18, 142, Color::WHITE);
    mStatus = textLabel(mRoot, &poppins_regular_14, 168, Color::GRAY);

    for (lv_obj_t*& dot : mDots) {
        dot = Draw::dot(mRoot, SDK::LVGL::kCx, kDotY, kDotRadius, Color::GRAY_DARK);
        Draw::setHidden(dot, true);
    }

    mButtons = std::make_unique<SDK::LVGL::Buttons>(mRoot);
    mButtons->set(SDK::LVGL::Buttons::NONE, SDK::LVGL::Buttons::NONE,
                  SDK::LVGL::Buttons::NONE, SDK::LVGL::Buttons::NONE);

    mTimer = lv_timer_create(&LockScreen::timerCb, kTimerPeriodMs, this);

    // Whatever the service already sent (the model keeps the last of each).
    const uint32_t now = lv_tick_get();
    onLockConfig(mModel.lockSettings());
    if (mModel.hasClock()) {
        onClock(mModel.clock());
    }
    setMode(Mode::Idle, now);

    mModel.bind(this);
}

LockScreen::~LockScreen()
{
    mModel.bind(nullptr);
    if (mTimer) {
        lv_timer_delete(mTimer);
    }
    mButtons.reset();
    lv_obj_delete(mRoot);
}

// --- model events --------------------------------------------------------------

void LockScreen::onLockConfig(const CustomMessage::LockSettings& settings)
{
    mShowCurrent = settings.showCurrent;
    mDetector.configure(settings.sequence, settings.timing, lv_tick_get());
    if (mMode != Mode::Unlocked) {
        setMode(Mode::Idle, lv_tick_get());
    }
}

void LockScreen::onClock(const CustomMessage::ClockData& /*clock*/)
{
    renderClock();
    renderStatus();
}

void LockScreen::onPower(const CustomMessage::PowerData& /*power*/)
{
    renderClock();    // the date line doubles as the drain warning
    renderStatus();
}

void LockScreen::onResumed()
{
    // Releases may have been missed while another screen had the buttons.
    const uint32_t now = lv_tick_get();
    mDetector.reset(now);
    if (mMode != Mode::Unlocked) {
        setMode(Mode::Idle, now);
    }
}

// --- input ---------------------------------------------------------------------

void LockScreen::keyEventCb(lv_event_t* e)
{
    auto* self = static_cast<LockScreen*>(lv_event_get_user_data(e));
    self->onKey(static_cast<uint8_t>(lv_event_get_key(e)));
}

void LockScreen::onKey(uint8_t code)
{
    Button button;
    Input  input;
    if (mMode == Mode::Unlocked || !decodeKey(code, button, input)) {
        return;
    }

    const uint32_t now = lv_tick_get();
    mLastKeyAt = now;
    apply(mDetector.onInput(button, input, now), now);
}

void LockScreen::timerCb(lv_timer_t* timer)
{
    static_cast<LockScreen*>(lv_timer_get_user_data(timer))->onTimer();
}

void LockScreen::onTimer()
{
    const uint32_t now = lv_tick_get();

    const Outcome polled = mDetector.poll(now);
    if (polled != Outcome::None) {
        apply(polled, now);
    }

    switch (mMode) {
        case Mode::Unlocked:
            if (now - mModeSince >= kUnlockedShowMs) {
                mModel.unlockAndExit();
            }
            break;

        case Mode::CoolingDown:
            renderCooldown(now);
            break;

        case Mode::Entering:
            if (mDetector.progress() == 0 && now - mLastKeyAt >= kGuideMs) {
                setMode(Mode::Idle, now);
            } else if (mFlashUntil != 0 && now >= mFlashUntil) {
                mFlashUntil = 0;
                renderStatus();
            }
            break;

        case Mode::Idle:
            break;
    }
}

void LockScreen::apply(Outcome outcome, uint32_t now)
{
    char first[24];
    std::snprintf(first, sizeof(first), "start from %s",
                  RideLock::buttonName(mDetector.sequence()[0]));

    switch (outcome) {
        case Outcome::None:
            // A press: wake the guide if it was asleep.
            if (mMode == Mode::Idle) {
                setMode(Mode::Entering, now);
            }
            return;

        case Outcome::Progress:
            if (mMode != Mode::Entering) {
                setMode(Mode::Entering, now);
            }
            mFlashUntil = 0;
            renderGuide();
            renderStatus();
            return;

        case Outcome::Unlocked:
            setMode(Mode::Unlocked, now);
            return;

        case Outcome::CoolingDown:
            if (mMode != Mode::CoolingDown) {
                setMode(Mode::CoolingDown, now);
            }
            return;

        case Outcome::CooledDown:
            setMode(Mode::Idle, now);
            return;

        case Outcome::Wrong: {
            char text[40];
            std::snprintf(text, sizeof(text), "Wrong button - %s", first);
            flash(text, now);
        } return;

        case Outcome::TooLong:
            flash("Just tap - no holding", now);
            return;

        case Outcome::Chord:
            flash("One button at a time", now);
            return;

        case Outcome::Timeout: {
            char text[40];
            std::snprintf(text, sizeof(text), "Too slow - %s", first);
            flash(text, now);
        } return;

        case Outcome::NotQuiet: {
            char text[40];
            std::snprintf(text, sizeof(text), "Pause, then %s", first);
            flash(text, now);
        } return;
    }
}

void LockScreen::flash(const char* text, uint32_t now)
{
    if (mMode != Mode::Entering) {
        setMode(Mode::Entering, now);
    }
    std::snprintf(mFlash, sizeof(mFlash), "%s", text);
    mFlashUntil = now + kFlashMs;
    renderGuide();
    renderStatus();
}

void LockScreen::setMode(Mode mode, uint32_t now)
{
    mMode        = mode;
    mModeSince   = now;
    mFlashUntil  = 0;
    mShownSecond = 0;

    // The refresh timer only matters while something is moving.
    if (mTimer) {
        if (mode == Mode::Idle) {
            lv_timer_pause(mTimer);
        } else {
            lv_timer_resume(mTimer);
        }
    }

    renderPadlock();
    renderGuide();
    renderClock();
    renderStatus();
    if (mode == Mode::CoolingDown) {
        renderCooldown(now);
    }
}

// --- drawing -------------------------------------------------------------------

void LockScreen::renderPadlock()
{
    uint32_t    color = Color::GRAY;
    const char* title = "LOCKED";
    switch (mMode) {
        case Mode::Idle:        color = Color::GRAY;  title = "LOCKED";    break;
        case Mode::Entering:    color = kAmber;       title = "UNLOCKING"; break;
        case Mode::CoolingDown: color = Color::RED;   title = "PAUSED";    break;
        case Mode::Unlocked:    color = Color::GREEN; title = "UNLOCKED";  break;
    }

    // An open padlock: the shackle swings away from its right-hand post.
    if (mMode == Mode::Unlocked) {
        Draw::setArc(mShackle, -90, 30);
    } else {
        Draw::setArc(mShackle, -90, 90);
    }
    Draw::setArcColor(mShackle, color);
    lv_obj_set_style_bg_color(mBody, Draw::rgb(color), LV_PART_MAIN);
    setText(mTitle, title, color);
}

void LockScreen::renderGuide()
{
    const uint8_t length   = mDetector.length();
    const uint8_t progress = mDetector.progress();
    const bool    showDots = (mMode == Mode::Entering || mMode == Mode::Unlocked);

    const int32_t firstX = SDK::LVGL::kCx - (static_cast<int32_t>(length) - 1) * kDotSpacing / 2;
    for (uint8_t i = 0; i < RideLock::kMaxSequence; ++i) {
        lv_obj_t* dot = mDots[i];
        if (!showDots || i >= length) {
            Draw::setHidden(dot, true);
            continue;
        }
        uint32_t color = Color::GRAY_DARK;
        if (mMode == Mode::Unlocked) {
            color = Color::GREEN;
        } else if (i < progress) {
            color = kAmber;
        }
        lv_obj_set_pos(dot, firstX + i * kDotSpacing - kDotRadius, kDotY - kDotRadius);
        lv_obj_set_style_bg_color(dot, Draw::rgb(color), LV_PART_MAIN);
        Draw::setHidden(dot, false);
    }

    // Light the arc beside the button to press next.
    using B = SDK::LVGL::Buttons;
    B::Color l1 = B::NONE, l2 = B::NONE, r1 = B::NONE, r2 = B::NONE;
    if (mMode == Mode::Entering && length > 0) {
        switch (mDetector.expected()) {
            case Button::L1: l1 = B::AMBER; break;
            case Button::L2: l2 = B::AMBER; break;
            case Button::R1: r1 = B::AMBER; break;
            case Button::R2: r2 = B::AMBER; break;
        }
    }
    mButtons->set(l1, l2, r1, r2);
}

void LockScreen::renderClock()
{
    const CustomMessage::ClockData& c = mModel.clock();
    const CustomMessage::PowerData& p = mModel.power();
    char buf[40];

    if (mModel.hasClock()) {
        if (c.use12h) {
            const unsigned h12 = (c.hour % 12u) == 0 ? 12u : (c.hour % 12u);
            std::snprintf(buf, sizeof(buf), "%u:%02u", h12, static_cast<unsigned>(c.minute));
            lv_label_set_text(mMeridiem, c.hour < 12 ? "AM" : "PM");
        } else {
            std::snprintf(buf, sizeof(buf), "%02u:%02u", static_cast<unsigned>(c.hour),
                          static_cast<unsigned>(c.minute));
            lv_label_set_text(mMeridiem, "");
        }
        lv_label_set_text(mTime, buf);
        lv_obj_update_layout(mTime);
        lv_obj_align_to(mMeridiem, mTime, LV_ALIGN_OUT_RIGHT_BOTTOM, 2, -14);
    }

    if (p.drainAlert) {
        char ma[16];
        formatMilliamps(ma, sizeof(ma), p.currentMa);
        std::snprintf(buf, sizeof(buf), "HIGH DRAIN %s", ma);
        setText(mDate, buf, Color::RED);
    } else if (mModel.hasClock()) {
        const char* wd = kWeekdays[c.weekday % 7u];
        const char* mo = kMonths[c.month % 12u];
        if (c.monthFirst) {
            std::snprintf(buf, sizeof(buf), "%s %s %u", wd, mo, static_cast<unsigned>(c.day));
        } else {
            std::snprintf(buf, sizeof(buf), "%s %u %s", wd, static_cast<unsigned>(c.day), mo);
        }
        setText(mDate, buf, Color::WHITE);
    } else {
        setText(mDate, "", Color::WHITE);
    }
}

void LockScreen::renderStatus()
{
    switch (mMode) {
        case Mode::Entering:
            if (mFlashUntil != 0) {
                setText(mStatus, mFlash, Color::ORANGE_DARK);
            } else {
                setText(mStatus, "Press the lit button", Color::WHITE);
            }
            return;

        case Mode::Unlocked:
            setText(mStatus, "", Color::GRAY);
            return;

        case Mode::CoolingDown:
            return;   // renderCooldown() owns the line

        case Mode::Idle:
            break;
    }

    const CustomMessage::PowerData& p = mModel.power();
    if (p.drainAlert) {
        setText(mStatus, "An app may be running", Color::RED);
        return;
    }

    char buf[40] = {};
    size_t n = 0;
    if (p.batteryPercent != CustomMessage::PowerData::kUnknownLevel) {
        n = static_cast<size_t>(std::snprintf(buf, sizeof(buf), p.charging ? "Charging %u%%" : "%u%%",
                                              static_cast<unsigned>(p.batteryPercent)));
    }
    if (mShowCurrent && p.currentValid && n < sizeof(buf)) {
        char ma[16];
        formatMilliamps(ma, sizeof(ma), p.currentMa);
        std::snprintf(buf + n, sizeof(buf) - n, n > 0 ? "  -  %s" : "%s", ma);
    }
    setText(mStatus, buf, Color::GRAY);
}

void LockScreen::renderCooldown(uint32_t now)
{
    const uint32_t remaining = mDetector.cooldownRemaining(now);
    const uint32_t seconds   = (remaining + 999u) / 1000u;
    if (seconds == mShownSecond && mShownSecond != 0) {
        return;
    }
    mShownSecond = seconds;

    char buf[40];
    std::snprintf(buf, sizeof(buf), "Hands off the buttons: %u s", static_cast<unsigned>(seconds));
    setText(mStatus, buf, Color::RED);
}
