# Patch: stop the stock activity apps from being started, or left searching, by accident

Against [UNAWatch/una-sdk](https://github.com/UNAWatch/una-sdk) `b5749dc4`.
It touches **Run** (`Examples/Apps/Running`), **Bike** (`Cycling`) and **Hike**
(`Hiking`), the three apps that turn the GNSS on as soon as they open.

| File | What it is |
|---|---|
| `make_activity_patch.py` | Applies the change to an SDK checkout. Every edit asserts its anchor, so it fails loudly on code that has moved |
| `activity-apps.patch` | Its output on `b5749dc4`, for `git apply` |

```bash
cd /path/to/una-sdk                                  # at b5749dc4, or close to it
git apply /path/to/una_experiment/patches/activity-apps.patch
# or, on a newer SDK:
python3 -I /path/to/una_experiment/patches/make_activity_patch.py .
```

`tools/build.sh --activity` does this in a separate worktree and builds the three
`.uapp` files into `dist/`.

## What it changes

**1. A pre-activity budget (service).** `Service::onStartGUI()` subscribes to
`GPS_LOCATION` so that a fix is ready for START. Nothing ever undid that unless
START was pressed or the app was exited. `MainPresenter::onIdleTimeout()`
deliberately does nothing while START is selected, and START is the default
item. The service now records when the pre-activity screen opened. If, five
minutes later, no track has started (`mTrackState == INACTIVE && mGpsWanted`), it
releases the external-HR scan, disconnects and returns from `run()`, which closes
the app. The pre-activity screens hold nothing unsaved.

**2. Hold to start, alone (GUI).** Ending an activity already needs R1 held
through a 1.5 s countdown (`TrackHoldConfirmationView`). Starting needed a
single click, or two without a fix. Now:

- `HoldConfirmMode` gains `Start`.
- On the main menu, `R1_PRESS` on START opens the hold screen in Start mode:
  green, labelled with the existing "Start" text, so there are no new assets. A
  click on START does nothing.
- Completing the hold starts the activity and opens the track screen.
- Releasing early, or any other button going down during the hold (a squeezed
  case), returns to the menu.
- The hold does not begin if another button is already held.
- A completed hold starts the activity even without a GPS fix: the hold is the
  confirmation, as the stock "start anyway" screen was.

30 files (10 per app), +291 / −49, all in hand-written code. `generated/` is untouched.

## Verified

- Builds for the watch with Arm GNU Toolchain 13.3.rel1: `Running`, `Cycling` and
  `Hiking` `_1.5.0-ridelock.uapp`. No new warnings in the touched files.
- Run's TouchGFX PC simulator, driven with real key events under Xvfb:

  | Input on START (GPS fixed) | Stock | Patched |
  |---|---|---|
  | R1 click | recording | nothing |
  | R1 held 2.2 s | recording | recording, after the countdown |
  | R1 held 0.5 s | recording | nothing (back to the menu) |
  | R1 held, L1 pressed during | recording | nothing (cancelled) |
  | L1 held, then R1 held | recording | nothing (not begun) |

- The budget, with a temporary 20 s build: `No activity started within 20 s of
  opening: closing`, and `Gps.Loc ... stopped`.
- The jacket model (`docs/STUDY.md` §6.2): with the patch, recordings started by
  taps, bumps, squeezes and pinned buttons go to zero, and GPS-on time drops from
  72–93 % to 4–21 % of a ride. A single-button *hold* still starts an activity
  (one-sided rubbing: 0.49/h). That is why the patch is a second layer behind
  RideLock, not a replacement for it.

Not verified on a real watch.

## Draft upstream PR

> **fix(apps): don't let a sleeve start an activity, or leave the GNSS searching**
>
> On a watch with no touchscreen, a tight sleeve (motorcycle jacket, wetsuit, ski
> jacket) presses the buttons by itself. From the watch face, R1 R1 opens the
> first activity, and R1 R1 R1 R1 starts a recording:
>
> - Opening Run/Bike/Hike subscribes to `GPS_LOCATION` (`onStartGUI`) to pre-warm
>   a fix, and nothing releases it until the activity starts or the app exits.
>   `MainPresenter::onIdleTimeout()` exits only when START is *not* selected, and
>   START is the default. An activity opened by accident searches for satellites
>   until the battery is flat. UNA rates the watch at 10 days normally and 20 h
>   with GPS.
> - START is a single click (two without a fix), while ending is already a 1.5 s
>   hold.
>
> This PR:
>
> 1. **Service:** closes the app when no activity has started 5 minutes after
>    opening (`skPreActivityBudgetMs`), releasing the GNSS and the strap scan.
> 2. **GUI:** START is held through the existing hold-to-confirm screen, which
>    gains a `Start` mode. The hold must be R1 alone: another button cancels it.
>
> Same change in Running, Cycling and Hiking, no generated code touched. Tested
> in the TouchGFX simulator (click, hold, early release, squeeze during and before
> the hold) and with a shortened budget. Open questions for review: the 5-minute
> value, and whether hold-to-start should be a setting for runners who prefer one
> click.
