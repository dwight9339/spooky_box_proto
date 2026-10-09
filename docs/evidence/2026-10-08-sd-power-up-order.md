# SD card wedged by the power-up order

Date: 2026-10-08 (local, UTC-6)  
Beads issues: `full_spooky_proto-3nc` (the 30 s write stall), note on
`full_spooky_proto-p04.8` (field rig power-up)

The p04.15 and p04.16 bench sessions each began with an SD card that would not
initialize (`FR_NOT_READY`, `stage=HAL_INIT`, `hal=0x10000000`) until it was reseated.
The operator had powered the board down from Instrument mode both times and asked
whether the mode was the cause. This session tested that against the power-up order.

**Result: the power-up order, not the mode.** Powering up with the USB cables connected
before the battery switch failed twice, once from each mode. Powering up with the
battery switch first passed twice, once from each mode.

## Setup

The bench and SD card were those of the p04.16 session
(`docs/evidence/2026-10-08-slicer-sequencer-demo.md` on `demo/halloween-2026`), running
image `p0416-demo-20261008a` (`7a34e47`, Demo preset). The audio shield, and with it the SD
card, takes 3.3 V from the Nucleo's Zio header ([prototype hardware](../design/prototype-hardware.md)).
The card has no load switch, so firmware cannot power-cycle it.

Every trial powered down the operator's usual way: the Spooky Box USB and the Spooky
Probe USB unplugged, then the battery switch off, about 10 s off. The operator's usual
power-up was the reverse: both cables connected, then the battery switch on. With the
cables in and the battery switch off, the rig receives limited power from USB, the
condition that wedged the card in the p04.3 session
(`docs/evidence/2026-10-04-field-on-m7-demo.md` on `demo/halloween-2026`).

After each power-up, `build/sd_boot_check.py` waited for the CDC (COM3) and logged
`DIAG IDENTITY`, `SD STATUS`, `ROLL`, `DIAG STATUS` and `DIAG DUMP` to
`build/sdboot-20261008.jsonl`. Rolling capture mounts the card at boot and retries
every 5 s, so `ROLL STATE=RUNNING` shows a working card. While rolling capture holds
the card, `SD STATUS` answers `FR_LOCKED`; after a failure, it makes its own mount
attempt. Each passing trial reported `BOOT=1` with the power-on and brown-out reset
flags set, a cold start.

## Trials

| Trial | Mode at power-down | Power-up order | Result |
| --- | --- | --- | --- |
| A1 | Instrument, Slicer looping | Cables, then battery | Fail: one write took 30,016 ms; card wedged |
| A2 | Field | Cables, then battery | Fail: one write took 30,003 ms; card wedged |
| B2 | Field | Battery, then cables | Pass: rolling capture started first time, longest write 38 ms, no fault |
| B1 | Instrument | Battery, then cables | Pass: rolling capture started first time, longest write 28 ms, no fault |

The card was reseated with the board on after A1, and `ROLL STATE=RUNNING` confirmed it
before A2. B2 was meant to be B1, but the operator powered it down from Field. The
failing trials' sequence, from `DIAG DUMP` (times from boot):

| Time | Event |
| --- | --- |
| 0 to 1.4 s | Boot; rolling capture mounts the card |
| 1.4 to 2.6 s | 15 writes of 16 to 22 ms |
| about 2.65 s | One write starts and does not complete |
| 3.3 s | `RADIO_OVERRUN` and `PDM_OVERRUN`: the foreground is blocked |
| 32.7 s | The write fails at the 30 s transfer timeout (`SD_WRITE A=30016`, `STORAGE_MARGIN`); `LOOP_STALL A=30124` |

Afterwards rolling capture was in `FAULT` and every initialization failed at once
(`init_ms=3`, `hal=0x10000000`): the HAL's error when the card answers CMD8 but not
CMD55 or ACMD41. `DIAG IDENTITY` got no answer while the foreground was blocked.

## Interpretation

The card initializes and works at first in both orders. In the cables-first order it
hangs in a write about 2.6 s into the boot, after which no command reset clears it;
only a reseat (a real power cycle) does. The likely trigger is the supply change when
the battery switch goes on while the rig is running on limited USB power: the trials
did not measure the rail or time the switch, so this is inferred, not observed. The
mode at power-down cannot matter to it: the demo retains no state across power-off,
and rolling capture writes in every mode.

Firmware made the failure worse than it needed to be: one write the card never
completes stalls the foreground for the full 30 s data-transfer timeout, overrunning
the audio queues (`full_spooky_proto-3nc`; `jjy.15` bounded only the mount path).

## Not exercised

- The 3.3 V rail during power-up, and the time between connecting the cables and
  switching the battery on.
- Cables first with a long wait before the battery switch, or the battery switch
  without the probe cable.
- More than two trials per order.
