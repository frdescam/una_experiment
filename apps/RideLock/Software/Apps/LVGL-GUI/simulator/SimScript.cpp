/**
 ******************************************************************************
 * @file    SimScript.cpp
 * @brief   Scripted input and screenshots for the RideLock simulator.
 ******************************************************************************
 */

#include "SimScript.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

#include <SDL.h>

#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include "SDK/Port/LVGL/LvglPort.hpp"

#include "Commands.hpp"

namespace
{

using Btn = SDK::Message::EventButton;

constexpr unsigned kKernelClickMaxMs = 500;   // HWButtons: CLICK only under this

bool parseButton(const std::string& name, Btn::Id& id)
{
    // Kernel ids follow the hardware: SW1 = L1, SW2 = R1, SW3 = L2, SW4 = R2.
    if (name == "L1") { id = Btn::Id::SW1; return true; }
    if (name == "R1") { id = Btn::Id::SW2; return true; }
    if (name == "L2") { id = Btn::Id::SW3; return true; }
    if (name == "R2") { id = Btn::Id::SW4; return true; }
    return false;
}

template <typename T, typename Data>
bool sendToGui(SDK::App::DualAppComm& comm, const SDK::Kernel& kernel, const Data& data)
{
    auto msg = SDK::make_msg<T>(kernel, data);
    if (msg && comm.sendToGui(msg.get())) {
        msg.release();   // the GUI releases it after handling
        return true;
    }
    return false;
}

} // namespace

SimScript::SimScript(SDK::App::DualAppComm& comm, const SDK::Kernel& kernel, std::string path)
    : mComm(comm)
    , mKernel(kernel)
    , mPath(std::move(path))
{
}

void SimScript::run()
{
    std::ifstream in(mPath);
    std::string   error;
    if (!in) {
        error = "cannot open " + mPath;
    } else {
        waitForStableFrame();   // let the app come up

        std::string line;
        unsigned    number = 0;
        while (error.empty() && std::getline(in, line)) {
            ++number;
            const auto hash = line.find('#');
            if (hash != std::string::npos) {
                line.erase(hash);
            }
            if (line.find_first_not_of(" \t\r") == std::string::npos) {
                continue;
            }
            std::printf("SCRIPT %u: %s\n", number, line.c_str());
            std::fflush(stdout);
            if (!step(line, error)) {
                error = "line " + std::to_string(number) + ": " + error;
            }
        }
    }

    mFailed = !error.empty();
    if (mFailed) {
        std::printf("SCRIPT FAIL: %s\n", error.c_str());
    } else {
        std::printf("SCRIPT PASS\n");
    }
    std::fflush(stdout);

    // Close the simulator if the app is still up.
    if (!mGuiExited) {
        SDL_Event quit {};
        quit.type = SDL_QUIT;
        SDL_PushEvent(&quit);
    }
}

