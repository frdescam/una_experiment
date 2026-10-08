/**
 ******************************************************************************
 * @file    Service.hpp
 * @brief   RideLock service: decides when the lock screen is up, keeps it fed
 *          with the time and the battery, and watches for a hidden drain.
 *
 * Built with APP_AUTOSTART, so it starts with the watch. Its lifetime:
 *
 *   boot, lockAtBoot      asks the kernel for the lock screen at once
 *   locked                publishes the clock each minute, battery on change,
 *                         and samples the battery current for the watchdog
 *   unlocked, relock > 0  stays resident with nothing subscribed and wakes
 *                         once, to put the lock back up
 *   unlocked, relock = 0  exits
 *
 * See RideLock/LockController.hpp for the policy and Commands.hpp for what it
 * tells the GUI.
 ******************************************************************************
 */

#ifndef RIDELOCK_SERVICE_HPP
#define RIDELOCK_SERVICE_HPP

#include <cstdint>

#include "SDK/Kernel/KernelProviderService.hpp"
#include "SDK/SensorLayer/SensorConnection.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"

#include "RideLock/DrainWatchdog.hpp"
#include "RideLock/LockController.hpp"
#include "RideLock/Sequence.hpp"
#include "RideLock/UnlockDetector.hpp"

class Service
{
public:
    explicit Service(SDK::Kernel& kernel);
    ~Service();

    void run();

private:
    /// How often the fuel gauge is read while locked. Slow on purpose: the
    /// watchdog judges ten-minute averages.
    static constexpr float kCurrentSamplePeriodMs = 30000.0f;

    void loadConfig();

    void onGuiRun(uint32_t nowMs);
    void onGuiStop(uint32_t nowMs);
    void onUnlocked(uint32_t nowMs);
    void handleSensorData(uint16_t handle, SDK::Sensor::DataBatch& batch, uint32_t nowMs);

    bool perform(RideLock::LockAction action);
    void requestLockScreen();
    void setCapabilities(bool locked);
    void refreshClockFormat();

    void connectSensors();
    void disconnectSensors();
    void refreshBatteryStatus(uint32_t nowMs);
    bool setCharging(bool charging, uint32_t nowMs);   ///< true if it changed
    bool setBatteryPercent(float level);               ///< true if it changed

    void publishConfig();
    /// Send the time if the minute changed (or @p force); true if sent.
    bool publishClock(bool force);
    void publishPower();
    uint32_t msToNextMinute() const;

    void vibrateUnlocked();
    void alertDrain();

    SDK::Kernel& mKernel;

    RideLock::LockController mLock;
    RideLock::DrainWatchdog  mDrain;
    RideLock::Sequence       mSequence;
    RideLock::UnlockTiming   mTiming;
    bool                     mNotificationsWhileLocked = true;
    bool                     mShowCurrent              = true;

    SDK::Sensor::Connection  mSensorBatteryLevel;
    SDK::Sensor::Connection  mSensorCharging;
    SDK::Sensor::Connection  mSensorMetrics;
    bool                     mSensorsWanted = false;   ///< subscribed while the lock is loaded

    uint8_t mBatteryPercent = 0xFF;
    bool    mCharging       = false;
    bool    mUse12h         = false;
    bool    mMonthFirst     = false;
    int     mLastMinute     = -1;   ///< minute of day last published, -1 = none
};

#endif // RIDELOCK_SERVICE_HPP
