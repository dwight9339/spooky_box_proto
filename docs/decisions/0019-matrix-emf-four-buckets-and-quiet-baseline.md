# 0019. Four matrix EMF buckets, a quiet-field baseline and no trail by default

- **Status:** Accepted 2026-10-03
- **Date:** 2026-10-03
- **Supersedes:** [0013](0013-matrix-emf-radio-and-status-mapping.md) items 3 and 14,
  the trail default in item 2, and the baseline behaviour quoted in its Context. The
  rest of 0013 stands.
- **Beads:** `full_spooky_proto-54w.8`

## Context

[Decision 0013](0013-matrix-emf-radio-and-status-mapping.md) item 3 set five EMF
buckets (indigo, cyan, green, amber and red, with bounds at 32, 128, 512 and 2,048 µT)
as starting values to be fixed by bench trials. Its Context quoted the firmware's
baseline, which moves 1/16 of the way to the measured field every 5 s, whatever that
field is.

The 2026-10-03 bench trials ran the matrix from the M7 with a magnet and a phone near
the TMAG5273 ([evidence](../evidence/2026-10-03-matrix-feedback-on-m7.md)):

- **Indigo** looked pinkish magenta on the IS31FL3741, and **green** (40, 225, 95)
  looked pale.
- With the magnet away, the resting reading ranged up to about 76 µT and once reached
  146 µT. Under 0013's bounds it flickered between buckets 0 and 1, and later, with a
  first bound of 80 µT, between cyan and green.
- The field of a magnet falls off steeply with distance, so the 0013 buckets crossed
  green and amber within a second or two of a slow approach.
- After the magnet was held close for a while, the matrix stayed green for minutes once
  the magnet was taken away. The baseline had followed the magnet: holding about
  5,000 µT for 30 s raises it by roughly 1,700 µT, and at 1/16 every 5 s it takes about
  three minutes to fall back under 150 µT.
- With a top bound of 4,000 µT, a phone held against the sensor only reached amber. With
  the 1,200 µT bound below, a phone held very close with its screen on reached red.

The user tried four buckets with cyan as the lowest, chose the bounds below, and judged
them a good starting point for field testing. 0013 item 2 kept the trail on by default
until the user picked; after comparing both forms on the matrix, the user chose to run
without it.

Principles involved: II (the EMF reading keeps one measured meaning; a magnet that has
gone must not be shown as present), IV (bounds and colours were set from a bench trial
and stay configurable), V (the trial is hardware evidence of the colours on the
IS31FL3741, not of field behaviour).

## Options

1. **Keep five buckets and retune the drive values.** Rejected by the user after the
   trial: the extra bucket made each band of distance too narrow to see.
2. **Keep the always-adapting baseline and make it faster to recover.** Rejected: any
   rate that recovers quickly also follows a held magnet quickly, so the reading of a
   steady disturbance would fade on its own.
3. **Chosen: four buckets, and a baseline that adapts only while the field is quiet.**

## Decision

1. The EMF metric is bucketed by its change from baseline:

   | Bucket | Change from baseline | Outline colour (drive value) | Step period |
   |---|---|---|---|
   | 0 | below 150 µT | Cyan (0, 175, 230) | 160 ms |
   | 1 | 150 to 400 µT | Green (0, 255, 0) | 125 ms |
   | 2 | 400 to 1,200 µT | Amber (255, 165, 0) | 95 ms |
   | 3 | 1,200 µT and above | Red (255, 30, 20) | 70 ms |

   Bounds, colours and periods are starting values, configurable until field testing
   fixes them. The colours are IS31FL3741 drive values judged on the bench. 0013 items
   4 and 5 apply unchanged: the bucket is sampled at step 0, and an unknown reading is
   dim grey at the bucket 0 period.
2. On the matrix, red in the outline means bucket 3; only the border blinks red. The
   rest of 0013 item 14's colour vocabulary is unchanged.
3. The baseline adapts only while the field is quiet: the 5 s update moves it 1/16 of
   the way to the measured field only when the current change from baseline is below
   100 µT. A larger change leaves it where it is. The boot sample still seeds it, and
   `EMF ZERO` still resets it to the current field.
4. The trail is off by default: the loop has six steps. `UI MATRIX TRAIL ON` still
   turns it on (0013 item 2).

## Consequences

- `EmfLevel_DefaultConfig` sets the bounds to 150, 400 and 1,200 µT, and
  `MatrixFeedback_DefaultConfig` sets the four colours and periods.
  `MagnetometerTest_Service` skips the baseline update while the change from baseline
  is 100 µT or more.
- A magnet held near the sensor no longer moves the baseline. It returns to cyan as
  soon as the magnet is taken away.
- A lasting change in the surroundings of 100 µT or more, such as the device resting
  next to steel, stays shown until `EMF ZERO`. Turning the device does not count, since
  the metric is the field's magnitude. `EMF ZERO` is rejected while recording
  ([0008](0008-recording-safe-command-policy.md)), so such an offset lasts for the rest
  of a session.
- The matrix service starts with the trail off.
- `docs/design/usb-cli.md` describes the quiet-field baseline and the trail default.
- Revisit after field testing if the resting reading crosses 150 µT, if the bands feel
  wrong away from the bench, or if a lasting offset becomes a nuisance. That last case
  could be met with a slow return to adapting after a long steady disturbance.

## Evidence

[Matrix feedback on the M7, 2026-10-03](../evidence/2026-10-03-matrix-feedback-on-m7.md),
colour and bound trials with a magnet and a phone.
