# How short can the unlock be?

**Question.** The wearer asked two things. Are six presses overkill? And would
three presses in the right order ever happen by chance? This study measures, for
many candidate unlocks, how often jacket-cuff noise opens the lock by itself,
what that noise then does to the unlocked watch, and how long a person takes to
unlock.

**Short answer.**

- **Six presses were overkill.** Each extra step cuts false unlocks about
  10–17×. Six steps cost 1.8 s more than four with gloves, and succeed at the
  first try less often (74 % against 83 %).
- **The default is four steps, one lap of the case: UP SELECT BACK DOWN.**
  - Under the worst modelled noise it opens about 20 times per 1000 h at the
    harsh level (once per 50 h of sustained hostile noise), and about 1.3 times
    at the realistic level (once per 800 h).
  - It needs all four buttons. A cuff that reaches only two or three of them
    never opens it, at any noise level tried.
- **Three steps are a setting**, for wear without a tight cuff: about 245 per
  1000 h at the harsh level (once per 4 h) and 13 at the realistic level (once
  per 80 h). Any three different buttons in any order do equally well.
- **Codes on two buttons are refused**, and so is every 2-step code. A cuff
  rocking across two neighbouring buttons opens UP SELECT UP about once an hour,
  even at the realistic level.
- **A hold to finish is not offered.** A hold is worth about one more tap, no
  more, and it needs an indicator on screen that says when it is long enough.
- **Three presses in the right order do happen by chance**: several times an
  hour under the harsh models, if nothing else stands in the way. ButtonLock's rules are what make a
  short code viable.

Everything here comes from models, and the models are invented. Trust the
per-step factor and the ranking more than the absolute numbers (§6).

## 1. Why three presses in the right order happen by chance

- **Per try, the odds are small.** A specific order of three buttons is 1 chance
  in 64 (4 × 4 × 4).
- **A cuff makes hundreds of tries an hour.** The random-taps model presses 600
  times an hour, and every press can start a try: 600 / 64 ≈ 9 an hour. A naive
  3-press lock (the right three presses in order, any timing) measures about
  5.6 false unlocks an hour under random taps, and 11.6 under chaos.
- **ButtonLock's rules make short codes viable.** The same three presses, with
  the rules on, open 23× less often under random taps (245 per 1000 h) and
  thousands of times less often under chaos (3 per 1000 h). The rules:
  - **quiet first**: the first step counts only after 0.6 s without input;
  - **brisk**: each press under 1.2 s, the next within 2.5 s;
  - **a wrong button ends the try**, and that press cannot start a new one;
  - **the cool-down**: 3 failed tries within 30 s pause unlocking until 15 s
    of silence.
- **Each extra step cuts false unlocks about 10–17×.** On the worst model at
  each level the factor is 11–12× per step. Elsewhere it is mostly larger, up
  to 50–110× for most models at the realistic level. Chaos, rarely a threat, is
  the exception (appendix, tool §9).

## 2. Method

- **Noise: eight invented jacket models.** Nobody has a recording of what a
  real cuff does. The models are the six of
  [`jacket-report.md`](jacket-report.md) (random taps, one-sided rubbing, road
  bumps, squeezes, a pinned button plus taps, chaos), plus two new ones:
  - taps and holds: random taps, a quarter of them held 0.5–3 s (to test a
    hold);
  - a rocker: bursts across two neighbouring buttons (a row or a side).
- **Two levels, plus a band.**
  - **Harsh**: the models as written, an hour of sustained hostile noise.
  - **Realistic** (*calibrated* in the tool's output): each model slowed down
    until the stock watch, with no lock, opens about one activity an hour.
  - That point is weakly anchored: stock opens saturate as noise rises. So the
    candidates also run at **2× and 4×** the realistic rate.
- **The lock.** Each variant replays the same rides through the real
  `UnlockDetector`, with all rules on. Input is paced one event per 10 Hz frame
  through a 16-deep queue, as on the watch. There are 146 variants (every 3-step
  code, other 4-step shapes, holds, naive locks), at 10,000 h per model per
  level. Codes the settings refuse are measured too.
- **The consequence window.** After a false unlock, the watch face has the
  buttons for 2 min until the relock. The same noise drives a model of the stock
  menus and Run app, and of the patched apps.
  - Each window starts on the watch face with nothing open. For very weak locks,
    activities opened are therefore overstated (an earlier one would still be
    open), and recordings understated.
  - On the stock apps, an activity opened in that window stays open, GPS on,
    behind the relocked screen. Each one is a drain that lasts until the drain
    alert or the wearer catches it.
- **The person.** Two invented profiles, bare hand and motorcycle gloves. They
  cover tap length, gaps, hesitations, slips (wrong button or chord) and holds
  let go early. Each profile makes 20,000 attempts through the same detector.
  The times are indicative only.
- **Figures** are per 1000 h of noise, for the **worst of the eight models**,
  which is named in each cell. The study gives no pooled figures. Three models
  (one-sided rubbing, squeezes, rocker) can never produce a code on three
  different buttons, so averaging over models would dilute the risk.

## 3. Results

Model names: Taps (random taps), Bumps (road bumps), Pinned (pinned button plus
taps), Chaos, Taps+Holds, Rocker, Squeeze, OneSide. A `0 (<0.3)` means none
in 10,000 h: below 0.3 per 1000 h at 95 % confidence.

| Unlock | Settings | Presses | Gloves: time | Gloves: first try | Harsh: false unlocks | Harsh: activities opened | Realistic: false unlocks | Realistic: activities opened |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| UP SELECT | refused | 2 | 1.2 s | 92 % | 4535 Rocker | 2462 Rocker | 1552 Rocker | 410 Rocker |
| UP SELECT BACK | accepted | 3 | 2.1 s | 87 % | 245 Taps | 155 Bumps | 12.8 Bumps | 1.1 Bumps |
| **UP SELECT BACK DOWN (default)** | accepted | 4 | 2.9 s | 83 % | 20.4 Taps | 11.9 Bumps | 1.3 Bumps | 0.2 Bumps |
| UP SELECT BACK DOWN UP | accepted | 5 | 3.8 s | 78 % | 1.9 Taps | 0.6 Taps | 0.1 Bumps | 0.1 Bumps |
| UP SELECT BACK DOWN UP SELECT (first default) | accepted | 6 | 4.7 s | 74 % | 0.3 Taps | 0.1 Taps | 0 (<0.3) | 0 (<0.3) |
| UP SELECT UP | refused | 3 | 2.1 s | 87 % | 3675 Rocker | 1673 Rocker | 1062 Rocker | 198 Rocker |
| UP, then hold SELECT (1.0 s) | not offered | 2 (1 tap + hold) | 2.2 s | 89 % | 340 Taps+Holds | 143 Taps+Holds | 52.5 Taps+Holds | 1.8 Chaos |
| UP SELECT, then hold BACK (1.0 s) | not offered | 3 (2 taps + hold) | 3.1 s | 85 % | 24.0 Taps+Holds | 8.5 Taps+Holds | 1.0 Taps+Holds | 0.1 Taps+Holds |
| Naive 3-click UP SELECT BACK (no rules) | - | 3 | 2.1 s | 91 % | 11578 Chaos | 13152 Chaos | 1905 Squeeze | 70.5 Squeeze |
| No lock (stock watch) | - | 0 | - | - | - | 5182 Chaos | - | 1022 Chaos |

Gloves: median time from the first press, and the share of attempts that
succeed at the first try. Activities opened: by the noise in the 2-min windows
after false unlocks. For no lock, it is by the noise on the stock watch.

**The band.** Worst model, per 1000 h, at the realistic rate and at 2× and 4×
that rate:

| Unlock | False unlocks: realistic | 2× | 4× | Activities opened: realistic | 2× | 4× |
|---|---:|---:|---:|---:|---:|---:|
| No lock (stock watch) | - | - | - | 1022 | 2534 | 4004 |
| UP SELECT BACK (3) | 12.8 | 24.4 | 81.8 | 1.1 | 2.8 | 21.6 |
| **UP SELECT BACK DOWN (4, default)** | 1.3 | 1.4 | 4.2 | 0.2 | 0.2 | 1.4 |
| UP SELECT BACK DOWN UP (5) | 0.1 | 0.1 | 0.2 | 0.1 | 0.1 | 0 (<0.3) |
| UP SELECT BACK DOWN UP SELECT (6) | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) |