bool SimScript::step(const std::string& line, std::string& error)
{
    std::istringstream ss(line);
    std::string cmd;
    ss >> cmd;

    if (cmd == "wait") {
        unsigned ms = 0;
        ss >> ms;
        sleepMs(ms);
        return true;
    }

    if (cmd == "tap" || cmd == "press" || cmd == "release") {
        std::string name;
        unsigned    held = 120;
        ss >> name >> held;
        Btn::Id id;
        if (!parseButton(name, id)) {
            error = "unknown button '" + name + "'";
            return false;
        }
        if (cmd == "press") {
            return sendButton(name, static_cast<int>(Btn::Event::PRESS)) || (error = "send failed", false);
        }
        if (cmd == "release") {
            return sendButton(name, static_cast<int>(Btn::Event::RELEASE)) || (error = "send failed", false);
        }
        sendButton(name, static_cast<int>(Btn::Event::PRESS));
        sleepMs(held);
        if (held < kKernelClickMaxMs) {
            sendButton(name, static_cast<int>(Btn::Event::CLICK));
        }
        sendButton(name, static_cast<int>(Btn::Event::RELEASE));
        return true;
    }

    if (cmd == "clock") {
        unsigned h = 0, m = 0, wday = 0, day = 1, month = 0;
        std::string fmt;
        ss >> h >> m >> wday >> day >> month >> fmt;
        CustomMessage::ClockData c;
        c.hour    = static_cast<uint8_t>(h);
        c.minute  = static_cast<uint8_t>(m);
        c.weekday = static_cast<uint8_t>(wday);
        c.day     = static_cast<uint8_t>(day);
        c.month   = static_cast<uint8_t>(month);
        c.use12h  = (fmt == "12h");
        return sendToGui<CustomMessage::Clock>(mComm, mKernel, c) || (error = "send failed", false);
    }

    if (cmd == "power") {
        unsigned pct   = 0;
        float    ma    = 0.0f;
        unsigned drain = 0;
        ss >> pct >> ma >> drain;
        CustomMessage::PowerData p;
        p.batteryPercent = static_cast<uint8_t>(pct);
        p.currentValid   = true;
        p.currentMa      = ma;
        p.drainAlert     = drain != 0;
        p.thresholdMa    = 8.0f;
        return sendToGui<CustomMessage::Power>(mComm, mKernel, p) || (error = "send failed", false);
    }

    if (cmd == "shot") {
        std::string file;
        ss >> file;
        if (const char* dir = std::getenv("RIDELOCK_SIM_SHOTS")) {
            file = std::string(dir) + "/" + file;
        }
        waitForStableFrame();
        if (!screenshot(file)) {
            error = "cannot write " + file;
            return false;
        }
        return true;
    }

    if (cmd == "expect_exit") {
        unsigned ms = 0;
        ss >> ms;
        for (unsigned waited = 0; waited <= ms; waited += 50) {
            if (mGuiExited) {
                return true;
            }
            sleepMs(50);
        }
        error = "the GUI did not exit";
        return false;
    }

    if (cmd == "expect_running") {
        if (mGuiExited) {
            error = "the GUI has exited";
            return false;
        }
        return true;
    }

    error = "unknown command '" + cmd + "'";
    return false;
}

bool SimScript::sendButton(const std::string& name, int event)
{
    Btn::Id id;
    if (!parseButton(name, id)) {
        return false;
    }
    auto msg = SDK::make_msg<Btn>(mKernel);
    if (!msg) {
        return false;
    }
    msg->timestamp = mKernel.sys.getTimeMs();
    msg->id        = id;
    msg->event     = static_cast<Btn::Event>(event);
    if (!mComm.sendToGui(msg.get())) {
        return false;
    }
    msg.release();
    return true;
}

bool SimScript::screenshot(const std::string& file)
{
    const uint8_t* frame = SDK::LVGL::Port::GetInstance().frame();
    std::FILE* f = std::fopen(file.c_str(), "wb");
    if (!f || !frame) {
        if (f) {
            std::fclose(f);
        }
        return false;
    }
    const int w = SDK::LVGL::kDisplayWidth;
    const int h = SDK::LVGL::kDisplayHeight;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; ++i) {
        // ABGR2222: R in bits 1:0, G in 3:2, B in 5:4; each level is 85 apart.
        const uint8_t px     = frame[i];
        const uint8_t rgb[3] = {
            static_cast<uint8_t>((px & 0x03) * 85),
            static_cast<uint8_t>(((px >> 2) & 0x03) * 85),
            static_cast<uint8_t>(((px >> 4) & 0x03) * 85),
        };
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
    return true;
}

void SimScript::waitForStableFrame()
{
    auto& port = SDK::LVGL::Port::GetInstance();
    for (int i = 0; i < 20; ++i) {
        const uint32_t before = port.frameCount();
        sleepMs(300);
        if (before > 0 && port.frameCount() == before) {
            return;
        }
    }
}

void SimScript::sleepMs(unsigned ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
