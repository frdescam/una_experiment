/**
 ******************************************************************************
 * @file    Service.cpp
 * @brief   RideLock service (see Service.hpp).
 ******************************************************************************
 */

#include "Service.hpp"

#include <cmath>
#include <ctime>
#include <memory>

#include "SDK/AppConfig/AppConfig.hpp"
#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include "SDK/Messages/SensorLayerMessages.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserBatteryCharging.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserBatteryLevel.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserBatteryMetrics.hpp"

#include "AppConfigFields.hpp"
#include "Commands.hpp"

#define LOG_MODULE_PRX      "Service"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace
{

/// getMessage() timeout meaning "until a message arrives".
constexpr uint32_t kWaitForever = 0xFFFFFFFFu;

std::tm localNow()
{
    std::tm local {};
    const std::time_t utc = std::time(nullptr);
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&local, &utc);
#else
    localtime_r(&utc, &local);
#endif
    return local;
}

} // namespace

Service::Service(SDK::Kernel& kernel)
    : mKernel(kernel)
    , mSequence(RideLock::defaultSequence())
    , mSensorBatteryLevel(SDK::Sensor::Type::BATTERY_LEVEL)
    , mSensorCharging(SDK::Sensor::Type::BATTERY_CHARGING)
    , mSensorMetrics(SDK::Sensor::Type::BATTERY_METRICS, kCurrentSamplePeriodMs, 0)
{
}

Service::~Service()
{
    disconnectSensors();
}

void Service::run()
{
    LOG_INFO("Started\n");

    // On the service thread, not in the constructor: AppConfig logs, and in
    // the simulator the logger is not ready while the service is constructed.
    loadConfig();

    mLock.start(mKernel.sys.getTimeMs());

    while (true) {
        uint32_t now = mKernel.sys.getTimeMs();

        if (!perform(mLock.poll(now))) {
            return;
        }

        if (mLock.guiLoaded() && publishClock(false) && mSensorsWanted) {
            // Once a minute: retry any subscription that lost the start-up
            // ack race (see Sensor::Connection::connect()), and ask the
            // kernel whether we are charging.
            connectSensors();
            refreshBatteryStatus(now);
        }

        // Sleep until the policy has something to do, or the clock shows a
        // new minute; a resident, unlocked service sleeps until its relock.
        uint32_t wait = mLock.msUntilNextDeadline(now);
        if (mLock.guiLoaded()) {
            const uint32_t minute = msToNextMinute();
            wait = (wait == RideLock::LockController::kNoDeadline || minute < wait) ? minute : wait;
        }
        if (wait == RideLock::LockController::kNoDeadline) {
            wait = kWaitForever;
        }

        SDK::MessageBase* msg = nullptr;
        if (!mKernel.comm.getMessage(msg, wait)) {
            continue;
        }

        now = mKernel.sys.getTimeMs();
        switch (msg->getType()) {
            case SDK::MessageType::COMMAND_APP_STOP:
                LOG_INFO("Stopped by the kernel\n");
                disconnectSensors();
                // We must release the message because this is the last event.
                mKernel.comm.releaseMessage(msg);
                return;

            case SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN:
                LOG_INFO("GUI is now running\n");
                onGuiRun(now);
                break;

            case SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP:
                LOG_INFO("GUI has stopped\n");
                onGuiStop(now);
                break;

            case CustomMessage::UNLOCKED:
                LOG_INFO("Unlocked by the wearer\n");
                onUnlocked(now);
                break;

            case CustomMessage::REFRESH:
                mLock.onGuiShown(now);
                publishClock(true);
                publishPower();
                break;

            case CustomMessage::HIDDEN:
                LOG_INFO("Lock screen hidden behind another screen\n");
                mLock.onGuiHidden(now);
                break;

            case SDK::MessageType::EVENT_SENSOR_LAYER_DATA: {
                auto* event = static_cast<SDK::Message::Sensor::EventData*>(msg);
                SDK::Sensor::DataBatch batch(event->data, event->count, event->stride);
                handleSensorData(event->handle, batch, now);
            } break;

            default:
                // Unknown types are ignored, and released below.
                break;
        }
        mKernel.comm.releaseMessage(msg);
    }
}

// --- configuration -----------------------------------------------------------