Doubling the noise only takes the stock watch from 1.0 to 2.5 activities an
hour, and quadrupling it to 4.0. Across the band, four steps stay at or below
4.2 per 1000 h. Three steps go from once per 80 h to once per 12 h.

## 4. What the numbers say

- **Length is what matters, once the rules are on.** The worst model at each
  level loses 11–12× per extra step: Taps at the harsh level (10.8, 11.1,
  11.5) and Bumps at the realistic level (12.4, 11.2, 12.4).
- **Which three buttons, and in which order, does not matter.**
  - Every model that can produce a code on three different buttons treats the
    four buttons alike, so the 24 such codes have the same expected rate.
  - Measured at the harsh level, they lie within a few percent of each other:
    SD 2.0–2.7 % of the mean, range 8–14 % (random taps: 232–250 per 1000 h).
  - At the realistic level the counts are small and the spread is larger (Bumps:
    11.5–15.6).
  - All variants share the same rides, so their counts are correlated. A test
    that assumes independent Poisson counts does not apply, which is why this is
    reported as a measured range.
  - UP SELECT BACK, the first three steps of the default, is as good as any.
- **Codes on two buttons fail where geometry matters.**
  - The rocker never opens a code on three buttons. It opens UP SELECT UP 3675
    times per 1000 h at the harsh level and 1062 at the realistic level.
  - One-sided rubbing opens UP DOWN UP 551 times per 1000 h at the harsh level.
  - No model rocks across a diagonal, so diagonal pairs such as UP BACK UP score
    like other 3-step codes here. The settings refuse every two-button code
    anyway: the edge of a real cuff could lie anywhere.
- **The hold is no better than one more tap.**
  - "UP, then hold SELECT" takes as long as three taps (2.2 s against 2.1 s
    with gloves). At the realistic level it opens 4× as often (52.5 against
    12.8).
  - "UP SELECT, then hold BACK" is about as safe as four taps (24.0 against
    20.4 harsh, 1.0 against 1.3 realistic), and a little slower (3.1 s against
    2.9 s).
  - A hold also needs an indicator on screen that says when it is long enough.
  - It is therefore not offered. `UnlockTiming::finalHoldMs` stays in the
    detector, for the study only.
- **What a false unlock costs.** With the default, the open watch's 2-min
  windows produce about 12 activities per 1000 h at the harsh level and 0.2 at
  the realistic level. With no lock, the same noise opens 5182 and 1022.
- **Rate sweep (random taps).** Slower noise makes fewer tries. Faster noise
  trips the quiet rule and the cool-down. So there is a worst rate, and it lies
  near one press every 4–6 s, which is about where the harsh random-taps model
  sits.
  - At that worst rate: 240, 24, 2.1 and 0.3 per 1000 h for 3, 4, 5 and 6 steps.
  - A reviewer's separate sweep, with its own code, gives about 250, 23, 3
    and 0.3.
- **The rules cost deaf time, the same for every length.** At the harsh level,
  the lock spends 59–100 % of the hour cooling down, so a person must first let
  go and wait 15 s. At the realistic level, it is 1–6 %, except under squeezes
  (41 %).

## 5. Independent cross-check

A separate program, `tests/seq_crosscheck.cpp`, written without reference to
`JacketModel.hpp` or `seq_study.cpp`, checked the arithmetic, with:

- its own RNG and noise generators;
- 2,000,000 h per model;
- the real `UnlockDetector` linked in;
- closed-form and Markov analytics alongside.

False unlocks per 1000 h, random taps / road bumps:

| Steps | 2 | 3 | 4 | 5 | 6 |
|---|---:|---:|---:|---:|---:|
| Cross-check | 2871 / 2290 | 245 / 200 | 21.0 / 17.1 | 1.8 / 1.4 | 0.14 / 0.10 |
| This study (family mean) | 2621 / 2141 | 243 / 196 | 21.9 / 16.2 | 1.9 / 0.9 | 0.3 / 0 (<0.3) |

- It agreed with this Monte Carlo within about 3 % where both have many
  counts. In this run, 3 and 4 steps agree within 1–5 %, inside the sampling
  noise. That validates the arithmetic, not the noise models.
- The 2-step figures here are about 9 % lower for a reason, not by chance. This
  study keeps the lock down for 2 min after every false unlock, and at 2.6 an
  hour that is 9 % of the time.
  - Corrected for that, they agree within 1 %.
  - `jacket_report`, which relocks at once, gives 2831 / 2302.
- The 5- and 6-step counts here are small (19 and 3 under random taps), and they
  agree within their intervals.

## 6. Caveats

- **The noise is invented.** The eight models are guesses at what a cuff might
  do, deliberately varied and mostly hostile. Trust these more than the
  absolute numbers:
  - the per-step factor (about 10–17×);
  - the ranking, from worst to best: 2 steps, codes on two buttons, 1 tap +
    hold, 3 steps, 2 taps + hold ≈ 4 steps, 5 steps, 6 steps.
