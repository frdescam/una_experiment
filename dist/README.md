# Prebuilt apps

Built from this repository with the UNA SDK at `b5749dc4` and Arm GNU Toolchain
13.3.rel1 (`tools/build.sh --activity` reproduces them). They need watch firmware
with kernel 1.4.0 or newer (SDK ABI 3). **Experimental: not yet run on a real
watch.** Check them against `SHA256SUMS`.

| File | What |
|---|---|
| `RideLock_0.1.0.uapp` | The lock-screen app (Utility, autostarts) |
| `Running_1.5.0-ridelock.uapp` | Stock Run + `patches/activity-apps.patch` |
| `Cycling_1.5.0-ridelock.uapp` | Stock Bike + the patch |
| `Hiking_1.5.0-ridelock.uapp` | Stock Hike + the patch |

Installing them, and putting the stock apps back: [`docs/STUDY.md`](../docs/STUDY.md) §7.
