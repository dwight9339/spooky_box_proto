# 0013. LED matrix mapping for EMF, radio onsets and recording status

- **Status:** Accepted 2026-10-01; items 3 and 14, the trail default in item 2 and the
  baseline behaviour in the Context superseded by
  [0019](0019-matrix-emf-four-buckets-and-quiet-baseline.md)
- **Date:** 2026-10-01
- **Supersedes:** none
- **Beads:** `full_spooky_proto-54w.8` (this decision and its implementation);
  consumer `full_spooky_proto-p04.3` (Field slice renderer on the M7, Demo preset)

## Context

Decision 0011 item 8 requires the EMF and radio-activity visual mapping, control map
open decision D-016, to be settled in its own record before the matrix work starts. It
is the first demo blocker (checkpoint 2026-10-07).

Facts from the hardware and code:

- The matrix is an IS31FL3741 13×9 RGB breakout used as a centred 9×9 logical
  viewport (`UI_RENDER_MATRIX_WIDTH` and `_HEIGHT`, physical columns 2..10).
- The magnetometer is a TMAG5273. On the A2 variant one code is about 4 µT. The
  firmware's EMF metric is `abs(|B| - baseline)` in integer µT, with a baseline that
  adapts every 5 s by 1/16 of the difference (`docs/design/usb-cli.md`).
- Radio audio arrives in 512-frame half-buffers, about 10.7 ms each at 48 kHz.
- No radio-activity metric exists in the firmware yet.

The user's early prototype drew a double-stroke square that grew from a centre dot past
the edge of the matrix and looped. It used five EMF buckets of increasing size, each
with its own outline colour and a slightly faster animation.

The mapping was developed with the user on 2026-09-30 and 2026-10-01 against an
interactive preview ([D-016 Matrix Mapping](https://claude.ai/artifact/RJYJdVM7wFrQ2QANfwGVQK),
version 6). The user's `EMF STREAM` experiments set the first bucket boundary.

Principles involved: I (rendering never delays capture; a failed recording is
visible), II (EMF and radio activity keep one measured meaning; stale data is shown
honestly), IV (timings stay configurable until bench trials fix them), V (the preview
and host tests are not hardware evidence), VII (the application publishes semantic
facts; the renderer decides pixels).

## Options

1. **Sustained radio-activity level shearing the rows.** Rejected by the user after
   preview. A constant offset while the audio stays busy read as a static distortion,
   not as a response to something happening.
2. **Kicked pixels greying the recording border.** Tried and replaced. Blinking the
   whole border reads more clearly.
3. **Wrapping rows at the matrix edge.** Rejected by the user in favour of clipping.
4. **Chosen: the expanding square, with EMF colour and speed, radio onset kicks and a
   status border, as below.**

## Decision

**Animation**

1. The animation is a loop of steps around the centre pixel (4,4). Step 0 lights the
   centre. At step *s* the outline covers Chebyshev radii *s* and *s*−1 at full
   brightness: a stroke 2 pixels wide.
2. With the trail, a 1-pixel outline follows at radius *s*−2 at 35% brightness and the
   loop has seven steps. Without it the loop has six. Both forms are implemented
   behind a runtime setting so the user can choose on the hardware. The trail is on by
   default until the user picks.

**EMF**

3. The EMF metric is bucketed by its change from baseline:

   | Bucket | Change from baseline | Outline colour | Step period |
   |---|---|---|---|
   | 0 | below 32 µT | Indigo (70, 50, 220) | 160 ms |
   | 1 | 32 to 128 µT | Cyan (0, 175, 230) | 130 ms |
   | 2 | 128 to 512 µT | Green (40, 225, 95) | 105 ms |
   | 3 | 512 to 2,048 µT | Amber (255, 165, 0) | 85 ms |
   | 4 | 2,048 µT and above | Red (255, 30, 20) | 70 ms |

   Boundaries, colours and periods are starting values, configurable until bench
   trials fix them. Colours are the intended appearance; the drive values for the
   IS31FL3741 are tuned on the bench.
4. The bucket is sampled once per loop, at step 0, so the colour and speed never change
   during a sweep.
5. When no EMF sample has arrived for 500 ms, the magnetometer has faulted, or the
   baseline is not yet valid, the outline is dim grey at the bucket 0 period. The
   matrix never shows an EMF colour it did not measure.

**Radio onsets**

6. Radio onsets are measured from the radio audio, never from RSSI or SNR. For each
   radio half-buffer the M7 computes the mean absolute sample level and keeps a fast
   average (time constant about 30 ms) and a slow average (about 1 s).
7. An onset fires when the fast average rises above the slow one by 1.5× (small),
   2.5× (medium) or 4× (large); its size is the highest ratio crossed. A new onset
   can fire only after the ratio falls back below 1.25×. Thresholds and time constants
   are starting values.
8. An onset kicks the rows sideways by 1, 2 or 3 pixels for small, medium or large:
   even rows one way, odd rows the other. Successive kicks alternate direction. A kick
   smaller than the one in progress is ignored. The offset decays by one pixel every
   80 ms.
9. Pixels pushed past the edge of the matrix are clipped.
10. When the radio is not running or is Faulted, no onsets fire.

**Recording status border**

11. While a session is recording, the outer ring of the matrix is a yellow border
    (230, 200, 40) drawn over the animation. While a kick is in progress the whole
    border is off, so the border blinks with radio transients.
12. When a recording faults or aborts, the border turns red (255, 30, 20), blinks three
    times at 250 ms on and 250 ms off, then turns off. Radio onsets do not affect it.
    The fault stays visible on the direct LEDs and the display, which keep their
    existing fault indications.
13. The border follows published Session events (`SES_PUB_RECORDING_STARTED`,
    `SES_PUB_RECORDING_ABORTED`, `SES_PUB_RECORDING_CARD_FULL`), never a command sent.

**Colour vocabulary (D-016)**

14. On the matrix, yellow means recording, red on the blinking border means a fault,
    dim grey means unknown, and white stays reserved for acknowledgements and the
    confirm prompt. Red in the outline means bucket 4; only the border blinks red.

**Load**

15. The renderer is the first work to slow down under recording load. It may drop
    steps, but colour, kicks and the border keep their meaning at the lower rate.

## Consequences

- `54w.8` implements the EMF bucketing, the onset detector and the renderer as
  portable code with host tests: bucket boundaries, staleness, onset thresholds and
  re-arming, kick decay and clipping, border states.
- `p04.3` drives the renderer from the M7 in the Demo preset. A full frame can change
  most of the 81 pixels, so the renderer's per-call pixel budget
  (`UI_RENDER_MATRIX_MAX_FRAME_PIXELS`, 5 today) needs a bounded multi-call frame
  update. Its cost under recording load is measured on the bench.
- The trail setting needs a CLI command, for example `UI MATRIX TRAIL ON|OFF`.
- The direct-LED and display fault indications in `presentation.md` stay in force.
- Revisit this record after the bench run if yellow and bucket 3 amber are hard to
  tell apart on the IS31FL3741, if the border hides too much of the outline while
  recording, or if red for both bucket 4 and faults proves confusing.

## Evidence

- None yet. The preview is a design aid, not evidence of how the matrix looks.