- **"Realistic" is a calibration, not a measurement.** It assumes a cuff that
  would open about one activity an hour on a stock watch. The band shows how
  much rides on that.
- **The stock-watch model is approximate.** The launcher is closed source: menu
  timeouts and the time to a GPS fix are assumptions
  ([`JacketModel.hpp`](../tests/JacketModel.hpp)).
- **The consequence window** overstates activities opened and understates
  recordings for very weak locks (§2).
- **The person model is invented too.** The times and first-try rates rank the
  candidates. They do not predict a real ride.
- **How a real cuff could settle 3 against 4.** While locked, ButtonLock could
  count two things over a few rides:
  - the **attempts**: how often noise starts a try, meaning the code's first
    button after 0.6 s of quiet;
  - **how far each one gets into the code**: step 1, 2 or 3.

  The ratio of tries that reach step k + 1 to tries that reach step k is that
  cuff's own per-step factor. With the attempt rate, it predicts the 3- and
  4-step false-unlock rates from tens of hours of riding instead of thousands.
  If it predicts three steps would open less than about once in a few hundred
  hours of riding, three steps are enough for that jacket. If not, keep four.

## 7. How to reproduce

```bash
cmake -S tests -B build-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests -j"$(nproc)" --target seq_study
build-tests/seq_study 10000 10000 "$(nproc)" > seq-study.md   # defaults: 10000 h per model, seed 0x5e9

# the cross-check (§5): 2,000,000 h per model takes a few minutes per model
cmake --build build-tests -j"$(nproc)" --target seq_crosscheck
build-tests/seq_crosscheck taps 2000000 1 "$(nproc)"
build-tests/seq_crosscheck bumps 2000000 1 "$(nproc)"
```

- The run takes about 100 s on 4 cores in a Release build. A Debug build (what
  `tools/test.sh` makes) is about ten times slower.
- For a given seed and number of hours, the output is the same whatever the
  thread count. Only the header line changes.
- `seq_study` is a manual tool, not a ctest.
- The arguments are `seq_study [hostile-hours] [calibrated-hours] [threads]
  [seed]`. *Hostile* is the harsh level, and *calibrated* the realistic one.

The essential tables of that run follow, verbatim. The full output (1137 lines)
adds per-model tables for every variant, every 3-step code ranked, every hold
code, and the consequences per model.

## Appendix: tool output (essential tables)

Generated by `tests/seq_study.cpp`: `seq_study 10000 10000 4 0x5e9` (4 threads, 100 s).

Every lock variant runs on the same one-hour rides (common random numbers): 10000 h per jacket model at the harsh level, 10000 h per model at the calibrated level and at 2x and 4x its rate (candidate locks only), 8 models, 146 lock variants. All of ButtonLock's rules are on (quiet 0.6 s, press < 1.2 s, gap < 2.5 s, chords end the attempt, 3 rejections in 30 s -> cool-down until 15 s of silence, pinned > 4 s ignored). The lock sees the frame-paced stream (10 Hz, 16-deep queue). After a false unlock the open padlock shows for 0.7 s, then the watch face has the buttons for **2 min** (relockMs) and the same noise is fed to the stock-watch model and to the patched-apps model (5 min pre-activity budget, start = 1.5 s hold of SELECT alone); then the lock is back up (detector reset).

**The consequence window.** Each 2-min window starts on the watch face with nothing open. For a very weak lock, which opens again soon after it relocks, an activity opened in an earlier window would in fact still be open, so activities opened are overstated for such locks and recordings understated. On the stock apps an activity opened in a window stays open, GPS on, behind the relocked screen: each one is a drain that lasts until the drain alert or the wearer catches it.

Rates are **per 1000 h** of that noise. `r (lo-hi)` is the Poisson 95% interval; `0 (<u)` is a zero count with its 3/N upper bound. Buttons: UP top left, SELECT top right, DOWN bottom left, BACK bottom right. *Settings*: whether the app's settings accept the code (they refuse codes under 3 steps or on fewer than 3 different buttons; holds are not offered). *Worst model*: the jacket model with the highest count for that lock and column, named. No pooled (all-model) figures are given: three of the eight models (OneSide, Squeeze, Rocker) can never produce a code on three or more different buttons, so pooling would dilute the risk.

### 1. Noise levels

Calibration: for each model, the event rate (every interval between noise events divided by the scale) was bisected on 3000 h so the stock watch, with no lock, opens 1.0 activity per hour. The calibrated point is weakly anchored (stock opens saturate as noise rises), hence the 2x and 4x band. Measured in the main runs:

| Model | Harsh: presses/h | Harsh: stock opens/h | Calibrated: rate scale | Calibrated: presses/h | Calibrated: stock opens/h | x2: presses/h | x2: stock opens/h | x4: presses/h | x4: stock opens/h |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Random taps (1 per 6 s, any button) | 600 | 4.63 | 0.111 | 66 | 0.97 | 133 | 2.45 | 265 | 3.94 |
| One-sided rubbing (1 per 4 s) | 900 | 1.56 | 0.084 | 75 | 0.99 | 151 | 1.47 | 301 | 1.55 |
| Road bumps (bursts of 2-6 taps) | 853 | 2.16 | 0.029 | 28 | 0.98 | 55 | 1.52 | 110 | 2.02 |
| Squeezes (2-4 buttons at once) | 774 | 1.83 | 0.633 | 516 | 1.02 | 945 | 2.35 | 1616 | 3.82 |
| Pinned button + random taps | 619 | 3.74 | 0.120 | 81 | 0.99 | 156 | 2.30 | 303 | 3.43 |
| Chaos (taps/holds/chords, 1 per 1.5 s) | 2880 | 5.18 | 0.041 | 117 | 1.02 | 233 | 2.53 | 468 | 4.00 |
| Taps and holds (1 per 6 s, 25% held 0.5-3 s) | 600 | 4.47 | 0.152 | 91 | 1.01 | 183 | 2.49 | 366 | 4.00 |
| Rocker (bursts across 2 neighbouring buttons) | 853 | 1.83 | 0.082 | 77 | 0.97 | 154 | 1.31 | 302 | 1.63 |

No lock: recordings started per hour.

