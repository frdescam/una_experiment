/**
 ******************************************************************************
 * @file    SimScript.hpp
 * @brief   Drives the RideLock simulator from a text script, for headless
 *          checks and for the screenshots in the docs.
 *
 * Set RIDELOCK_SIM_SCRIPT to a file of commands, one per line ('#' starts a
 * comment). Buttons are L1, L2, R1, R2. Events go to the GUI exactly as the
 * kernel sends them: PRESS, then on release CLICK (if held under 500 ms) and
 * RELEASE.
 *
 *   wait <ms>                       let real time pass
 *   tap <button> [held_ms]          press and release (default 120 ms)
 *   press <button> / release <button>
 *   clock <h> <m> <wday> <day> <month0> [12h]   show a fixed time
 *   power <percent> <mA> [drain]    battery line; drain=1 raises the alert
 *   shot <file.ppm>                 save the display (240x240, binary PPM), in
 *                                   $RIDELOCK_SIM_SHOTS if that is set
 *   expect_exit <ms>                fail unless the GUI exits within ms
 *   expect_running                  fail if the GUI has exited
 *
 * The run's result is printed as "SCRIPT PASS" or "SCRIPT FAIL: ..." and is
 * the simulator's exit code.
 ******************************************************************************
 */

#ifndef RIDELOCK_SIM_SCRIPT_HPP
#define RIDELOCK_SIM_SCRIPT_HPP

#include <atomic>
#include <string>

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Simulator/App/DualAppComm.hpp"

class SimScript
{
public:
    SimScript(SDK::App::DualAppComm& comm, const SDK::Kernel& kernel, std::string path);

    /// Run the script to the end (call on its own thread).
    void run();

    /// main() sets this once the GUI's frame loop has returned.
    void notifyGuiExited() { mGuiExited = true; }

    bool failed() const { return mFailed; }

private:
    bool step(const std::string& line, std::string& error);
    bool sendButton(const std::string& name, int event);
    bool screenshot(const std::string& file);
    void waitForStableFrame();
    void sleepMs(unsigned ms);

    SDK::App::DualAppComm& mComm;
    const SDK::Kernel&     mKernel;
    std::string            mPath;
    std::atomic<bool>      mGuiExited { false };
    bool                   mFailed = false;
};

#endif // RIDELOCK_SIM_SCRIPT_HPP
