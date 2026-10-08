/**
 ******************************************************************************
 * @file    AppConfigFields.hpp
 * @brief   RideLock's configuration fields (set from the phone, or by editing
 *          app_config.json in the app's folder over USB).
 ******************************************************************************
 */

#ifndef RIDELOCK_APP_CONFIG_FIELDS_HPP
#define RIDELOCK_APP_CONFIG_FIELDS_HPP

#include <cstddef>

#include "SDK/AppConfig/AppConfig.hpp"

namespace RideLockConfig
{

/// The values file, in the app's sandbox root (Apps/<APP_ID>/ on the watch).
constexpr const char* kFileName = "app_config.json";

constexpr const char* kUnlockSequence  = "unlockSequence";
constexpr const char* kRelockMinutes   = "relockMinutes";
constexpr const char* kLockAtBoot      = "lockAtBoot";
constexpr const char* kNotifications   = "notificationsWhileLocked";
constexpr const char* kDrainAlertMa    = "drainAlertMilliamps";
constexpr const char* kDrainWindowMin  = "drainWindowMinutes";
constexpr const char* kShowCurrent     = "showCurrent";

/// Longest unlockSequence text accepted ("L1 R1 R2 L2 L1 R1 L2 R2" is 23).
constexpr size_t kSequenceTextMax = 32;

extern const SDK::AppConfig::Field kFields[];
extern const size_t                kFieldCount;

} // namespace RideLockConfig

#endif // RIDELOCK_APP_CONFIG_FIELDS_HPP