| Model | Harsh: stock | Harsh: patched apps | Calibrated: stock | Calibrated: patched apps | x2: stock | x4: stock |
|---|---:|---:|---:|---:|---:|---:|
| Random taps (1 per 6 s, any button) | 0.95 | 0 (<3.0e-04) | 0.15 | 0 (<3.0e-04) | 0.37 | 0.71 |
| One-sided rubbing (1 per 4 s) | 0.50 | 0.50 | 0.36 | 0.16 | 0.48 | 0.49 |
| Road bumps (bursts of 2-6 taps) | 1.00 | 0 (<3.0e-04) | 0.40 | 0 (<3.0e-04) | 0.63 | 0.86 |
| Squeezes (2-4 buttons at once) | 0.23 | 0 (<3.0e-04) | 0.14 | 0 (<3.0e-04) | 0.31 | 0.59 |
| Pinned button + random taps | 0.94 | 0.0002 | 0.16 | 0.0060 | 0.40 | 0.76 |
| Chaos (taps/holds/chords, 1 per 1.5 s) | 1.00 | 0.44 | 0.16 | 0.06 | 0.37 | 0.72 |
| Taps and holds (1 per 6 s, 25% held 0.5-3 s) | 0.90 | 0.51 | 0.15 | 0.07 | 0.38 | 0.73 |
| Rocker (bursts across 2 neighbouring buttons) | 0.35 | 0 (<3.0e-04) | 0.21 | 0 (<3.0e-04) | 0.26 | 0.31 |

### 3. Candidate locks, per level (worst model, with 95% intervals)

#### Harsh (10000 h per model)

| Lock | Settings | Presses | False unlocks | Stock: activities opened | Stock: recordings started | Patched apps: recordings started |
|---|---|---:|---:|---:|---:|---:|
| *No lock* | - | - | - | 5182 (5138-5227) Chaos | 1000 (980-1020) Bumps | 512 (498-526) Taps+Holds |
| UP SELECT BACK DOWN UP SELECT [first default] | accepted | 6 | 0.3 (0.062-0.88) Taps | 0.1 (0.0025-0.56) Taps | 0 (<0.3), every model | 0 (<0.3), every model |
| UP SELECT BACK DOWN UP | accepted | 5 | 1.90 (1.14-2.97) Taps | 0.6 (0.22-1.31) Taps | 0.2 (0.024-0.72) Bumps | 0 (<0.3), every model |
| UP SELECT BACK DOWN [default] | accepted | 4 | 20.4 (17.7-23.4) Taps | 11.9 (9.86-14.2) Bumps | 4.60 (3.37-6.14) Bumps | 0.1 (0.0025-0.56) Taps+Holds |
| UP BACK DOWN SELECT | accepted | 4 | 23.1 (20.2-26.3) Taps | 14.3 (12.1-16.8) Bumps | 5.70 (4.32-7.39) Bumps | 0.3 (0.062-0.88) Taps+Holds |
| UP SELECT BACK | accepted | 3 | 245 (236-255) Taps | 155 (148-163) Bumps | 64.6 (59.7-69.8) Bumps | 2.60 (1.70-3.81) Taps+Holds |
| UP BACK UP | refused | 3 | 253 (243-263) Taps | 149 (142-157) Bumps | 62.2 (57.4-67.3) Bumps | 1.40 (0.77-2.35) Taps+Holds |
| UP SELECT UP | refused | 3 | 3675 (3638-3713) Rocker | 1673 (1648-1698) Rocker | 566 (552-581) Rocker | 2.00 (1.22-3.09) Taps+Holds |
| UP SELECT | refused | 2 | 4535 (4493-4577) Rocker | 2462 (2432-2493) Rocker | 869 (851-888) Rocker | 34.4 (30.9-38.2) Taps+Holds |
| UP, then hold SELECT (0.8 s) | not offered | 2 (1 tap + hold) | 382 (369-394) Taps+Holds | 159 (151-167) Taps+Holds | 21.7 (18.9-24.8) Taps+Holds | 8.10 (6.43-10.1) Taps+Holds |
| UP, then hold SELECT (1.0 s) | not offered | 2 (1 tap + hold) | 340 (329-352) Taps+Holds | 143 (136-151) Taps+Holds | 19.3 (16.7-22.2) Taps+Holds | 7.80 (6.17-9.73) Taps+Holds |
| UP SELECT, then hold BACK (0.8 s) | not offered | 3 (2 taps + hold) | 26.2 (23.1-29.6) Taps+Holds | 8.90 (7.15-11.0) Taps+Holds | 0.6 (0.22-1.31) Taps+Holds | 0.3 (0.062-0.88) Taps+Holds |
| UP SELECT, then hold BACK (1.0 s) | not offered | 3 (2 taps + hold) | 24.0 (21.1-27.2) Taps+Holds | 8.50 (6.79-10.5) Taps+Holds | 0.5 (0.16-1.17) Taps+Holds | 0.3 (0.062-0.88) Taps+Holds |
| Naive 3-click UP SELECT BACK (no rules) | - | 3 | 11578 (11511-11645) Chaos | 13152 (13081-13223) Chaos | 2271 (2241-2300) Chaos | 220 (211-229) Chaos |
| Naive 4-click UP SELECT BACK DOWN (no rules) | - | 4 | 3963 (3924-4002) Chaos | 4536 (4495-4578) Chaos | 780 (762-797) Chaos | 76.4 (71.1-82.0) Chaos |

#### Calibrated (10000 h per model)

