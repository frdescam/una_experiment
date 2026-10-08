/**
 ******************************************************************************
 * @file    Model.hpp
 * @brief   GUI-side state of RideLock and its link to the kernel and service.
 *
 * Receives the kernel's lifecycle callbacks and the service's messages, keeps
 * the last value of each, and forwards them to the lock screen. The one thing
 * the GUI tells the service is that the wearer unlocked; it then exits, which
 * hands the display back to the watch.
 ******************************************************************************
 */

#ifndef MODEL_HPP
#define MODEL_HPP

#include "SDK/Interfaces/ICustomMessageHandler.hpp"
#include "SDK/Interfaces/IGuiLifeCycleCallback.hpp"
#include "SDK/Kernel/Kernel.hpp"

#include "Commands.hpp"

class ModelListener;

class Model : public SDK::Interface::IGuiLifeCycleCallback,
              public SDK::Interface::ICustomMessageHandler
{
public:
    Model();

    /// The screen that receives model events.
    void bind(ModelListener* listener);

    /// Report the unlock to the service and close the GUI.
    void unlockAndExit();

    const CustomMessage::LockSettings& lockSettings() const { return mSettings; }
    const CustomMessage::ClockData&    clock() const { return mClock; }
    const CustomMessage::PowerData&    power() const { return mPower; }
    bool                               hasClock() const { return mHasClock; }

protected:
    // IGuiLifeCycleCallback: the kernel's lifecycle, forwarded by the port.
    void onStart() override;
    void onResume() override;
    void onSuspend() override;
    void onStop() override;

    // ICustomMessageHandler: the service's messages.
    bool customMessageHandler(SDK::MessageBase* msg) override;

private:
    ModelListener*            mListener = nullptr;
    const SDK::Kernel&        mKernel;

    CustomMessage::LockSettings mSettings;
    CustomMessage::ClockData    mClock;
    CustomMessage::PowerData    mPower;
    bool                        mHasClock = false;
    bool                        mExiting  = false;
};

#endif // MODEL_HPP
