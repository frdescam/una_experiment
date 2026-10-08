/**
 ******************************************************************************
 * @file    Model.cpp
 * @brief   GUI-side state of RideLock (see Model.hpp).
 ******************************************************************************
 */

#include "gui/model/Model.hpp"
#include "gui/model/ModelListener.hpp"

#include "SDK/Kernel/KernelProviderGUI.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include "SDK/Port/LVGL/LvglPort.hpp"

#define LOG_MODULE_PRX      "Model"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace
{

/// Receives model events while no screen is bound.
ModelListener sNullListener;

} // namespace

Model::Model()
    : mKernel(SDK::KernelProviderGUI::GetInstance().getKernel())
{
    SDK::LVGL::Port::GetInstance().setAppLifeCycleCallback(this);
    SDK::LVGL::Port::GetInstance().setCustomMessageHandler(this);

#if defined(SIMULATOR)
    LOG_INFO("RideLock simulator. Keys: 1=L1 2=L2 3=R1 4=R2, Esc quits.\n");
#endif
}

void Model::bind(ModelListener* listener)
{
    mListener = listener;
}

void Model::unlockAndExit()
{
    if (mExiting) {
        return;
    }
    mExiting = true;

    LOG_INFO("Unlocked: handing the display back\n");
    SDK::send_msg<CustomMessage::Unlocked>(mKernel);

    SDK::LVGL::Port::GetInstance().setAppLifeCycleCallback(nullptr);
    SDK::LVGL::Port::GetInstance().setCustomMessageHandler(nullptr);

    // No return on the watch: the kernel stops the GUI process. In the
    // simulator this makes the port's frame loop return.
    mKernel.sys.exit();
}

void Model::onStart()
{
    LOG_INFO("Lock screen started\n");
}

void Model::onResume()
{
    // Another screen may have had the buttons meanwhile; ask for fresh state.
    SDK::send_msg<CustomMessage::Refresh>(mKernel);
    (mListener ? mListener : &sNullListener)->onResumed();
}

void Model::onSuspend()
{
    // Something else is in front of the lock: the service brings it back if
    // that lasts (see LockController::onGuiHidden).
    SDK::send_msg<CustomMessage::Hidden>(mKernel);
}

void Model::onStop()
{
    LOG_INFO("Lock screen stopped\n");
}

bool Model::customMessageHandler(SDK::MessageBase* msg)
{
    ModelListener* listener = mListener ? mListener : &sNullListener;

    switch (msg->getType()) {
        case CustomMessage::LOCK_CONFIG:
            mSettings = static_cast<CustomMessage::LockConfig*>(msg)->data;
            listener->onLockConfig(mSettings);
            break;

        case CustomMessage::CLOCK:
            mClock    = static_cast<CustomMessage::Clock*>(msg)->data;
            mHasClock = true;
            listener->onClock(mClock);
            break;

        case CustomMessage::POWER:
            mPower = static_cast<CustomMessage::Power*>(msg)->data;
            listener->onPower(mPower);
            break;

        default:
            break;
    }
    return true;
}