| Lock | Settings | Presses | False unlocks | Stock: activities opened | Stock: recordings started | Patched apps: recordings started |
|---|---|---:|---:|---:|---:|---:|
| *No lock* | - | - | - | 1022 (1002-1042) Chaos | 395 (383-408) Bumps | 162 (154-170) OneSide |
| UP SELECT BACK DOWN UP SELECT [first default] | accepted | 6 | 0 (<0.3), every model | 0 (<0.3), every model | 0 (<0.3), every model | 0 (<0.3), every model |
| UP SELECT BACK DOWN UP | accepted | 5 | 0.1 (0.0025-0.56) Bumps | 0.1 (0.0025-0.56) Bumps | 0 (<0.3), every model | 0 (<0.3), every model |
| UP SELECT BACK DOWN [default] | accepted | 4 | 1.30 (0.69-2.22) Bumps | 0.2 (0.024-0.72) Bumps | 0.1 (0.0025-0.56) Bumps | 0 (<0.3), every model |
| UP BACK DOWN SELECT | accepted | 4 | 1.90 (1.14-2.97) Bumps | 0 (<0.3), every model | 0 (<0.3), every model | 0 (<0.3), every model |
| UP SELECT BACK | accepted | 3 | 12.8 (10.7-15.2) Bumps | 1.10 (0.55-1.97) Bumps | 0.1 (0.0025-0.56) Bumps | 0 (<0.3), every model |
| UP BACK UP | refused | 3 | 15.0 (12.7-17.6) Bumps | 1.10 (0.55-1.97) Bumps | 0.2 (0.024-0.72) Bumps | 0 (<0.3), every model |
| UP SELECT UP | refused | 3 | 1062 (1042-1083) Rocker | 198 (189-206) Rocker | 23.8 (20.9-27.0) Rocker | 0 (<0.3), every model |
| UP SELECT | refused | 2 | 1552 (1527-1576) Rocker | 410 (398-423) Rocker | 58.2 (53.6-63.1) Rocker | 0.1 (0.0025-0.56) Chaos |
| UP, then hold SELECT (0.8 s) | not offered | 2 (1 tap + hold) | 58.3 (53.7-63.2) Taps+Holds | 2.20 (1.38-3.33) Chaos | 0.1 (0.0025-0.56) Taps+Holds | 0 (<0.3), every model |
| UP, then hold SELECT (1.0 s) | not offered | 2 (1 tap + hold) | 52.5 (48.1-57.2) Taps+Holds | 1.80 (1.07-2.84) Chaos | 0.1 (0.0025-0.56) Taps+Holds | 0 (<0.3), every model |
| UP SELECT, then hold BACK (0.8 s) | not offered | 3 (2 taps + hold) | 1.20 (0.62-2.10) Chaos | 0.1 (0.0025-0.56) Chaos | 0.1 (0.0025-0.56) Taps+Holds | 0 (<0.3), every model |
| UP SELECT, then hold BACK (1.0 s) | not offered | 3 (2 taps + hold) | 1.00 (0.48-1.84) Taps+Holds | 0.1 (0.0025-0.56) Taps+Holds | 0.1 (0.0025-0.56) Taps+Holds | 0 (<0.3), every model |
| Naive 3-click UP SELECT BACK (no rules) | - | 3 | 1905 (1878-1932) Squeeze | 70.5 (65.4-75.9) Squeeze | 2.00 (1.22-3.09) Squeeze | 1.30 (0.69-2.22) Chaos |
| Naive 4-click UP SELECT BACK DOWN (no rules) | - | 4 | 480 (467-494) Squeeze | 17.9 (15.4-20.7) Squeeze | 0.6 (0.22-1.31) Squeeze | 0.8 (0.35-1.58) Chaos |

### 4. Calibration band: the calibrated level, and 2x and 4x its noise rate

Worst model per cell. The stock watch's own opens (first row) grow far more slowly than the noise.

| Lock | Settings | False unlocks: calibrated | x2 | x4 | Activities opened: calibrated | x2 | x4 |
|---|---|---:|---:|---:|---:|---:|---:|
| *No lock: stock watch* | - | - | - | - | 1022 Chaos | 2534 Chaos | 4004 Chaos |
| UP SELECT BACK DOWN UP SELECT [first default] | accepted | 0 (<0.3), every model | 0 (<0.3), every model | 0 (<0.3), every model | 0 (<0.3), every model | 0 (<0.3), every model | 0 (<0.3), every model |
| UP SELECT BACK DOWN UP | accepted | 0.1 Bumps | 0.1 Taps | 0.2 Pinned | 0.1 Bumps | 0.1 Taps | 0 (<0.3), every model |
| UP SELECT BACK DOWN [default] | accepted | 1.30 Bumps | 1.40 Bumps | 4.20 Taps+Holds | 0.2 Bumps | 0.2 Taps+Holds | 1.40 Taps+Holds |
| UP BACK DOWN SELECT | accepted | 1.90 Bumps | 2.50 Bumps | 4.80 Taps | 0 (<0.3), every model | 0.4 Bumps | 1.30 Taps |
| UP SELECT BACK | accepted | 12.8 Bumps | 24.4 Bumps | 81.8 Taps | 1.10 Bumps | 2.80 Bumps | 21.6 Taps |
| UP BACK UP | refused | 15.0 Bumps | 26.9 Bumps | 87.2 Taps | 1.10 Bumps | 2.70 Bumps | 22.8 Taps |
| UP SELECT UP | refused | 1062 Rocker | 1739 Rocker | 2624 Rocker | 198 Rocker | 533 Rocker | 1095 Rocker |
| UP SELECT | refused | 1552 Rocker | 2413 Rocker | 3451 Rocker | 410 Rocker | 902 Rocker | 1665 Rocker |
| UP, then hold SELECT (0.8 s) | not offered | 58.3 Taps+Holds | 184 Taps+Holds | 395 Taps+Holds | 2.20 Chaos | 18.0 Taps+Holds | 101 Taps+Holds |
| UP, then hold SELECT (1.0 s) | not offered | 52.5 Taps+Holds | 168 Taps+Holds | 360 Taps+Holds | 1.80 Chaos | 15.9 Taps+Holds | 93.7 Taps+Holds |
| UP SELECT, then hold BACK (0.8 s) | not offered | 1.20 Chaos | 5.00 Taps+Holds | 16.8 Taps+Holds | 0.1 Chaos | 0.2 Taps+Holds | 4.20 Taps+Holds |
| UP SELECT, then hold BACK (1.0 s) | not offered | 1.00 Taps+Holds | 4.80 Taps+Holds | 14.9 Taps+Holds | 0.1 Taps+Holds | 0.2 Taps+Holds | 3.70 Taps+Holds |
| Naive 3-click UP SELECT BACK (no rules) | - | 1905 Squeeze | 3300 Squeeze | 5294 Squeeze | 70.5 Squeeze | 308 Squeeze | 1086 Squeeze |
| Naive 4-click UP SELECT BACK DOWN (no rules) | - | 480 Squeeze | 886 Squeeze | 1500 Squeeze | 17.9 Squeeze | 81.2 Squeeze | 303 Squeeze |

### 8. Every 3-step code (extract)

**The 24 codes on three different buttons (the 3-step codes the settings accept): measured range.** Every model that can produce them treats the four buttons alike, so they have the same expected rate. They are measured on the same rides, so their counts are correlated and no independent-Poisson test applies; this is the spread that was measured. SD: the standard deviation across the 24 codes, as a share of their mean.

