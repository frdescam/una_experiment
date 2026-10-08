/**
 ******************************************************************************
 * @file    main.cpp
 * @brief   RideLock LVGL PC simulator.
 *
 * Runs the app's real service and LVGL GUI processes against the SDK's mock
 * kernel (SDK/Libs/Source/Simulator): simulated sensors, a directory as the
 * file system, and the kernel's message protocol between the two processes.
 * The display is an SDL2 window showing the same ABGR2222 frames the watch
 * would receive. With RIDELOCK_SIM_SCRIPT set, a script drives the buttons
 * (see SimScript.hpp) and the exit code is its result.
 *
 * The mock kernel runs one app, as launched by the user: the GUI exiting after
 * an unlock ends the simulation. The service's relock and boot behaviour is
 * covered by the host tests of LockController (tests/LockController_test.cpp).
 ******************************************************************************
 */

#include <cstdlib>
#include <memory>
#include <thread>

#include "SDK/Kernel/KernelProviderGUI.hpp"
#include "SDK/Kernel/KernelProviderService.hpp"
#include "SDK/Port/LVGL/LvglPort.hpp"
#include "SDK/Simulator/App/AppMessageCore.hpp"
#include "SDK/Simulator/App/KernelMessageDispatcher.hpp"
#include "SDK/Simulator/Kernel/Kernel.hpp"
#include "SDK/Simulator/Kernel/Mock/Backlight.hpp"
#include "SDK/Simulator/Kernel/Mock/Buzzer.hpp"
#include "SDK/Simulator/Kernel/Mock/System.hpp"
#include "SDK/Simulator/Kernel/Mock/Vibro.hpp"
#include "SDK/Simulator/LVGL/LvglHost.hpp"

#include "Service.hpp"
#include "SimScript.hpp"

#define LOG_MODULE_PRX      "main"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

/// The GUI's entry hook, as on the watch (gui/src/GuiApp.cpp).
extern "C" void una_lvgl_app_init(void);

int main(int, char**)
{
    // Each process gets its own kernel view, as on the watch. The kernels are
    // declared before the message core on purpose: the core's destructor logs
    // the messages it drains, through the logger the service kernel owns, so
    // it has to go first when main() returns.
    SDK::Simulator::Mock::SystemService serviceSystem;
    SDK::Simulator::Kernel              serviceKernel("service");
    SDK::Simulator::Mock::SystemGUI     guiSystem;
    SDK::Simulator::Kernel              guiKernel("gui");

    // One message pool and the Service <-> GUI queues, as the kernel provides.
    SDK::App::MessageCore appMessageCore;
    SDK::App::DualAppComm& appComm = appMessageCore.getAppComm();

    serviceKernel.setIAppComm(appComm.getServiceComm());
    serviceKernel.setISystem(&serviceSystem);
    guiKernel.setIAppComm(appComm.getGuiComm());
    guiKernel.setISystem(&guiSystem);
    SDK::Simulator::KernelHolder::Create(guiKernel);

    Logger_init(serviceKernel.getKernel().log);
    SDK::KernelProviderService::CreateInstance(&serviceKernel.getKernel());
    SDK::KernelProviderGUI::CreateInstance(&guiKernel.getKernel());

    // The service process.
    Service service(SDK::KernelProviderService::GetInstance().getKernel());

    // The kernel side: answers both processes' requests and runs the sensors.
    SDK::Simulator::Mock::Backlight   backlight;
    SDK::Simulator::Mock::Buzzer      buzzer;
    SDK::Simulator::Mock::Vibro       vibro;
    SDK::App::KernelMessageDispatcher dispatcher(appComm, appComm.getMsgManager(), vibro, backlight, buzzer);

    // The display, ticks and buttons the kernel would supply to the GUI.
    SDK::Simulator::LvglHost::Options options;
    options.title = "RideLock (LVGL)";
    options.scale = 2;
    SDK::Simulator::LvglHost host(appComm, serviceKernel.getKernel(), options);
    if (!host.init()) {
        return 1;
    }

    std::thread serviceThread(&Service::run, &service);
    std::thread dispatcherThread(&SDK::App::KernelMessageDispatcher::run, &dispatcher);

    // The GUI process, exactly as EntryPoint/LVGL/main.cpp runs it on the watch.
    SDK::LVGL::Port& port = SDK::LVGL::Port::GetInstance();
    port.init();
    una_lvgl_app_init();

    host.start();

    std::unique_ptr<SimScript> script;
    std::thread                scriptThread;
    if (const char* path = std::getenv("RIDELOCK_SIM_SCRIPT")) {
        script = std::make_unique<SimScript>(appComm, serviceKernel.getKernel(), path);
        scriptThread = std::thread(&SimScript::run, script.get());
    }

    host.run();
    if (script) {
        script->notifyGuiExited();
        scriptThread.join();
    }
    host.shutdown();

    serviceThread.join();
    dispatcherThread.join();

    LOG_INFO("Simulator finished\n");
    return (script && script->failed()) ? 2 : 0;
}
