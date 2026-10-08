/**
 ******************************************************************************
 * @file    ModelListener.hpp
 * @brief   Events the Model raises towards the lock screen.
 ******************************************************************************
 */

#ifndef MODEL_LISTENER_HPP
#define MODEL_LISTENER_HPP

#include "Commands.hpp"

class ModelListener
{
public:
    virtual ~ModelListener() = default;

    /// How to unlock (sent by the service when the GUI starts).
    virtual void onLockConfig(const CustomMessage::LockSettings& /*settings*/) {}

    /// The local time changed (or was resent).
    virtual void onClock(const CustomMessage::ClockData& /*clock*/) {}

    /// Battery, current or drain state changed.
    virtual void onPower(const CustomMessage::PowerData& /*power*/) {}

    /// The screen is back in front: anything held or half-entered is stale.
    virtual void onResumed() {}
};

#endif // MODEL_LISTENER_HPP
