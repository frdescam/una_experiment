/**
 ******************************************************************************
 * @file    AppConfigFields.hpp
 * @brief   ButtonLock's configuration fields (set from the phone, or by editing
 *          app_config.json in the app's folder over USB).
 ******************************************************************************
 */

#ifndef BUTTONLOCK_APP_CONFIG_FIELDS_HPP
#define BUTTONLOCK_APP_CONFIG_FIELDS_HPP

#include <cstddef>

#include "SDK/AppConfig/AppConfig.hpp"

namespace ButtonLockConfig
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

/// Longest unlockSequence text accepted. Eight steps of the longest names,
/// "SELECT DOWN SELECT DOWN SELECT DOWN SELECT DOWN", are 47.
constexpr size_t kSequenceTextMax = 64;

extern const SDK::AppConfig::Field kFields[];
extern const size_t                kFieldCount;

} // namespace ButtonLockConfig

#endif // BUTTONLOCK_APP_CONFIG_FIELDS_HPP
