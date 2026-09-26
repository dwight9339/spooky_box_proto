# Spooky Bench: radio-to-microphone WAV alignment

This offline procedure measures the sample offset and drift between the saved radio and
microphone tracks using an acoustic loopback. It consumes a recorder WAV previously
retrieved and integrity-checked by
[WAV inspection](spooky-bench-wav-inspection.md); it does not access or lock the board.

## Stimulus and recording

Use broadband, nonrepeating program material such as noise or continuous speech. Avoid
steady tones, sparse music, and short loops because repeated phases can produce several
plausible correlation peaks. Fix one earbud or small speaker against the microphone so
its position cannot move during or between recordings. Set a clearly audible level that
does not clip either track; a weak acoustic signal can allow noise or adjacent waveform
phases to win the correlation search.

Record several files without resetting to expose recording-to-recording start variation,
then record at least one file after a reset. Include a long file for drift measurement.
Record the boot grouping, stimulus, physical placement, and any reset in the dated
evidence; those facts cannot be recovered from the WAV container.

Retrieve each file with `wav inspect`. Use its CRC-verified artifact path as the offline
input:

```powershell
host/.venv/Scripts/python.exe -B -m spookybench --json wav align --wav "C:/Users/name/AppData/Local/SpookyBench/runs/<run>/audio/REC009.WAV"
```

Defaults are a 2-second analysis window, 5-second hop, and a search of plus or minus
50 ms. Override them with `--window-seconds`, `--hop-seconds`, and `--max-lag-ms` only
when the evidence records the chosen values and reason.

## Interpretation

Positive lag means that microphone frame `n` matches an earlier radio frame. Compare
median lag between recordings for startup variation. Within one file, inspect every
window as well as the summary:

- `detected=false` means correlation or peak confidence was insufficient.
- Alternating polarity usually indicates competing waveform phases rather than a
  physical polarity change.
- `endpoint_delta_*` is the observed first-to-last change, even when ambiguous.
- Drift follows the polarity held by at least 75% of detected windows. Isolated
  opposite-phase peaks remain visible and count as `drift_excluded_windows`.
- `drift_reliable=true` also requires dominant-phase lag that is monotonic or stable
  within one frame. Only then may `drift_frames` and `drift_ppm` be cited as drift
  evidence.
- A command-level pass means enough windows correlated. It does not override
  `drift_reliable=false` or establish an acceptable product tolerance.

If polarity changes, lag moves nonmonotonically, or too few windows are detected, repeat
the recording with louder fixed broadband stimulus before changing thresholds. Preserve
negative and ambiguous results rather than selecting only favorable windows.

## Evidence

Preserve the complete JSON result, the source WAV run directory, tool source identity,
boot/reset grouping, stimulus setup, and any listening observations in a dated file under
`docs/evidence/`. The current-firmware baseline is
[REC009–REC013 alignment](../evidence/2026-09-25-wav-alignment-rec009-rec013.md).