void Service::loadConfig()
{
    SDK::AppConfig config(mKernel, RideLockConfig::kFileName, RideLockConfig::kFields,
                          RideLockConfig::kFieldCount);

    char text[RideLockConfig::kSequenceTextMax + 1] = {};
    config.getString(RideLockConfig::kUnlockSequence, text, sizeof(text));
    if (!RideLock::parseSequence(text, mSequence)) {
        LOG_WARNING("unlockSequence \"%s\" is not usable, using the default\n", text);
        mSequence = RideLock::defaultSequence();
    }

    RideLock::LockPolicy policy;
    policy.relockMs   = static_cast<uint32_t>(config.getInt(RideLockConfig::kRelockMinutes)) * 60000u;
    policy.lockAtBoot = config.getBool(RideLockConfig::kLockAtBoot);
    mLock.setPolicy(policy);

    RideLock::DrainConfig drain;
    drain.thresholdMa = static_cast<float>(config.getInt(RideLockConfig::kDrainAlertMa));
    drain.windowMs    = static_cast<uint32_t>(config.getInt(RideLockConfig::kDrainWindowMin)) * 60000u;
    mDrain.configure(drain);

    mNotificationsWhileLocked = config.getBool(RideLockConfig::kNotifications);
    mShowCurrent              = config.getBool(RideLockConfig::kShowCurrent);

    char seq[RideLockConfig::kSequenceTextMax + 1] = {};
    RideLock::formatSequence(mSequence, seq, sizeof(seq));
    LOG_INFO("Config (%s): unlock \"%s\", relock %u min, lock at boot %u, drain alert %u mA / %u min\n",
             config.isLoaded() ? "file" : "defaults", seq,
             static_cast<unsigned>(policy.relockMs / 60000u), policy.lockAtBoot ? 1u : 0u,
             static_cast<unsigned>(drain.thresholdMa), static_cast<unsigned>(drain.windowMs / 60000u));
}

// --- lifecycle ---------------------------------------------------------------

void Service::onGuiRun(uint32_t nowMs)
{
    mLock.onGuiRun(nowMs);

    setCapabilities(true);
    refreshClockFormat();

    publishConfig();
    mLastMinute = -1;
    publishClock(true);

    mDrain.reset();
    connectSensors();
    refreshBatteryStatus(nowMs);
    publishPower();
}

void Service::onGuiStop(uint32_t nowMs)
{
    mLock.onGuiStop(nowMs);

    // Nothing is watched while unlocked: the wearer is using the watch.
    disconnectSensors();
    mDrain.reset();
    setCapabilities(false);
}

void Service::onUnlocked(uint32_t nowMs)
{
    mLock.onUnlocked(nowMs);
    vibrateUnlocked();
}

bool Service::perform(RideLock::LockAction action)
{
    switch (action) {
        case RideLock::LockAction::ShowLock:
            LOG_INFO(mLock.guiLoaded() ? "Bringing the lock screen back to the front\n"
                                       : "Putting the lock screen up\n");
            requestLockScreen();
            return true;

        case RideLock::LockAction::Exit:
            LOG_INFO("Nothing left to do, exiting\n");
            disconnectSensors();
            return false;

        case RideLock::LockAction::None:
            return true;
    }
    return true;
}

void Service::requestLockScreen()
{
    // Loads the GUI and brings it to the front; whatever was on screen is
    // suspended. COMMAND_APP_NOTIF_GUI_RUN confirms it.
    SDK::send_msg<SDK::Message::RequestAppRunGui>(mKernel);
}

void Service::setCapabilities(bool locked)
{
    if (auto msg = SDK::make_msg<SDK::Message::RequestSetCapabilities>(mKernel)) {
        // Locked: the L2 hold for music control is exactly what a cuff does,
        // so it is off. Unlocked (resident service): the kernel's defaults.
        msg->enPhoneNotification = locked ? mNotificationsWhileLocked : true;
        msg->enUsbChargingScreen = true;
        msg->enMusicControl      = !locked;
        msg.send();
    }
}

void Service::refreshClockFormat()
{
    if (auto msg = SDK::make_msg<SDK::Message::RequestSystemSettings>(mKernel)) {
        if (msg.send(100) && msg.ok()) {
            mUse12h     = msg->timeFormat;
            mMonthFirst = msg->dateMonthFirst;
        }
    }
}

// --- sensors -----------------------------------------------------------------

void Service::connectSensors()
{
    // Idempotent: connects only what is not connected yet.
    mSensorsWanted = true;
    if (!mSensorBatteryLevel.isConnected()) {
        mSensorBatteryLevel.connect();
    }
    if (!mSensorCharging.isConnected()) {
        mSensorCharging.connect();
    }
    if ((mShowCurrent || mDrain.config().thresholdMa > 0.0f) && !mSensorMetrics.isConnected()) {
        mSensorMetrics.connect();
    }
}

void Service::disconnectSensors()
{
    // Unconditionally: disconnect() also releases a handle whose connect ack
    // timed out, and is a no-op for one never subscribed.
    mSensorsWanted = false;
    mSensorMetrics.disconnect();
    mSensorCharging.disconnect();
    mSensorBatteryLevel.disconnect();
}