| Level | Model | Lowest | Highest | Mean | SD | Highest/lowest - 1 |
|---|---|---:|---:|---:|---:|---:|
| Harsh | Taps | 232 | 250 | 243 | 2.0 % | 8 % |
| Harsh | OneSide | 0 (<0.3) for all 24 | | | | |
| Harsh | Bumps | 189 | 210 | 196 | 2.0 % | 11 % |
| Harsh | Squeeze | 0 (<0.3) for all 24 | | | | |
| Harsh | Pinned | 134 | 153 | 144 | 2.7 % | 14 % |
| Harsh | Chaos | 2.30 | 4.80 | 3.40 | 19.2 % | 109 % |
| Harsh | Taps+Holds | 116 | 130 | 123 | 2.6 % | 11 % |
| Harsh | Rocker | 0 (<0.3) for all 24 | | | | |
| Calibrated | Taps | 1.30 | 2.90 | 2.28 | 18.7 % | 123 % |
| Calibrated | OneSide | 0 (<0.3) for all 24 | | | | |
| Calibrated | Bumps | 11.5 | 15.6 | 13.8 | 7.8 % | 36 % |
| Calibrated | Squeeze | 0 (<0.3) for all 24 | | | | |
| Calibrated | Pinned | 1.70 | 3.60 | 2.46 | 19.0 % | 112 % |
| Calibrated | Chaos | 1.30 | 2.90 | 2.10 | 16.1 % | 123 % |
| Calibrated | Taps+Holds | 2.10 | 4.30 | 2.96 | 15.1 % | 105 % |
| Calibrated | Rocker | 0 (<0.3) for all 24 | | | | |

### 9. By family: mean false unlocks per code, per 1000 h

A family's codes are alike to every model that treats the four buttons alike (all but OneSide and Rocker), so its mean is a steadier figure than any one code's. A zero is shown with one code's 3/N bound (the codes share rides).

#### Harsh

| Family | H | Codes | Worst code on its worst model | Taps | OneSide | Bumps | Squeeze | Pinned | Chaos | Taps+Holds | Rocker |
|---|---:|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 2 taps | - | 3 | 4593 UP DOWN, Rocker | 2621 | 836 | 2141 | 0 (<0.3) | 2323 | 29.4 | 1646 | 3043 |
| 3 taps, three buttons | - | 24 | 250 DOWN UP SELECT, Taps | 243 | 0 (<0.3) | 196 | 0 (<0.3) | 144 | 3.40 | 123 | 0 (<0.3) |
| 3 taps, two buttons | - | 12 | 3748 BACK SELECT BACK, Rocker | 243 | 182 | 197 | 0 (<0.3) | 238 | 3.83 | 122 | 2476 |
| 4 taps, four buttons | - | 5 | 23.1 UP BACK DOWN SELECT, Taps | 21.9 | 0 (<0.3) | 16.2 | 0 (<0.3) | 4.24 | 0.32 | 8.64 | 0 (<0.3) |
| 4 taps, two buttons | - | 2 | 2777 UP SELECT UP SELECT, Rocker | 21.6 | 0 (<0.3) | 18.0 | 0 (<0.3) | 21.6 | 0.3 | 9.65 | 1388 |
| 5 taps | - | 1 | 1.90 UP SELECT BACK DOWN UP, Taps | 1.90 | 0 (<0.3) | 0.9 | 0 (<0.3) | 0.7 | 0.1 | 0.4 | 0 (<0.3) |
| 6 taps | - | 1 | 0.3 UP SELECT BACK DOWN UP SELECT [first default], Taps | 0.3 | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) |
| 1 tap + hold | 0.8 s | 12 | 446 DOWN, then hold UP (0.8 s), OneSide | 0 (<0.3) | 147 | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 3.04 | 378 | 0 (<0.3) |
| 1 tap + hold | 1.0 s | 12 | 400 DOWN, then hold UP (1.0 s), OneSide | 0 (<0.3) | 132 | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 2.57 | 340 | 0 (<0.3) |
| 2 taps + hold, three buttons | 0.8 s | 24 | 29.6 UP DOWN, then hold SELECT (0.8 s), Taps+Holds | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0.29 | 27.0 | 0 (<0.3) |
| 2 taps + hold, three buttons | 1.0 s | 24 | 26.6 UP DOWN, then hold SELECT (1.0 s), Taps+Holds | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0.27 | 24.2 | 0 (<0.3) |
| 2 taps + hold, two buttons | 0.8 s | 12 | 96.7 UP DOWN, then hold UP (0.8 s), OneSide | 0 (<0.3) | 31.1 | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0.29 | 26.9 | 0 (<0.3) |
| 2 taps + hold, two buttons | 1.0 s | 12 | 87.4 UP DOWN, then hold UP (1.0 s), OneSide | 0 (<0.3) | 28.2 | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0.26 | 24.1 | 0 (<0.3) |

#### Calibrated

| Family | H | Codes | Worst code on its worst model | Taps | OneSide | Bumps | Squeeze | Pinned | Chaos | Taps+Holds | Rocker |
|---|---:|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 2 taps | - | 3 | 1552 UP SELECT, Rocker | 190 | 113 | 172 | 0 (<0.3) | 190 | 174 | 231 | 1025 |
| 3 taps, three buttons | - | 24 | 15.6 BACK SELECT UP, Bumps | 2.28 | 0 (<0.3) | 13.8 | 0 (<0.3) | 2.46 | 2.10 | 2.96 | 0 (<0.3) |
| 3 taps, two buttons | - | 12 | 1064 SELECT UP SELECT, Rocker | 2.42 | 2.88 | 14.5 | 0 (<0.3) | 2.42 | 2.17 | 2.99 | 685 |
| 4 taps, four buttons | - | 5 | 1.90 UP BACK DOWN SELECT, Bumps | 0.02 | 0 (<0.3) | 1.24 | 0 (<0.3) | 0.04 | 0.04 | 0 (<0.3) | 0 (<0.3) |
| 4 taps, two buttons | - | 2 | 675 UP SELECT UP SELECT, Rocker | 0.05 | 0 (<0.3) | 0.7 | 0 (<0.3) | 0.15 | 0 (<0.3) | 0 (<0.3) | 338 |
| 5 taps | - | 1 | 0.1 UP SELECT BACK DOWN UP, Bumps | 0 (<0.3) | 0 (<0.3) | 0.1 | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) |
| 6 taps | - | 1 | 0 (<0.3), every code | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) |
| 1 tap + hold | 0.8 s | 12 | 76.0 DOWN, then hold UP (0.8 s), OneSide | 0 (<0.3) | 24.7 | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 49.5 | 60.6 | 0 (<0.3) |
| 1 tap + hold | 1.0 s | 12 | 67.6 DOWN, then hold UP (1.0 s), OneSide | 0 (<0.3) | 22.3 | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 44.3 | 55.0 | 0 (<0.3) |
| 2 taps + hold, three buttons | 0.8 s | 24 | 1.20 UP SELECT, then hold BACK (0.8 s), Chaos | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0.6 | 0.77 | 0 (<0.3) |
| 2 taps + hold, three buttons | 1.0 s | 24 | 1.00 UP SELECT, then hold BACK (1.0 s), Taps+Holds | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0.52 | 0.7 | 0 (<0.3) |
| 2 taps + hold, two buttons | 0.8 s | 12 | 1.70 SELECT BACK, then hold SELECT (0.8 s), OneSide | 0 (<0.3) | 0.48 | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0.59 | 0.82 | 0 (<0.3) |
| 2 taps + hold, two buttons | 1.0 s | 12 | 1.60 SELECT BACK, then hold SELECT (1.0 s), OneSide | 0 (<0.3) | 0.45 | 0 (<0.3) | 0 (<0.3) | 0 (<0.3) | 0.49 | 0.78 | 0 (<0.3) |

