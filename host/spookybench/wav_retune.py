"""Check a recorder WAV's radio zero runs against logged retune stamps; offline.

Decision 0015 item 10: with tunes stamped on the radio sample timeline, every
exact-zero radio run of 1 ms or more after the first tune must lie inside a
marked retune interval. The per-band settle margin is the largest overhang of a
zero run past its tune's end stamp, rounded up to a whole radio half-buffer.

Inputs are the WAV, the UART log holding the firmware's [retune] lines, and the
recording's timeline origin from RECORD TIMELINE (WAV frame 0 is that position).
"""
from array import array
from pathlib import Path
import re
import sys
from .result import BenchError
from .wav_inspect import read_format

RATE_HZ = 48000
MIN_RUN_FRAMES = RATE_HZ // 1000          # 1 ms
HALF_BUFFER_FRAMES = 512                  # radio audio reaches the M7 in these
EXAMPLES = 20

RETUNE = re.compile(
    r"\[retune\] start=(\d+):(\d+|-) end=(\d+):(\d+|-) unc=(\d+) band=(\w+) "
    r"target=(\d+) outcome=(\w+) freq=(\d+)")
ORIGIN = re.compile(r"^(\d+):(\d+)$")


def parse_origin(text):
    match = ORIGIN.match(text.strip())
    if not match:
        raise BenchError("invalid_invocation", "--origin must be EPOCH:FRAME, e.g. 1:16423219")
    return int(match.group(1)), int(match.group(2))


def parse_retunes(lines, origin):
    """Return (tunes, unstamped): tunes in WAV frames for the origin's epoch."""
    epoch, origin_frame = origin
    tunes, unstamped = [], 0
    for line in lines:
        match = RETUNE.search(line)
        if not match:
            continue
        start_epoch, start, end_epoch, end = match.group(1, 2, 3, 4)
        if start == "-" or end == "-":
            unstamped += 1
            continue
        if int(start_epoch) != epoch or int(end_epoch) != epoch:
            continue
        tunes.append({"start": int(start) - origin_frame, "end": int(end) - origin_frame,
                      "band": match.group(6), "outcome": match.group(8),
                      "target_khz": int(match.group(7))})
    tunes.sort(key=lambda tune: tune["start"])
    return tunes, unstamped


def zero_runs(left, right, minimum=MIN_RUN_FRAMES):
    """[first, last] frame pairs where both radio channels are exactly zero."""
    runs, begin = [], None
    for index, (a, b) in enumerate(zip(left, right)):
        if a == 0 and b == 0:
            if begin is None:
                begin = index
        elif begin is not None:
            if index - begin >= minimum:
                runs.append((begin, index - 1))
            begin = None
    if begin is not None and len(left) - begin >= minimum:
        runs.append((begin, len(left) - 1))
    return runs


def classify(runs, tunes, frames):
    """Match each zero run after the first tune to the tune intervals it overlaps."""
    in_file = [tune for tune in tunes if tune["end"] >= 0 and tune["start"] < frames]
    first = in_file[0]["start"] if in_file else None
    explained, unexplained, overhang = 0, [], {}
    for first_frame, last_frame in runs:
        if first is None or last_frame < first:
            continue
        overlapping = [tune for tune in in_file
                       if tune["start"] <= last_frame and first_frame <= tune["end"]]
        if overlapping:
            tune = overlapping[-1]
            explained += 1
            past = max(0, last_frame - tune["end"])
            overhang[tune["band"]] = max(overhang.get(tune["band"], 0), past)
            continue
        before = [tune for tune in in_file if tune["end"] < first_frame]
        unexplained.append({"first_frame": first_frame, "frames": last_frame - first_frame + 1,
                            "after_end_frames": (first_frame - before[-1]["end"]) if before else None})
    margins = {band: -(-value // HALF_BUFFER_FRAMES) * HALF_BUFFER_FRAMES
               for band, value in overhang.items()}
    return {"tunes_in_file": len(in_file), "zero_runs_checked": explained + len(unexplained),
            "explained": explained, "unexplained": len(unexplained),
            "unexplained_examples": unexplained[:EXAMPLES],
            "max_overhang_frames": overhang, "settle_margin_frames": margins}


def tune_summary(tunes):
    summary = {}
    for tune in tunes:
        entry = summary.setdefault(tune["band"], {"tunes": 0, "outcomes": {}, "max_frames": 0})
        entry["tunes"] += 1
        entry["outcomes"][tune["outcome"]] = entry["outcomes"].get(tune["outcome"], 0) + 1
        entry["max_frames"] = max(entry["max_frames"], tune["end"] - tune["start"])
    return summary


def analyze(wav_path, log_lines, origin):
    _fmt, data_offset, data_size = read_format(wav_path)
    samples = array("h")
    with wav_path.open("rb") as stream:
        stream.seek(data_offset)
        samples.frombytes(stream.read(data_size))
    if sys.byteorder != "little":
        samples.byteswap()
    left, right = samples[0::3], samples[1::3]
    tunes, unstamped = parse_retunes(log_lines, origin)
    metrics = classify(zero_runs(left, right), tunes, len(left))
    metrics.update({"frames": len(left), "unstamped_tunes": unstamped,
                    "bands": tune_summary([t for t in tunes if t["end"] >= 0
                                           and t["start"] < len(left)])})
    return metrics


def verdict(metrics):
    if metrics["tunes_in_file"] == 0:
        return "fail", "no_tunes", "no stamped tune falls inside the WAV"
    if metrics["unstamped_tunes"]:
        return "fail", "unstamped_tunes", f"{metrics['unstamped_tunes']} tune(s) without stamps"
    if metrics["unexplained"]:
        return "fail", "zero_run_outside_retune", \
            f"{metrics['unexplained']} zero run(s) outside every retune interval"
    return "pass", None, None


def run_local(options):
    wav_path, log_path = Path(options["wav"]), Path(options["log"])
    for path in (wav_path, log_path):
        if not path.is_file():
            raise BenchError("invalid_invocation", f"file not found: {path}")
    lines = log_path.read_bytes().decode("utf-8", errors="replace").splitlines()
    metrics = analyze(wav_path, lines, parse_origin(options["origin"]))
    return metrics, verdict(metrics)
