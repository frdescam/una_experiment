# RideLock

A lock screen for the UNA Watch. While it is up, every button press goes to it,
so nothing reaches the launcher or an activity. It unlocks only for a deliberate
sequence, and the screen lights each button to press next. Why it exists and how
well it works: [`docs/STUDY.md`](../../docs/STUDY.md).

![screens](../../docs/images/ridelock-screens.png)

## Using it

- **Locked** looks like a watch face: time, date, battery, and the measured
  battery current (an experiment aid; `showCurrent`).
- **To unlock**, press any button. Then press the button whose bezel arc is lit,
  one at a time: by default **L1 R1 R2 L2 L1 R1**, round the case clockwise from
  the top left, one and a half times. Take a breath before the first press: the
  first step only counts after a short pause.
- **"Hands off the buttons: 15 s"** means unlocking is paused after several wrong
  attempts, which is what a sleeve does. Leave the buttons alone until the count
  ends.
- **It relocks itself** 2 minutes after an unlock (`relockMinutes`). It also
  locks at start-up (`lockAtBoot`). To lock at once, open RideLock from Utilities.
- **"HIGH DRAIN"** means the battery current has stayed above 8 mA for 10 minutes
  while locked: something, probably an activity opened before the lock went up,
  is still running. Unlock, find it (the activity list) and close it.

## Settings

Edited from the phone, if the companion app offers it for sideloaded apps, or as
`app_config.json` in the app's folder on the watch (start from
[`Output/app_config.example.json`](Output/app_config.example.json)). Changes
apply the next time the service starts, i.e. after a power cycle.

| Field | Default | |
|---|---|---|
| `unlockSequence` | `L1 R1 R2 L2 L1 R1` | 2–8 steps, both sides, no button twice in a row; an invalid value falls back to the default |
| `relockMinutes` | `2` | 0 = only when opened by hand |
| `lockAtBoot` | `true` | |
| `notificationsWhileLocked` | `true` | |
| `drainAlertMilliamps` | `8` | 0 = off |
| `drainWindowMinutes` | `10` | |
| `showCurrent` | `true` | |

## Layout

```
Software/Libs/Core/RideLock/   pure C++, no SDK: the logic, host-tested in tests/
    Sequence.*                 buttons, the sequence and its parser
    UnlockDetector.*           the unlock rules
    LockController.*           when the lock goes up (boot, relock, lost, hidden)
    DrainWatchdog.*            the current averager and alert
Software/Libs/Header,Sources/  the service (Service.*), the message contract
                               (Commands.hpp) and the config table (AppConfigFields.*)
Software/Apps/LVGL-GUI/        the GUI: Model, LockScreen, fonts
    simulator/                 PC simulator, its scripts, and the service harness
Software/Apps/RideLock-CMake/  the watch build
Output/app-manifest.json       package metadata and the config fields
```

## Build and test

From the repository root:

```bash
tools/bootstrap.sh && source tools/env.sh
tools/build.sh                 # dist/RideLock_0.1.0.uapp
tools/test.sh --sim            # unit tests, jacket model, simulator, service harness
```

The PC simulator on its own (keys 1–4 are L1, L2, R1, R2):

```bash
cd apps/RideLock/Software/Apps/LVGL-GUI/simulator
cmake -S . -B build-ninja -G Ninja && cmake --build build-ninja
cd build/bin && ./RideLockSimulator
```
