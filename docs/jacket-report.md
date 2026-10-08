Jacket model: 1000 one-hour rides per model, seed 0xc0ffee

| Jacket model | Stock: activities opened /h | Stock: recordings started /h | Stock: GPS on | Budget patch: GPS on | Budget + hold-to-start: recordings /h | Budget + hold-to-start: GPS on | RideLock: false unlocks |
|---|---:|---:|---:|---:|---:|---:|---:|
| Random taps (1 per 6 s, any button) | 4.8 | 0.96 | 72 % | 72 % | 0.00 | 14 % | 0 in 1000 h |
| One-sided rubbing (1 per 4 s) | 1.4 | 0.49 | 48 % | 48 % | 0.49 | 44 % | 0 in 1000 h |
| Road bumps (bursts of 2-6 taps) | 2.2 | 1.00 | 93 % | 93 % | 0.00 | 21 % | 0 in 1000 h |
| Squeezes (2-4 buttons at once) | 1.8 | 0.25 | 16 % | 16 % | 0.00 | 4 % | 0 in 1000 h |
| Pinned button + random taps | 3.7 | 0.94 | 73 % | 72 % | 0.00 | 17 % | 0 in 1000 h |
| Chaos (taps/holds/chords, 1 per 1.5 s) | 5.3 | 1.00 | 87 % | 87 % | 0.44 | 34 % | 0 in 1000 h |

False unlocks in 1000 hours, per lock variant (ablation):

| Lock variant | Random taps (1 per 6 s, any button) | One-sided rubbing (1 per 4 s) | Road bumps (bursts of 2-6 taps) | Squeezes (2-4 buttons at once) | Pinned button + random taps | Chaos (taps/holds/chords, 1 per 1.5 s) |
|---|---:|---:|---:|---:|---:|---:|
| RideLock default (L1 R1 R2 L2 L1 R1) | 0 | 0 | 0 | 0 | 0 | 0 |
|   without the cool-down | 1 | 0 | 1 | 0 | 0 | 2 |
|   without the quiet rule | 0 | 0 | 0 | 0 | 0 | 0 |
|   without the chord rule | 0 | 0 | 0 | 0 | 0 | 0 |
|   5 steps (L1 R1 R2 L2 L1) | 1 | 0 | 3 | 0 | 0 | 0 |
|   4 steps (L1 R1 R2 L2) | 19 | 0 | 15 | 0 | 3 | 0 |
|     4 steps without the cool-down | 78 | 0 | 41 | 0 | 13 | 192 |
|     4 steps without the quiet rule | 23 | 0 | 32 | 0 | 6 | 0 |
|     4 steps without the chord rule | 20 | 0 | 15 | 0 | 5 | 2 |
|   2 steps (L1 R1) | 2831 | 0 | 2302 | 0 | 2629 | 21 |
| Naive 6-click lock (no rules) | 127 | 0 | 7 | 43 | 14 | 266 |
| Naive 4-click lock (no rules) | 1762 | 0 | 301 | 756 | 399 | 4548 |
| Naive 2-click lock (no rules) | 28842 | 0 | 17860 | 12866 | 20035 | 79159 |
