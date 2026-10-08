#!/usr/bin/env python3
"""Apply the "accidental activity" fix to the GPS activity apps of a UNA SDK
checkout (Run, Bike and Hike), in place.

    python3 -I patches/make_activity_patch.py /path/to/una-sdk

Two changes per app, both small and local:

1. Service: a pre-activity budget. Opening an activity turns the GNSS on at
   once, so a fix is ready for START; nothing ever turned it off again if
   START was never pressed. Now, if no activity has started within five
   minutes of opening, the service closes the app (and with it the GNSS and
   the external-HR scan).

2. GUI: hold to start. A click on START (the default item) started the
   activity, or reached a confirm screen where another click did; a sleeve
   produces clicks. START now needs R1 held for the 1.5 s countdown of the
   existing hold-to-finish screen, which gains a Start mode. The hold must be
   R1 alone: another button held when it begins, or pressed during it (the
   case squeezed), cancels it, as does releasing early.

Every edit asserts its anchor, so the script fails loudly rather than
half-applying to an SDK whose code has moved. It was written against
UNAWatch/una-sdk b5749dc4. patches/activity-apps.patch is its output there.
"""

import pathlib
import re
import sys

APPS = ("Running", "Cycling", "Hiking")

BUDGET_MIN = 5


def edit(path: pathlib.Path, old: str, new: str, count: int = 1) -> None:
    text = path.read_text(newline="")
    crlf = "\r\n" in text
    if crlf:
        text = text.replace("\r\n", "\n")
    found = text.count(old)
    if found != count:
        sys.exit(f"{path}: expected {count} occurrence(s) of anchor, found {found}:\n{old}")
    text = text.replace(old, new)
    if crlf:
        text = text.replace("\n", "\r\n")
    path.write_text(text, newline="")


def edit_re(path: pathlib.Path, pattern: str, new: str) -> None:
    text = path.read_text(newline="")
    crlf = "\r\n" in text
    if crlf:
        text = text.replace("\r\n", "\n")
    text, n = re.subn(pattern, new, text, count=1, flags=re.S)
    if n != 1:
        sys.exit(f"{path}: pattern not found:\n{pattern}")
    if crlf:
        text = text.replace("\n", "\r\n")
    path.write_text(text, newline="")


def patch_service(app_dir: pathlib.Path) -> None:
    hdr = app_dir / "Software/Libs/Header/Service.hpp"
    src = app_dir / "Software/Libs/Sources/Service.cpp"

    edit(hdr,
         "    static constexpr uint32_t skSampleLatency        = 1000;\n",
         "    static constexpr uint32_t skSampleLatency        = 1000;\n"
         "\n"
         "    /// Opening the app turns the GNSS on so a fix is ready for START. If no\n"
         "    /// activity starts within this long (the app was opened by a sleeve or a\n"
         "    /// pocket), the service closes the app rather than search until the\n"
         "    /// battery is flat.\n"
         f"    static constexpr uint32_t skPreActivityBudgetMs  = {BUDGET_MIN} * 60 * 1000;\n")

    edit(hdr,
         "    bool         mGpsWanted = false;",
         "    uint32_t     mPreActivitySinceMs = 0;          ///< when the pre-activity screen opened (GUI start)\n"
         "    bool         mGpsWanted = false;")

    edit(src,
         "                        LOG_INFO(\"GPS location subscription recovered after a lost startup connect\\n\");\n"
         "                    }\n"
         "                }\n",
         "                        LOG_INFO(\"GPS location subscription recovered after a lost startup connect\\n\");\n"
         "                    }\n"
         "                }\n"
         "\n"
         "                // Nothing started within the budget: close the app, which\n"
         "                // releases the GNSS. The pre-activity screens hold nothing\n"
         "                // unsaved (settings are saved as they change).\n"
         "                if (mTrackState == Track::State::INACTIVE && mGpsWanted &&\n"
         "                    mKernel.sys.getTimeMs() - mPreActivitySinceMs >= skPreActivityBudgetMs) {\n"
         "                    LOG_INFO(\"No activity started within %u s of opening: closing\\n\",\n"
         "                             static_cast<unsigned>(skPreActivityBudgetMs / 1000u));\n"
         "                    requestAccessoryRelease();\n"
         "                    disconnect();\n"
         "                    return;   // ends the app, GUI included\n"
         "                }\n")

    edit(src,
         "void Service::onStartGUI()\n{\n    mGuiStarted = true;\n",
         "void Service::onStartGUI()\n{\n    mGuiStarted = true;\n"
         "    mPreActivitySinceMs = mKernel.sys.getTimeMs();\n")


