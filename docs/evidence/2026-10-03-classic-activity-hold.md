# Classic activity hold

Date: 2026-10-03 (local, UTC-6; Spooky Bench timestamps are UTC)  
Beads issue: `full_spooky_proto-54w.32`

This session ran the Classic activity hold on the NUCLEO-H755ZI-Q bench
([spec 001](../../spec/specs/001-classic-scan-engine/spec.md) User Story 5,
[decision 0016](../decisions/0016-classic-scan-motion.md) items 16 to 20 and 23). The
operator confirmed that the board was connected and free, with the radio path, the
headphones and an SD card installed. Spooky Bench 0.7.0 ran on Windows with target CDC
on COM3 and Spookyprobe on COM6. Classic was driven over the USB CLI
([USB CLI](../design/usb-cli.md#classic-scan-engine)) by an ad hoc pyserial script (not
part of Spooky Bench); raw exchanges are in the untracked local `build/` directory.

**Result:** the hold works as specified and the recording regression passes, but
spec **SC-008 failed** at this bench with both trigger sizes tried. Neither setting held
on the stations without also holding on static, so the trigger size and release time
are not set (decision 0016 item 23).

## Images

All images were built from `cba0e73de756f5fb08bb9674ccc5fc81145023c9` plus the
uncommitted `54w.32` changes, archived as a source patch per build (`build/<build ID>.patch`,
SHA-256 in the manifest), with GNU Tools for STM32 14.3.1+st.2. Every flash passed.

| Build ID | Preset | Hold trigger | CM7 SHA-256 | Source patch SHA-256 |
| --- | --- | --- | --- | --- |
| `54w32-hold-ipcsmoke-20261003a` | IpcSmoke | medium | `99fa52d2db55fa274e3cb6af7291125c1ef591ea1b974aa75a9d338191029225` | `e2555bf2551e7a1e1acf9f26bba8d7a455d0ea16cc106d54fa57b34d545ff1c5` |
| `54w32-hold-ipcsmoke-20261003b` | IpcSmoke | small | `4817a744860d7f44f1cb0e75b721543ff647f2e1a0d719ef2f848a8886082798` | `e5665255da7cf056281f41456707b7b24689a87e9372ce8abbdc8eefee562edf` |
| `54w32-hold-tunequal-20261003c` | RadioTuneQual | medium | `38749073f11a2e53f9af4b217dca94a5307809519c5edbebbb3dde3e7e610bf6` | `d2198f6b118defe745694efd669f14419519d3069241055e8ce32427c73c73b0` |
| `54w32-hold-ipcsmoke-20261003d` | IpcSmoke | small | `381dc3f9a07fe27eb0618d61b18d1c03e2d803d818c7781f48b824cc0c9762e7` | `56285d9026dd8c1806bbf32d445b6ecb72ba5245d338decd6392818b2128a769` |

The IpcSmoke CM4 image was `7d9d1116cb20cd8367de3d86a2ec8f067b1cf0fa0ea9220279c6cf9492489584`
throughout. Builds b to d also log each radio onset as an `[activity]` line. Build c is
the opt-in `RadioTuneQual` qualification image, used only to record a sweep with
tuning allowed during the recording; its results are qualification data, not
default-build behavior. The committed code keeps the medium trigger. Native host tests
passed 28/28 (MSVC 14.29); host tests are not hardware evidence.

## Reception at the bench

A survey tuned every FM channel with Classic paused (build a). The strongest channel
reported 26 dBuV with an SNR of at most 7 dB, and the receiver flagged no channel as
valid. RSSI was a poor guide: in a 10 s listening tour of ten channels chosen by RSSI,
the user heard all ten as static, including 98.5, 96.9 and 93.9 MHz, while during
sweeps the user hears stations come through. Station audio was identified by ear
instead (see the recorded sweep).

## Onsets on fixed channels

With Classic paused on build a, 20 s per channel: 98.5 MHz gave 6 small onsets, 96.9
gave 3, 100.1 gave 1, 93.9 and 95.5 none, and four static channels (88.1, 89.6, 90.0,
88.5 MHz, RSSI 0 to 1) none. No medium or large onsets.

## Sweeps

All at 120 jumps per minute, 1 channel, wrap, hold time 10 s.

| Sweep | Build, trigger | Run | Result |
| --- | --- | --- | --- |
| A | a, medium | `2026-10-03T165453.387804_0000-f4a26fe0` | 282 jumps (1.4 passes), 18 small and 0 medium onsets, **0 holds** |
| B | b, small | `2026-10-03T165834.945493_0000-054e51f5` | 20 holds of 1.5 to 2.3 s. The user heard every held channel checked in the tour (87.7, 89.3, 92.3, 92.5, 107.7, 96.9, 93.9 MHz) as static |
| D | d, small | `2026-10-03T174213.429225_0000-6f9fd083` | 114 jumps (87.5 to 98.9 MHz), 17 holds of 1.50 to 10.00 s; see below |

In sweep D, three holds (91.1, 92.9 and 96.0 MHz) ended at exactly the 10.00 s hold
time, with onsets still firing, and three more lasted 5.5 to 8.7 s. No hold exceeded
the hold time. Listening live, the user judged that "over half landed on actual audio
and at least a couple of the ones that landed on static held for a long time", and
that the long holds "felt too long": the user would set no more than 2 to 3 s.

Each hold started on the pass where the onset arrived and released 1.50 to 1.51 s after
the last onset unless the hold time ended it first, as specified.

## Recorded sweep (build c)

To identify station audio, a full FM sweep was recorded with `RECORD START 120` and
`CLASSIC RUN` from 87.5 MHz, hold off (runs `2026-10-03T172537.835867_0000-2a3a5393`
flash, `2026-10-03T172604.251451_0000-ead03ec2` console).

- `REC105.WAV`: `RECORD RESULT outcome=PASS`, 5,763,072 frames, 34,578,432 data bytes,
  finalized. Copied from the card on the host (not CRC-transferred), SHA-256
  `a7004648eb52dda244067513be33c8f183e3aa726ac146353c038731ff2cd263`; untracked in
  `build/`. Queues at most 1/8, longest SD write 52 ms.
- 221 jumps. Only 102 tunes left a run of exact-zero radio samples (5.8 to 7.0 ms),
  all on the 500 ms schedule, so landings were labelled from the schedule. The first
  jump left no zero run; the anchor is the device's jump count (`JUMPS=257` at about
  115.0 s of audio fits only that alignment) and the final frequency, 89.0 MHz.
