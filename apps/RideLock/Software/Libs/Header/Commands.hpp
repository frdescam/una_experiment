/**
 ******************************************************************************
 * @file    Commands.hpp
 * @brief   Message contract between the RideLock service and its GUI.
 *
 * The service owns everything that outlives a screen: the configuration, the
 * clock, the battery and the drain watchdog. The GUI owns the buttons, so the
 * unlock decision is made there and reported once.
 *
 *   service -> GUI   LockConfig  on every GUI start: sequence and timing
 *                    Clock       on start, on resume, and at each new minute
 *                    Power       battery, charging, measured current, drain alert
 *   GUI -> service   Unlocked    the sequence was entered; the GUI then exits
 *                    Refresh     the GUI came back on screen: resend state
 *                    Hidden      another screen went in front of the lock
 *
 * Each message carries a plain data struct, which the GUI keeps a copy of
 * (messages themselves cannot be copied).
 ******************************************************************************
 */

#ifndef RIDELOCK_COMMANDS_HPP
#define RIDELOCK_COMMANDS_HPP

#include <cstdint>

#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageTypes.hpp"

#include "RideLock/Sequence.hpp"
#include "RideLock/UnlockDetector.hpp"

// Force 4-byte alignment for all message structures
#pragma pack(push, 4)

namespace CustomMessage
{

// Service --> GUI
constexpr SDK::MessageType::Type LOCK_CONFIG = 0x00000001;
constexpr SDK::MessageType::Type CLOCK       = 0x00000002;
constexpr SDK::MessageType::Type POWER       = 0x00000003;

// GUI --> Service
constexpr SDK::MessageType::Type UNLOCKED    = 0x00000010;
constexpr SDK::MessageType::Type REFRESH     = 0x00000011;
constexpr SDK::MessageType::Type HIDDEN      = 0x00000012;

/// How to unlock, read from the app's configuration by the service.
struct LockSettings {
    RideLock::Sequence     sequence    = RideLock::defaultSequence();
    RideLock::UnlockTiming timing      = {};
    bool                   showCurrent = true;   ///< show the measured current
};

/// Local wall-clock time, already broken down by the service.
struct ClockData {
    uint8_t hour       = 0;      ///< 0..23
    uint8_t minute     = 0;      ///< 0..59
    uint8_t weekday    = 0;      ///< 0 = Sunday
    uint8_t day        = 1;      ///< 1..31
    uint8_t month      = 0;      ///< 0 = January
    bool    use12h     = false;  ///< the watch is set to a 12-hour clock
    bool    monthFirst = false;  ///< the watch writes the month before the day
};

/// Battery and drain state.
struct PowerData {
    static constexpr uint8_t kUnknownLevel = 0xFF;

    uint8_t batteryPercent = kUnknownLevel;   ///< kUnknownLevel until the first reading
    bool    charging       = false;
    bool    currentValid   = false;           ///< currentMa holds a reading
    bool    drainAlert     = false;           ///< the watchdog suspects a hidden drain
    float   currentMa      = 0.0f;            ///< average current magnitude, mA
    float   thresholdMa    = 0.0f;            ///< the alert level, for the screen to quote
};

struct LockConfig : public SDK::MessageBase {
    LockSettings data;

    LockConfig() : SDK::MessageBase(LOCK_CONFIG), data{} {}
    explicit LockConfig(const LockSettings& d) : SDK::MessageBase(LOCK_CONFIG), data(d) {}
};

struct Clock : public SDK::MessageBase {
    ClockData data;

    Clock() : SDK::MessageBase(CLOCK), data{} {}
    explicit Clock(const ClockData& d) : SDK::MessageBase(CLOCK), data(d) {}
};

struct Power : public SDK::MessageBase {
    PowerData data;

    Power() : SDK::MessageBase(POWER), data{} {}
    explicit Power(const PowerData& d) : SDK::MessageBase(POWER), data(d) {}
};

struct Unlocked : public SDK::MessageBase {
    Unlocked() : SDK::MessageBase(UNLOCKED) {}
};

struct Refresh : public SDK::MessageBase {
    Refresh() : SDK::MessageBase(REFRESH) {}
};

struct Hidden : public SDK::MessageBase {
    Hidden() : SDK::MessageBase(HIDDEN) {}
};

// Every message must fit the largest kernel message pool block.
static_assert(sizeof(LockConfig) <= 256, "LockConfig must fit a 256-byte pool block");
static_assert(sizeof(Clock)      <= 256, "Clock must fit a 256-byte pool block");
static_assert(sizeof(Power)      <= 256, "Power must fit a 256-byte pool block");

} // namespace CustomMessage

#pragma pack(pop)

#endif // RIDELOCK_COMMANDS_HPP