def patch_gui(app: str, app_dir: pathlib.Path) -> None:
    gui = app_dir / "Software/Apps/TouchGFX-GUI/gui"
    track_start = "model->trackStart(false);" if app == "Running" else "model->trackStart();"

    # The hold screen's modes gain Start.
    edit(gui / "include/gui/model/Model.hpp",
         "    enum class HoldConfirmMode { Finish, Discard };",
         "    enum class HoldConfirmMode { Finish, Discard, Start };")

    # Main menu: START reacts to R1 going down, and goes to the hold screen.
    edit(gui / "include/gui/main_screen/MainPresenter.hpp",
         "    void startTrack();\n",
         "    void startTrack();\n"
         "    void beginHoldToStart();\n")
    edit(gui / "src/main_screen/MainPresenter.cpp",
         "void MainPresenter::startTrack()\n",
         "void MainPresenter::beginHoldToStart()\n"
         "{\n"
         "    model->setHoldConfirmMode(Model::HoldConfirmMode::Start);\n"
         "}\n"
         "\n"
         "void MainPresenter::startTrack()\n")

    edit(gui / "include/gui/main_screen/MainView.hpp",
         "    bool mGpsFix = false;\n",
         "    bool mGpsFix = false;\n"
         "    uint8_t mOthersDown = 0;   ///< L1, L2, R2 held (one bit each): a squeeze is not a start\n")

    view = gui / "src/main_screen/MainView.cpp"
    edit(view,
         "void MainView::handleKeyEvent(uint8_t key)\n{\n",
         "void MainView::handleKeyEvent(uint8_t key)\n{\n"
         "    switch (key) {\n"
         "        case SDK::GUI::Button::L1_PRESS:   mOthersDown |= 1u;  break;\n"
         "        case SDK::GUI::Button::L2_PRESS:   mOthersDown |= 2u;  break;\n"
         "        case SDK::GUI::Button::R2_PRESS:   mOthersDown |= 4u;  break;\n"
         "        case SDK::GUI::Button::L1_RELEASE: mOthersDown &= ~1u; break;\n"
         "        case SDK::GUI::Button::L2_RELEASE: mOthersDown &= ~2u; break;\n"
         "        case SDK::GUI::Button::R2_RELEASE: mOthersDown &= ~4u; break;\n"
         "        default: break;\n"
         "    }\n"
         "\n")
    edit(view,
         "    if (key == SDK::GUI::Button::R1) {\n"
         "        onConfirm();\n"
         "    }\n",
         "    // START is held, not clicked, as finishing already is: a click is what\n"
         "    // a sleeve or a pocket produces. The hold must be R1 alone, so squeezing\n"
         "    // the case does not count either. The hold screen starts the activity.\n"
         "    if (key == SDK::GUI::Button::R1_PRESS && mOthersDown == 0 &&\n"
         "        menuLayout.getSelectedItem() == Menu::ID_START) {\n"
         "        presenter->beginHoldToStart();\n"
         "        application().gotoTrackHoldConfirmationScreenNoTransition();\n"
         "        return;\n"
         "    }\n"
         "\n"
         "    if (key == SDK::GUI::Button::R1) {\n"
         "        onConfirm();\n"
         "    }\n")
    edit_re(view,
            r"(\n    case Menu::ID_START:\n).*?(\n        break;\n)",
            r"\1        // Started by holding R1 (handleKeyEvent), never by a click.\2")

    # Hold screen: a Start mode.
    presenter = gui / "include/gui/trackholdconfirmation_screen/TrackHoldConfirmationPresenter.hpp"
    edit(presenter,
         "    virtual void onIdleTimeout() override { model->application().gotoTrackActionScreenNoTransition(); }\n",
         "    /** Start mode: the hold completed, start the activity. */\n"
         f"    void startTrack() {{ {track_start} }}\n"
         "\n"
         "    virtual void onIdleTimeout() override\n"
         "    {\n"
         "        if (model->getHoldConfirmMode() == Model::HoldConfirmMode::Start) {\n"
         "            model->application().gotoMainScreenNoTransition();\n"
         "        } else {\n"
         "            model->application().gotoTrackActionScreenNoTransition();\n"
         "        }\n"
         "    }\n")

    vhdr = gui / "include/gui/trackholdconfirmation_screen/TrackHoldConfirmationView.hpp"
    edit(vhdr,
         " * @brief Shared hold-to-confirm screen for ending an activity.\n",
         " * @brief Shared hold-to-confirm screen for starting and ending an activity.\n"
         " *\n"
         " * Start: holding R1 on the main menu's START comes here, and completing the\n"
         " * hold starts the activity; releasing early goes back to the menu.\n")

    vsrc = gui / "src/trackholdconfirmation_screen/TrackHoldConfirmationView.cpp"
    edit(vsrc,
         "    mMode = presenter->getHoldConfirmMode();\n"
         "    const bool finish = (mMode == Model::HoldConfirmMode::Finish);\n"
         "\n"
         "    // Label (\"Hold to Finish\" / \"Hold to Discard\").\n"
         "    questionText.setTypedText(touchgfx::TypedText(finish ? T_TEXT_HOLD_TO_FINISH\n"
         "                                                         : T_TEXT_HOLD_TO_DISCARD));\n"
         "    questionText.invalidate();\n",
         "    mMode = presenter->getHoldConfirmMode();\n"
         "    const bool start  = (mMode == Model::HoldConfirmMode::Start);\n"
         "    const bool finish = (mMode == Model::HoldConfirmMode::Finish) || start;  // both green\n"
         "\n"
         "    // Label (\"Hold to Finish\" / \"Hold to Discard\" / \"Start\").\n"
         "    if (start) {\n"
         "        // The text database has no \"Hold to Start\": the menu's \"Start\",\n"
         "        // centred in the label's box, with the countdown below it.\n"
         "        const int16_t boxX = questionText.getX();\n"
         "        const int16_t boxY = questionText.getY();\n"
         "        const int16_t boxW = questionText.getWidth();\n"
         "        const int16_t boxH = questionText.getHeight();\n"
         "        questionText.setTypedText(touchgfx::TypedText(T_TEXT_START));\n"
         "        questionText.resizeToCurrentText();\n"
         "        questionText.setXY(boxX + (boxW - questionText.getWidth()) / 2,\n"
         "                           boxY + (boxH - questionText.getHeight()) / 2);\n"
         "    } else {\n"
         "        questionText.setTypedText(touchgfx::TypedText(finish ? T_TEXT_HOLD_TO_FINISH\n"
         "                                                             : T_TEXT_HOLD_TO_DISCARD));\n"
         "    }\n"
         "    questionText.invalidate();\n")
    edit(vsrc,
         "    // Releasing R1 before the countdown completes cancels and returns to the menu.\n"
         "    if (key == SDK::GUI::Button::R1_RELEASE && !mFired) {\n"
         "        timerRing.stop();\n"
         "        application().gotoTrackActionScreenNoTransition();\n"
         "    }\n",
         "    // Releasing R1 before the countdown completes cancels and returns to the\n"
         "    // menu it came from. A start must be R1 alone: another button going down\n"
         "    // (the case squeezed) cancels it too.\n"
         "    const bool other = key == SDK::GUI::Button::L1_PRESS ||\n"
         "                       key == SDK::GUI::Button::L2_PRESS ||\n"
         "                       key == SDK::GUI::Button::R2_PRESS;\n"
         "    if (!mFired && mMode == Model::HoldConfirmMode::Start &&\n"
         "        (key == SDK::GUI::Button::R1_RELEASE || other)) {\n"
         "        timerRing.stop();\n"
         "        application().gotoMainScreenNoTransition();\n"
         "    } else if (key == SDK::GUI::Button::R1_RELEASE && !mFired) {\n"
         "        timerRing.stop();\n"
         "        application().gotoTrackActionScreenNoTransition();\n"
         "    }\n")
    edit(vsrc,
         "    mFired = true;\n"
         "    if (mMode == Model::HoldConfirmMode::Finish) {\n",
         "    mFired = true;\n"
         "    if (mMode == Model::HoldConfirmMode::Start) {\n"
         "        presenter->startTrack();\n"
         "        application().gotoTrackScreenNoTransition();\n"
         "    } else if (mMode == Model::HoldConfirmMode::Finish) {\n")


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    sdk = pathlib.Path(sys.argv[1])
    for app in APPS:
        app_dir = sdk / "Examples/Apps" / app
        patch_service(app_dir)
        patch_gui(app, app_dir)
        print(f"patched {app}")


if __name__ == "__main__":
    main()