**Per-step factor**: the family mean at n taps divided by the mean at n + 1 taps (2 taps; 3 taps on three buttons; 4 taps on four buttons; 5; 6). `>x` when the longer one had no false unlock (against its 3/N bound) and that bound says something; `-` otherwise. Models that never open a code on three buttons at that level are left out.

| Level | Model | 2 -> 3 | 3 -> 4 | 4 -> 5 | 5 -> 6 |
|---|---|---:|---:|---:|---:|
| Harsh | Taps | 10.8 | 11.1 | 11.5 | 6.3 |
| Harsh | Bumps | 10.9 | 12.1 | 18.0 | >3.0 |
| Harsh | Pinned | 16.1 | 34.0 | 6.1 | >2.3 |
| Harsh | Chaos | 8.6 | 10.6 | 3.2 | - |
| Harsh | Taps+Holds | 13.4 | 14.2 | 21.6 | >1.3 |
| Calibrated | Taps | 83.4 | 114.0 | - | - |
| Calibrated | Bumps | 12.4 | 11.2 | 12.4 | - |
| Calibrated | Pinned | 77.3 | 61.6 | - | - |
| Calibrated | Chaos | 82.7 | 52.6 | - | - |
| Calibrated | Taps+Holds | 77.9 | >9.9 | - | - |

### 11. Rate sweep: random taps (any button), 10000 h per point

The harsh level is one tap every 6 s on average (scale 1). False unlocks per 1000 h.

| Mean gap between taps | Scale | Presses/h | Stock opens/h | UP SELECT BACK | UP SELECT BACK DOWN [default] | UP SELECT BACK DOWN UP | UP SELECT BACK DOWN UP SELECT [first default] |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 24.0 s | 0.25 | 150 | 2.76 | 22.4 | 0.2 | 0 (<0.3) | 0 (<0.3) |
| 12.0 s | 0.50 | 300 | 4.09 | 107 | 5.40 | 0.4 | 0 (<0.3) |
| 8.0 s | 0.75 | 450 | 4.45 | 205 | 13.7 | 1.10 | 0.1 |
| 6.0 s | 1.00 | 600 | 4.62 | 240 | 23.6 | 1.80 | 0.2 |
| 4.0 s | 1.50 | 900 | 4.84 | 190 | 24.4 | 2.10 | 0.3 |
| 3.0 s | 2.00 | 1200 | 5.15 | 102 | 14.5 | 1.70 | 0.3 |
| 2.0 s | 3.00 | 1801 | 5.30 | 29.3 | 5.80 | 0.9 | 0.2 |
| 1.5 s | 4.00 | 2400 | 5.51 | 17.4 | 2.80 | 0.5 | 0 (<0.3) |

### 12. Share of time the lock is cooling down (a person is told to wait)

#### Harsh

| Lock | Taps | OneSide | Bumps | Squeeze | Pinned | Chaos | Taps+Holds | Rocker |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| UP SELECT BACK DOWN UP SELECT [first default] | 73.0 % | 91.5 % | 59.3 % | 60.4 % | 60.7 % | 99.8 % | 74.3 % | 58.7 % |
| UP SELECT BACK DOWN UP | 73.0 % | 91.5 % | 59.3 % | 60.4 % | 60.7 % | 99.8 % | 74.3 % | 58.7 % |
| UP SELECT BACK DOWN [default] | 73.0 % | 91.5 % | 59.3 % | 60.4 % | 60.7 % | 99.8 % | 74.3 % | 58.7 % |
| UP SELECT BACK | 72.4 % | 91.5 % | 58.9 % | 60.4 % | 60.4 % | 99.8 % | 74.0 % | 58.7 % |
| UP, then hold SELECT (1.0 s) | 73.6 % | 91.5 % | 59.5 % | 60.4 % | 61.3 % | 99.8 % | 73.7 % | 59.4 % |
| UP SELECT, then hold BACK (1.0 s) | 73.1 % | 91.5 % | 59.3 % | 60.4 % | 60.7 % | 99.8 % | 74.3 % | 58.7 % |

#### Calibrated

| Lock | Taps | OneSide | Bumps | Squeeze | Pinned | Chaos | Taps+Holds | Rocker |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| UP SELECT BACK DOWN UP SELECT [first default] | 1.2 % | 1.9 % | 2.3 % | 41.2 % | 1.3 % | 4.0 % | 2.9 % | 6.2 % |
| UP SELECT BACK DOWN UP | 1.2 % | 1.9 % | 2.3 % | 41.2 % | 1.3 % | 4.0 % | 2.9 % | 6.2 % |
| UP SELECT BACK DOWN [default] | 1.2 % | 1.9 % | 2.3 % | 41.2 % | 1.3 % | 4.0 % | 2.9 % | 6.2 % |
| UP SELECT BACK | 1.2 % | 1.9 % | 2.3 % | 41.2 % | 1.3 % | 4.0 % | 2.9 % | 6.2 % |
| UP, then hold SELECT (1.0 s) | 1.2 % | 1.9 % | 2.3 % | 41.2 % | 1.3 % | 4.0 % | 2.9 % | 6.4 % |
| UP SELECT, then hold BACK (1.0 s) | 1.2 % | 1.9 % | 2.3 % | 41.2 % | 1.3 % | 4.0 % | 2.9 % | 6.2 % |

### 13. Cost to a person

Formula: 0.6 s quiet + 0.5 s per tap (gloves) + the hold and ~0.3 s to let go. Simulation: 20000 attempts per lock and profile through the same detector and frame pacing, starting from a quiet watch, each with its own random stream. A person retries after a rejection (1.5 s to read it), restarts after a "too slow" timeout, and waits out a cool-down. The profiles are invented, not measured.

- **bare hand**: tap 130 ms median (sigma 0.35), gap 250 ms median (sigma 0.45), 1% of gaps get an extra 1.5-4 s pause, 0.5% of presses slip (half another button, half a chord), 1% of holds let go early, let go 300 ms after the hold indicator completes.
- **motorcycle gloves**: tap 250 ms median (sigma 0.40), gap 350 ms median (sigma 0.50), 3% of gaps get an extra 1.5-4 s pause, 3.0% of presses slip (half another button, half a chord), 3% of holds let go early, let go 350 ms after the hold indicator completes.

