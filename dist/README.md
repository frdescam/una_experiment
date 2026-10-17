# Prebuilt apps

Built from this repository with the UNA SDK at `b5749dc4` and Arm GNU Toolchain
13.3.rel1 (`tools/build.sh --activity` reproduces them). They need watch firmware
with kernel 1.4.0 or newer (SDK ABI 3). **Experimental.** The first version of the
lock (RideLock 0.1.0) has run on one watch; this one and the patched apps have
not yet. Check them against `SHA256SUMS`.

| File | What |
|---|---|
| `ButtonLock_0.2.0.uapp` | The lock-screen app (Utility, autostarts). Replaces RideLock: remove `RideLock_0.1.0.uapp` first |
| `Running_1.5.0-holdstart.uapp` | Stock Run + `patches/activity-apps.patch` |
| `Cycling_1.5.0-holdstart.uapp` | Stock Bike + the patch |
| `Hiking_1.5.0-holdstart.uapp` | Stock Hike + the patch |

Installing them, and putting the stock apps back: [`docs/STUDY.md`](../docs/STUDY.md) §7.