void Service::handleSensorData(uint16_t handle, SDK::Sensor::DataBatch& batch, uint32_t nowMs)
{
    if (batch.size() == 0) {
        return;
    }

    if (mSensorBatteryLevel.matchesDriver(handle)) {
        SDK::SensorDataParser::BatteryLevel parser(batch[0]);
        if (parser.isDataValid() && setBatteryPercent(parser.getCharge())) {
            publishPower();
        }
    } else if (mSensorCharging.matchesDriver(handle)) {
        SDK::SensorDataParser::BatteryCharging parser(batch[0]);
        if (parser.isDataValid() && setCharging(parser.isCharging() || parser.isUsbConnected(), nowMs)) {
            publishPower();
        }
    } else if (mSensorMetrics.matchesDriver(handle)) {
        bool changed = false;
        for (uint16_t i = 0; i < batch.size(); ++i) {
            SDK::SensorDataParser::BatteryMetrics parser(batch[i]);
            if (!parser.isDataValid()) {
                continue;
            }
            // The gauge's own filtered current if it has one, else the
            // instantaneous reading.
            float ma = parser.getAverageCurrent();
            if (ma == 0.0f) {
                ma = parser.getCurrent();
            }
            const RideLock::DrainEvent e = mDrain.addSample(ma, nowMs);
            if (e == RideLock::DrainEvent::Alert) {
                LOG_WARNING("Drain alert: %.1f mA averaged\n", static_cast<double>(mDrain.averageMa()));
                alertDrain();
            } else if (e == RideLock::DrainEvent::Clear) {
                LOG_INFO("Drain over\n");
            }
            changed = true;
        }
        if (changed) {
            publishPower();
        }
    }
}

void Service::refreshBatteryStatus(uint32_t nowMs)
{
    // A second source for charging, besides BATTERY_CHARGING: the watchdog
    // only sees the current's magnitude, so a charge current it took for a
    // drain would raise a false alert.
    if (auto msg = SDK::make_msg<SDK::Message::RequestBatteryStatus>(mKernel)) {
        if (msg.send(100) && msg.ok()) {
            const bool changed = setCharging(msg->isCharging, nowMs) |
                                 setBatteryPercent(msg->batteryLevel);
            if (changed) {
                publishPower();
            }
        }
    }
}

bool Service::setCharging(bool charging, uint32_t nowMs)
{
    if (charging == mCharging) {
        return false;
    }
    mCharging = charging;
    mDrain.setCharging(charging, nowMs);
    return true;
}

bool Service::setBatteryPercent(float level)
{
    const auto pct = static_cast<uint8_t>(level < 0.0f ? 0.0f : (level > 100.0f ? 100.0f : level));
    if (pct == mBatteryPercent) {
        return false;
    }
    mBatteryPercent = pct;
    return true;
}

// --- to the GUI --------------------------------------------------------------

void Service::publishConfig()
{
    CustomMessage::LockSettings settings;
    settings.sequence    = mSequence;
    settings.timing      = mTiming;
    settings.showCurrent = mShowCurrent;
    SDK::send_msg<CustomMessage::LockConfig>(mKernel, settings);
}

bool Service::publishClock(bool force)
{
    const std::tm local = localNow();
    const int minuteOfDay = local.tm_hour * 60 + local.tm_min;
    if (!force && minuteOfDay == mLastMinute) {
        return false;
    }
    mLastMinute = minuteOfDay;

    CustomMessage::ClockData clock;
    clock.hour       = static_cast<uint8_t>(local.tm_hour);
    clock.minute     = static_cast<uint8_t>(local.tm_min);
    clock.weekday    = static_cast<uint8_t>(local.tm_wday);
    clock.day        = static_cast<uint8_t>(local.tm_mday);
    clock.month      = static_cast<uint8_t>(local.tm_mon);
    clock.use12h     = mUse12h;
    clock.monthFirst = mMonthFirst;
    SDK::send_msg<CustomMessage::Clock>(mKernel, clock);
    return true;
}

void Service::publishPower()
{
    CustomMessage::PowerData power;
    power.batteryPercent = mBatteryPercent;
    power.charging       = mCharging;
    power.currentValid   = mDrain.hasEstimate() && !mCharging;
    power.currentMa      = mDrain.averageMa();
    power.drainAlert     = mDrain.alerting();
    power.thresholdMa    = mDrain.config().thresholdMa;
    SDK::send_msg<CustomMessage::Power>(mKernel, power);
}

uint32_t Service::msToNextMinute() const
{
    const std::time_t utc = std::time(nullptr);
    const uint32_t intoMinute = static_cast<uint32_t>(utc % 60);
    // A little past the boundary, so the reading is already the new minute.
    return (60u - intoMinute) * 1000u + 50u;
}

// --- feedback ----------------------------------------------------------------

void Service::vibrateUnlocked()
{
    if (auto msg = SDK::make_msg<SDK::Message::RequestVibroPlay>(mKernel)) {
        msg->notes[0].effect = SDK::Message::RequestVibroPlay::SHORT_DOUBLE_CLICK_MEDIUM_1_100;
        msg->notes[0].pause  = 0;
        msg->notesCount      = 1;
        msg.send();
    }
}

void Service::alertDrain()
{
    if (auto msg = SDK::make_msg<SDK::Message::RequestVibroPlay>(mKernel)) {
        msg->notes[0].effect = SDK::Message::RequestVibroPlay::ALERT_750MS_100;
        msg->notes[1].effect = 0;
        msg->notes[1].pause  = 400;
        msg->notes[2].effect = SDK::Message::RequestVibroPlay::ALERT_750MS_100;
        msg->notesCount      = 3;
        msg.send();
    }
    if (auto msg = SDK::make_msg<SDK::Message::RequestBacklightSet>(mKernel)) {
        msg->brightness       = 100;
        msg->autoOffTimeoutMs = 5000;
        msg.send();
    }
}
