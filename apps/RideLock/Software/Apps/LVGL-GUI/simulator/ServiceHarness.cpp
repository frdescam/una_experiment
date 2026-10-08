/**
 ******************************************************************************
 * @file    ServiceHarness.cpp
 * @brief   Integration test of the real RideLock service on the SDK's mock
 *          kernel, with this program playing the kernel and the GUI.
 *
 * The PC simulator runs one launch of the app, so it cannot show what the
 * service does across launches: lock at boot, stay resident after an unlock,
 * relock. This harness can. It answers the service's kernel requests itself,
 * records them, and checks them against each scenario:
 *
 *   relock   lockAtBoot, relock after 1 min: the service asks for the lock
 *            screen after the startup grace; while locked it turns music
 *            control (and here notifications) off and sends the GUI its
 *            config; an unlock vibrates and restores the capabilities; and the
 *            lock is requested again one minute later. Takes about 70 s.
 *   noboot   no lockAtBoot, no relock: the service leaves on its own after the
 *            grace without asking for anything.
 *   manual   opened from the launcher, relock off: no request of its own, and
 *            it leaves once the unlocked GUI has gone.
 *   hidden   locked, then another screen goes in front of the lock (a sleeve
 *            finding a kernel gesture, say): one minute later the lock is
 *            brought back to the front; a short interruption is left alone.
 *            Takes about 70 s.
 *
 *   RideLockServiceHarness <relock|noboot|manual|hidden> <scratch-dir>
 *
 * Prints HARNESS PASS or HARNESS FAIL: <why>, and exits 0 or 1.
 ******************************************************************************
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "SDK/Kernel/KernelProviderService.hpp"
#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include "SDK/Simulator/App/AppMessageCore.hpp"
#include "SDK/Simulator/Kernel/Kernel.hpp"
#include "SDK/Simulator/Kernel/Mock/System.hpp"

#include "Commands.hpp"
#include "Service.hpp"

#define LOG_MODULE_PRX      "harness"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace
{

using Clock = std::chrono::steady_clock;

struct Record {
    SDK::MessageType::Type type;
    double                 atSec;          ///< since the harness started
    bool                   notifications;  ///< REQUEST_SET_CAPABILITIES fields
    bool                   music;
    uint32_t               seqLength;      ///< LOCK_CONFIG
};

class Recorder
{
public:
    explicit Recorder(Clock::time_point t0) : mT0(t0) {}

    void add(Record r)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        r.atSec = std::chrono::duration<double>(Clock::now() - mT0).count();
        mRecords.push_back(r);
        mCv.notify_all();
    }

    /// Wait for the next record of @p type after index @p from; returns its index or -1.
    int waitFor(SDK::MessageType::Type type, size_t from, double timeoutSec)
    {
        std::unique_lock<std::mutex> lock(mMutex);
        const auto deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                                 std::chrono::duration<double>(timeoutSec));
        for (;;) {
            for (size_t i = from; i < mRecords.size(); ++i) {
                if (mRecords[i].type == type) {
                    return static_cast<int>(i);
                }
            }
            if (mCv.wait_until(lock, deadline) == std::cv_status::timeout) {
                return -1;
            }
        }
    }

    Record at(int i)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mRecords[static_cast<size_t>(i)];
    }

    size_t size()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mRecords.size();
    }

private:
    Clock::time_point       mT0;
    std::mutex              mMutex;
    std::condition_variable mCv;
    std::vector<Record>     mRecords;
};

bool writeConfig(const std::filesystem::path& outputDir, int relockMinutes, bool lockAtBoot)
{
    std::filesystem::create_directories(outputDir);
    std::ofstream f(outputDir / "app_config.json");
    f << "{ \"schema\": 1, \"values\": { \"relockMinutes\": " << relockMinutes
      << ", \"lockAtBoot\": " << (lockAtBoot ? "true" : "false")
      << ", \"notificationsWhileLocked\": false } }\n";
    return static_cast<bool>(f);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <relock|noboot|manual|hidden> <scratch-dir>\n", argv[0]);
        return 1;
    }
    const std::string scenario = argv[1];
    const std::filesystem::path scratch = std::filesystem::absolute(argv[2]);

    // The mock file system is ../../../../../Output from the working directory.
    const std::filesystem::path cwd = scratch / "a" / "b" / "c" / "d" / "e";
    std::filesystem::create_directories(cwd);
    const bool relock = scenario == "relock" || scenario == "hidden";
    const bool boot   = scenario != "noboot";
    if (!writeConfig(scratch / "Output", relock ? 1 : 0, boot)) {
        std::fprintf(stderr, "cannot write the config\n");
        return 1;
    }
    std::filesystem::current_path(cwd);

    SDK::Simulator::Mock::SystemService serviceSystem;
    SDK::Simulator::Kernel              serviceKernel("service");
    SDK::Simulator::Mock::SystemGUI     guiSystem;
    SDK::Simulator::Kernel              guiKernel("gui");

    SDK::App::MessageCore  appMessageCore;
    SDK::App::DualAppComm& comm = appMessageCore.getAppComm();
    serviceKernel.setIAppComm(comm.getServiceComm());
    serviceKernel.setISystem(&serviceSystem);
    guiKernel.setIAppComm(comm.getGuiComm());
    guiKernel.setISystem(&guiSystem);
    SDK::Simulator::KernelHolder::Create(guiKernel);
    Logger_init(serviceKernel.getKernel().log);
    SDK::KernelProviderService::CreateInstance(&serviceKernel.getKernel());

    const SDK::Kernel& kernel = serviceKernel.getKernel();
    Recorder rec(Clock::now());
    std::atomic<bool> running { true };

    // The kernel: answer every request the service makes, and record it.
    std::thread kernelThread([&] {
        while (running) {
            SDK::MessageBase* msg = nullptr;
            if (!comm.receiveFromApp(msg, 100) || msg == nullptr) {
                continue;
            }
            Record r {};
            r.type = msg->getType();
            SDK::MessageResult result = SDK::MessageResult::SUCCESS;
            switch (msg->getType()) {
                case SDK::MessageType::REQUEST_SET_CAPABILITIES: {
                    auto* caps = static_cast<SDK::Message::RequestSetCapabilities*>(msg);
                    r.notifications = caps->enPhoneNotification;
                    r.music         = caps->enMusicControl;
                } break;
                case SDK::MessageType::REQUEST_SYSTEM_SETTINGS: {
                    auto* s = static_cast<SDK::Message::RequestSystemSettings*>(msg);
                    s->timeFormat = true;
                } break;
                case SDK::MessageType::REQUEST_APP_RUN_GUI:
                case SDK::MessageType::REQUEST_VIBRO_PLAY:
                case SDK::MessageType::REQUEST_BACKLIGHT_SET:
                    break;
                default:
                    // No sensors here: subscriptions fail, as on a watch without them.
                    result = SDK::MessageResult::FAIL;
                    break;
            }
            rec.add(r);
            comm.getMsgManager().signalCompletion(msg, result);
            comm.getMsgManager().releaseMessage(msg);
        }
    });

    // The GUI: read what the service sends it, and record it.
    std::thread guiThread([&] {
        while (running) {
            SDK::MessageBase* msg = nullptr;
            if (!guiKernel.getKernel().comm.getMessage(msg, 100) || msg == nullptr) {
                continue;
            }
            Record r {};
            r.type = msg->getType();
            if (msg->getType() == CustomMessage::LOCK_CONFIG) {
                r.seqLength = static_cast<CustomMessage::LockConfig*>(msg)->data.sequence.length;
            }
            rec.add(r);
            guiKernel.getKernel().comm.releaseMessage(msg);
        }
    });

    auto toService = [&](SDK::MessageType::Type type) {
        auto msg = SDK::make_msg(kernel, type);
        if (msg && comm.sendToService(msg.get())) {
            msg.release();
        }
    };
    auto fromGui = [&](SDK::MessageBase* custom) {
        // What the GUI process's send does: GUI -> service, app-private type.
        guiKernel.getKernel().comm.sendMessage(custom);
        guiKernel.getKernel().comm.releaseMessage(custom);
    };

    Service service(SDK::KernelProviderService::GetInstance().getKernel());
    std::atomic<bool> serviceDone { false };
    std::thread serviceThread([&] { service.run(); serviceDone = true; });

    std::string failure;
    auto check = [&](bool ok, const std::string& what) {
        if (!ok && failure.empty()) {
            failure = what;
        }
        std::printf("HARNESS %s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
        std::fflush(stdout);
        return ok;
    };
    auto waitServiceDone = [&](double sec) {
        for (int i = 0; i < static_cast<int>(sec * 20) && !serviceDone; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return serviceDone.load();
    };

    if (scenario == "relock") {
        int i = rec.waitFor(SDK::MessageType::REQUEST_APP_RUN_GUI, 0, 8.0);
        if (check(i >= 0, "boot: the lock screen is requested")) {
            const double at = rec.at(i).atSec;
            check(at >= 4.5 && at <= 7.0, "boot: ...after the 5 s startup grace (" + std::to_string(at) + " s)");
        }

        size_t mark = rec.size();
        toService(SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN);
        i = rec.waitFor(SDK::MessageType::REQUEST_SET_CAPABILITIES, mark, 3.0);
        if (check(i >= 0, "locked: capabilities are set")) {
            check(!rec.at(i).music, "locked: music control (the L2 hold) is off");
            check(!rec.at(i).notifications, "locked: notifications follow the config (off here)");
        }
        i = rec.waitFor(CustomMessage::LOCK_CONFIG, mark, 3.0);
        if (check(i >= 0, "locked: the GUI gets the lock config")) {
            check(rec.at(i).seqLength == 6, "locked: ...with the six-step default sequence");
        }
        check(rec.waitFor(CustomMessage::CLOCK, mark, 3.0) >= 0, "locked: the GUI gets the clock");
        check(rec.waitFor(CustomMessage::POWER, mark, 3.0) >= 0, "locked: the GUI gets the power state");

        mark = rec.size();
        fromGui(guiKernel.getKernel().comm.allocateMessage<CustomMessage::Unlocked>());
        check(rec.waitFor(SDK::MessageType::REQUEST_VIBRO_PLAY, mark, 3.0) >= 0, "unlock: a confirmation vibration");
        toService(SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP);
        const auto unlockedAt = Clock::now();
        i = rec.waitFor(SDK::MessageType::REQUEST_SET_CAPABILITIES, mark, 3.0);
        if (check(i >= 0, "unlocked: capabilities are restored")) {
            check(rec.at(i).music && rec.at(i).notifications, "unlocked: ...music control and notifications back on");
        }
        check(!serviceDone, "unlocked: the service stays resident for the relock");

        mark = rec.size();
        i = rec.waitFor(SDK::MessageType::REQUEST_APP_RUN_GUI, mark, 66.0);
        const double after = std::chrono::duration<double>(Clock::now() - unlockedAt).count();
        if (check(i >= 0, "relock: the lock screen is requested again")) {
            check(after >= 59.0 && after <= 62.5, "relock: ...one minute after the unlock (" + std::to_string(after) + " s)");
        }

        toService(SDK::MessageType::COMMAND_APP_STOP);
        check(waitServiceDone(3.0), "stop: the service leaves on COMMAND_APP_STOP");
    } else if (scenario == "noboot") {
        check(waitServiceDone(8.0), "the service leaves on its own after the grace");
        check(rec.waitFor(SDK::MessageType::REQUEST_APP_RUN_GUI, 0, 0.1) < 0, "...without asking for the lock screen");
    } else if (scenario == "manual") {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        toService(SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN);   // the user opened it
        check(rec.waitFor(SDK::MessageType::REQUEST_APP_RUN_GUI, 0, 7.0) < 0, "opened by the user: no request of its own");
        check(!serviceDone, "the service stays while its GUI is loaded");
        fromGui(guiKernel.getKernel().comm.allocateMessage<CustomMessage::Unlocked>());
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        check(!serviceDone, "...even after the unlock, until the GUI has gone");
        toService(SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP);
        check(waitServiceDone(3.0), "with relock off, the service leaves once the GUI has gone");
    } else if (scenario == "hidden") {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        toService(SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN);   // opened from the launcher
        auto guiSays = [&](SDK::MessageBase* m) { fromGui(m); };

        // A notification in front for a few seconds: left alone.
        guiSays(guiKernel.getKernel().comm.allocateMessage<CustomMessage::Hidden>());
        std::this_thread::sleep_for(std::chrono::seconds(3));
        guiSays(guiKernel.getKernel().comm.allocateMessage<CustomMessage::Refresh>());

        // Then something stays in front of it.
        size_t mark = rec.size();
        guiSays(guiKernel.getKernel().comm.allocateMessage<CustomMessage::Hidden>());
        const auto hiddenAt = Clock::now();
        int i = rec.waitFor(SDK::MessageType::REQUEST_APP_RUN_GUI, mark, 66.0);
        const double after = std::chrono::duration<double>(Clock::now() - hiddenAt).count();
        if (check(i >= 0, "hidden: the lock is brought back to the front")) {
            check(after >= 59.0 && after <= 62.5, "hidden: ...one relock period later (" + std::to_string(after) + " s)");
        }
        check(rec.waitFor(SDK::MessageType::REQUEST_APP_RUN_GUI, 0, 0.1) == i,
              "hidden: the short interruption was left alone");
        toService(SDK::MessageType::COMMAND_APP_STOP);
        check(waitServiceDone(3.0), "stop: the service leaves on COMMAND_APP_STOP");
    } else {
        check(false, "unknown scenario " + scenario);
    }

    if (!serviceDone) {
        toService(SDK::MessageType::COMMAND_APP_STOP);
        waitServiceDone(3.0);
    }
    running = false;
    serviceThread.join();
    kernelThread.join();
    guiThread.join();

    if (failure.empty()) {
        std::printf("HARNESS PASS (%s)\n", scenario.c_str());
        return 0;
    }
    std::printf("HARNESS FAIL (%s): %s\n", scenario.c_str(), failure.c_str());
    return 1;
}
