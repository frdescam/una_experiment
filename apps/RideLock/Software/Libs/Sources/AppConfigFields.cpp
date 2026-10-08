/**
 ******************************************************************************
 * @file    AppConfigFields.cpp
 * @brief   The app's copy of the configuration contract in its app-manifest.json.
 ******************************************************************************
 */

#include "AppConfigFields.hpp"

namespace RideLockConfig
{

using SDK::AppConfig;

// Every value here must match Output/app-manifest.json exactly. Check with the
// SDK's validate_app_config.py --check <app-manifest.json> --check-bounds <this file>.
const AppConfig::Field kFields[] = {
    AppConfig::stringField("unlockSequence", "L1 R1 R2 L2 L1 R1", 5, 32),
    AppConfig::intField("relockMinutes", 2, 0, 120),
    AppConfig::boolField("lockAtBoot", true),
    AppConfig::boolField("notificationsWhileLocked", true),
    AppConfig::intField("drainAlertMilliamps", 8, 0, 200),
    AppConfig::intField("drainWindowMinutes", 10, 2, 60),
    AppConfig::boolField("showCurrent", true),
};

const size_t kFieldCount = sizeof(kFields) / sizeof(kFields[0]);

} // namespace RideLockConfig