| Lock | Presses | Formula (gloves) | Profile | First try succeeds | Median time | 95th pct time | Mean presses used | Hit a cool-down | Not unlocked in 2 min |
|---|---:|---:|---|---:|---:|---:|---:|---:|---:|
| UP SELECT BACK DOWN UP SELECT [first default] | 6 | 3.6 s | bare hand | 93.2 % | 3.1 s | 7.0 s | 6.23 | 0 (<0.015 %) | 0 (<0.015 %) |
| UP SELECT BACK DOWN UP SELECT [first default] | 6 | 3.6 s | motorcycle gloves | 74.3 % | 4.7 s | 13.3 s | 7.09 | 0.54 % | 0 (<0.015 %) |
| UP SELECT BACK DOWN UP | 5 | 3.1 s | bare hand | 94.8 % | 2.6 s | 4.7 s | 5.15 | 0.01 % | 0 (<0.015 %) |
| UP SELECT BACK DOWN UP | 5 | 3.1 s | motorcycle gloves | 78.2 % | 3.8 s | 10.6 s | 5.76 | 0.29 % | 0 (<0.015 %) |
| UP SELECT BACK DOWN [default] | 4 | 2.6 s | bare hand | 96.1 % | 2.0 s | 2.9 s | 4.09 | 0 (<0.015 %) | 0 (<0.015 %) |
| UP SELECT BACK DOWN [default] | 4 | 2.6 s | motorcycle gloves | 82.7 % | 2.9 s | 8.4 s | 4.48 | 0.15 % | 0 (<0.015 %) |
| UP SELECT BACK | 3 | 2.1 s | bare hand | 97.1 % | 1.4 s | 2.0 s | 3.05 | 0 (<0.015 %) | 0 (<0.015 %) |
| UP SELECT BACK | 3 | 2.1 s | motorcycle gloves | 87.2 % | 2.1 s | 6.4 s | 3.27 | 0.05 % | 0 (<0.015 %) |
| UP SELECT UP | 3 | 2.1 s | bare hand | 97.0 % | 1.4 s | 2.0 s | 3.06 | 0 (<0.015 %) | 0 (<0.015 %) |
| UP SELECT UP | 3 | 2.1 s | motorcycle gloves | 87.3 % | 2.1 s | 6.3 s | 3.27 | 0.07 % | 0 (<0.015 %) |
| UP SELECT | 2 | 1.6 s | bare hand | 98.3 % | 0.8 s | 1.2 s | 2.02 | 0.01 % | 0 (<0.015 %) |
| UP SELECT | 2 | 1.6 s | motorcycle gloves | 91.7 % | 1.2 s | 3.8 s | 2.12 | 0.03 % | 0 (<0.015 %) |
| UP, then hold SELECT (0.8 s) | 2 (1 tap + hold) | 2.2 s | bare hand | 97.3 % | 1.7 s | 2.2 s | 2.04 | 0 (<0.015 %) | 0 (<0.015 %) |
| UP, then hold SELECT (0.8 s) | 2 (1 tap + hold) | 2.2 s | motorcycle gloves | 88.9 % | 2.0 s | 5.3 s | 2.19 | 0.12 % | 0 (<0.015 %) |
| UP, then hold SELECT (1.0 s) | 2 (1 tap + hold) | 2.4 s | bare hand | 97.4 % | 1.9 s | 2.4 s | 2.04 | 0.01 % | 0 (<0.015 %) |
| UP, then hold SELECT (1.0 s) | 2 (1 tap + hold) | 2.4 s | motorcycle gloves | 89.0 % | 2.2 s | 5.5 s | 2.19 | 0.04 % | 0 (<0.015 %) |
| UP SELECT, then hold BACK (0.8 s) | 3 (2 taps + hold) | 2.7 s | bare hand | 96.1 % | 2.3 s | 3.0 s | 3.08 | 0 (<0.015 %) | 0 (<0.015 %) |
| UP SELECT, then hold BACK (0.8 s) | 3 (2 taps + hold) | 2.7 s | motorcycle gloves | 84.4 % | 2.9 s | 7.6 s | 3.37 | 0.14 % | 0 (<0.015 %) |
| UP SELECT, then hold BACK (1.0 s) | 3 (2 taps + hold) | 2.9 s | bare hand | 96.3 % | 2.5 s | 3.2 s | 3.08 | 0 (<0.015 %) | 0 (<0.015 %) |
| UP SELECT, then hold BACK (1.0 s) | 3 (2 taps + hold) | 2.9 s | motorcycle gloves | 84.5 % | 3.1 s | 7.8 s | 3.37 | 0.17 % | 0 (<0.015 %) |
| Naive 3-click UP SELECT BACK (no rules) | 3 | 2.1 s | bare hand | 98.6 % | 1.4 s | 1.9 s | 3.03 | 0 (<0.015 %) | 0 (<0.015 %) |
| Naive 3-click UP SELECT BACK (no rules) | 3 | 2.1 s | motorcycle gloves | 91.0 % | 2.1 s | 5.3 s | 3.19 | 0 (<0.015 %) | 0 (<0.015 %) |

Times run from the first press to the unlock; add up to 0.6 s if a button was touched just before (the quiet rule). Ways the rules reject a careful person: a pause over 2.5 s between steps (timeout), a press over 1.2 s (gloves), a second button brushed while pressing (chord), starting within 0.6 s of touching anything (not quiet), and with a hold: letting go before H, or holding past 4 s. Three of these within 30 s start a cool-down: 15 s of not touching anything. A hold also needs an indicator on screen that says when it is long enough.

### 14. Model notes

- The harsh level is the models as written; the calibrated level divides every interval between noise events by the rate scale in §1 (press lengths and taps within a burst unchanged); x2 and x4 multiply that scale.
- Each 2-min consequence window starts on the watch face with nothing open, so activities opened are overstated for very weak locks (an earlier one would still be open) and recordings understated. On the stock apps an activity opened in a window stays open, GPS on, behind the relocked screen; only its opening is counted here, not its GPS time.
- Consequence intervals are Poisson intervals on the count, slightly narrow because opens cluster in windows.
- Every lock variant sees the same rides, so differences between locks are not blurred by different noise, but their counts are correlated.
- `Taps+Holds` and `Rocker` are new in this study; the other six are the models of docs/jacket-report.md. All eight are invented; nobody has recorded what a cuff does.