- Per landing, an ad hoc script measured level, the variation of 10.67 ms block levels
  and the zero-crossing rate, and took the device's onsets from the `[activity]` lines.

The user listened to two clip files. Of 41 candidates (35 landings that fired an onset,
plus 6 chosen by the audio measures alone), "pretty much all" sounded like station
audio. Of 30 landings without onsets, spread across the sweep, two were station audio
(96.2 and 99.5 MHz), one faintly might be (104.7 MHz) and 27 were static.

In this recording, then, onsets of any size fell on station landings, and about 40% of
station landings fired none. Medium onsets fired on about 4 of roughly 18 stations
(clusters of adjacent channels). Station audio was louder than static here (about −26
to −36 dBFS against a steady −39.5 dBFS) and had a lower zero-crossing rate (mostly
1,000 to 5,000 per second against 7,000 to 8,000).

## Spec SC-008

Not met. With the medium trigger, Classic held on no static channel but missed most
stations (sweep A: no holds; recorded sweep: about 4 of 18). With the small trigger it
held on stations and on static, including long holds on static (sweeps B and D). The
recorded sweep alone suggested that small would pass; the live sweeps do not bear that
out, possibly because reception drifted during the session. The trigger size and
release time stay at decision 0016's starting values (medium, 1.5 s).

## Recording regression (build b)

One `test recording-regression --manifest build/bench-54w32-hold-ipcsmoke-20261003b.json --seconds 60 --stimulus ambient`
run, `2026-10-03T170225.350187_0000-c98524b6`: **pass**, all six stages. This covers the
added work in the radio capture callback: one mean-absolute level per half-buffer.

- Target reported build `54w32-hold-ipcsmoke-20261003b`, boot epoch 4.
- `REC104.WAV`: 2,883,584 frames, 17,301,504 data bytes, 60.075 s audio, 60,125 ms
  elapsed. Transfer CRC32 `69c174ea`, SHA-256
  `253495f4b2b67ce7f15b8b2589c98e40bfc88e2af62edc2caf5b3bd1009ff474`.
- Radio and PDM queue high-water 1/8; maximum SD write 26 ms; no clipped samples; radio
  channels mono-like (correlation 0.99997); no logger drops or transmit errors.

`DIAG LATENCY` afterwards, recording-only maxima: loop 32 ms (budget 75), recorder
26 ms (70), Classic 2 ms (10), the new activity feed 2 ms (10), every other service at
most 2 ms, zero violations. Across the session the activity feed dropped no block, and
at most 7 of its 32 entries were waiting at once.

No listening check was made of the regression WAV.

## Not done

- SC-008 with a trigger that separates stations from static. Decision 0016's
  Consequences name this case ("revisit if onsets cannot tell stations from FM
  static"); the follow-up is a Beads issue linked from `54w.32`.
- Bands other than FM.
- The hold during a session on the default build: the recording guard keeps Classic
  unable to scan (`54w.6`).
