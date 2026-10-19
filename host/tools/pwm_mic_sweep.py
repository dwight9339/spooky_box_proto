"""Find which LED PWM channels put 160 ms click bursts in the microphone (demo only).

full_spooky_proto-p04.31. On the Demo image the button and encoder LEDs run as
21.333 kHz timer PWM from the 64 MHz HSI (PSC 2, ARR 999). The microphone's
3.072 MHz PDM clock is 144 times that frequency, but comes from PLL3, whose
fractional divider (FRACN 4719) runs 2.03 ppm fast. A PWM edge that disturbs the
PDM data therefore lines up with the same point of the PDM clock once every
1 / (3.072 MHz x 2.03 ppm) = 160.6 ms, giving a burst of flipped PDM bits: about
20 ms of 2 to 5 sample spikes in the microphone channel.

`analyze` checks local WAV files offline. `sweep` drives the Demo image over the
USB CDC CLI: for each step it sets `DEMO LIGHTS RAW <mask> <duty> <rest>`, records
a short Field session, fetches the WAV with Spooky Bench's CRC transfer, and
analyzes the microphone channel. Only the masked channels switch; every other
LED holds a steady level (no PWM edges). This is a demo-track bench script, not
part of Spooky Bench.
"""
import argparse
from array import array
import json
from pathlib import Path
import re
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from spookybench import serial_io, wav_inspect  # noqa: E402
from spookybench.result import BenchError  # noqa: E402

SAMPLE_RATE = 48000
FRAME_BYTES = 6
MIC = 2
PDM_CYCLES_PER_FRAME = 64  # 3.072 MHz PDM, Sinc4 OSR 64
# PLL3 multiplies its 16 MHz reference by N + FRACN / 8192 = 24 + 4719 / 8192, where
# 24.576 would give exactly 3.072 MHz PDM.
PLL3_RATIO_ERROR = (24 + 4719 / 8192) / 24.576 - 1
BEAT_FRAMES = 1 / PLL3_RATIO_ERROR / PDM_CYCLES_PER_FRAME  # about 7710.1 frames

CLICK_THRESHOLD = 250  # sample-to-sample step; the quiet floor's 10 ms maxima are ~25
CLICK_MERGE = 24       # steps closer than this are one click (one flipped PDM bit)
BINS = 32              # phase bins over one beat, about 5 ms each
WINDOW_BINS = 5        # a burst lasts about 20 ms
CLEAN_CLICKS_PER_SECOND = 0.5

CHANNELS = ("BTN0", "BTN1",
            "ENC0_R", "ENC0_G", "ENC0_B", "ENC1_R", "ENC1_G", "ENC1_B",
            "ENC2_R", "ENC2_G", "ENC2_B", "ENC3_R", "ENC3_G", "ENC3_B")
ALL = (1 << len(CHANNELS)) - 1


# --- Analysis ------------------------------------------------------------------

def read_mic(path, start_seconds=None, end_seconds=None):
    """The microphone channel of a recorder WAV, optionally one span of it."""
    path = Path(path)
    _, offset, size = wav_inspect.read_format(path)
    frames = size // FRAME_BYTES
    first = 0 if start_seconds is None else min(frames, int(start_seconds * SAMPLE_RATE))
    last = frames if end_seconds is None else min(frames, int(end_seconds * SAMPLE_RATE))
    samples = array("h")
    with path.open("rb") as stream:
        stream.seek(offset + first * FRAME_BYTES)
        samples.frombytes(stream.read(max(0, last - first) * FRAME_BYTES))
    if sys.byteorder != "little":
        samples.byteswap()
    return samples[MIC::3]


def clicks(mic, threshold=CLICK_THRESHOLD, merge=CLICK_MERGE):
    """Sample index of the first step of each click."""
    found = []
    last = None
    for index in range(len(mic) - 1):
        if abs(mic[index + 1] - mic[index]) > threshold:
            if last is None or index - last > merge:
                found.append(index)
            last = index
    return found


def _best_window(histogram, width, excluded=()):
    best_start, best_count = 0, -1
    size = len(histogram)
    for start in range(size):
        window = [(start + k) % size for k in range(width)]
        if any(b in excluded for b in window):
            continue
        count = sum(histogram[b] for b in window)
        if count > best_count:
            best_start, best_count = start, count
    return best_start, max(best_count, 0)


def analyze(mic, period=BEAT_FRAMES, threshold=CLICK_THRESHOLD):
    """Click rate and how the clicks fold onto the PWM/PDM beat.

    A burst train puts most clicks in one 20 ms window of the beat (two, if both
    PWM edges disturb the PDM data); unrelated clicks spread over all phases.
    """
    frames = len(mic)
    seconds = frames / SAMPLE_RATE
    found = clicks(mic, threshold)
    histogram = [0] * BINS
    for index in found:
        histogram[int((index % period) / period * BINS) % BINS] += 1
    total = len(found)
    first, first_count = _best_window(histogram, WINDOW_BINS)
    taken = {(first + k) % BINS for k in range(WINDOW_BINS)}
    second, second_count = _best_window(histogram, WINDOW_BINS, taken)
    cycles = max(1, round(frames / period))
    hit = set()
    for index in found:
        phase_bin = int((index % period) / period * BINS) % BINS
        if phase_bin in taken:
            hit.add(int(index // period))
    def phase_ms(start_bin):
        """Mean phase of the clicks in the window starting at start_bin."""
        offsets = []
        for index in found:
            offset = (index % period) - start_bin * period / BINS
            if offset < 0:
                offset += period
            if offset < WINDOW_BINS * period / BINS:
                offsets.append(offset)
        centre = (sum(offsets) / len(offsets) if offsets else WINDOW_BINS * period / BINS / 2)
        return round(((start_bin * period / BINS + centre) % period) / SAMPLE_RATE * 1000, 1)

    rate = total / seconds if seconds else 0.0
    folded = (first_count + second_count) / total if total else 0.0
    if total < 3 or rate < CLEAN_CLICKS_PER_SECOND:
        verdict = "clean"
    elif folded >= 0.6 and len(hit) / cycles >= 0.3:
        verdict = "bursts"
    else:
        verdict = "clicks"
    return {"seconds": round(seconds, 3), "clicks": total,
            "clicks_per_second": round(rate, 2), "verdict": verdict,
            "beat_frames": round(period, 1),
            "train_phase_ms": phase_ms(first),
            "train_fraction": round(first_count / total, 3) if total else 0.0,
            "second_phase_ms": phase_ms(second),
            "second_fraction": round(second_count / total, 3) if total else 0.0,
            "beats_hit": len(hit), "beats": cycles}


# --- Bench sweep -----------------------------------------------------------------

class _Log:
    """JSON lines of everything sent and received; also the transfer's records."""

    def __init__(self, path):
        self.stream = path.open("a", encoding="utf-8")

    def emit(self, record):
        record = dict(record)
        record.setdefault("host_time", time.strftime("%Y-%m-%dT%H:%M:%S"))
        self.stream.write(json.dumps(record, default=str) + "\n")
        self.stream.flush()

    def close(self):
        self.stream.close()


class _Run:
    def reserve(self, size):  # Spooky Bench's artifact budget; none here
        pass


def raw_command(mask, duty, rest):
    return f"DEMO LIGHTS RAW {mask:X} {duty} {rest}"


def plan(duty, rest, channels=CHANNELS):
    """Controls, all channels together, then each channel alone."""
    steps = [("steady-on", 0, 1000, 1000), ("steady-off", 0, 0, 0),
             (f"all-{duty}", ALL, duty, rest)]
    for bit, name in enumerate(CHANNELS):
        if name in channels:
            steps.append((f"{name}-{duty}", 1 << bit, duty, rest))
    return steps


def follow_up(results, duties, rest):
    """Each single channel that was not clean, again at the other duties."""
    steps = []
    for result in results:
        mask = result["mask"]
        if result["analysis"]["verdict"] != "clean" and mask and not mask & (mask - 1):
            name = CHANNELS[mask.bit_length() - 1]
            steps += [(f"{name}-{d}", mask, d, rest) for d in duties if d != result["duty"]]
    return steps


class Device:
    def __init__(self, port, log):
        self.serial = serial_io.open_serial(port)
        self.clock = serial_io.Clock()
        self.pending = bytearray()
        self.log = log

    def close(self):
        self.serial.close()

    def drain(self, quiet=0.5, limit=5.0):
        end = time.monotonic() + limit
        last = time.monotonic()
        while time.monotonic() < end and time.monotonic() - last < quiet:
            data = self.serial.read(4096)
            if data:
                self.pending.extend(data)
                last = time.monotonic()
        while b"\n" in self.pending:
            self._line()

    def _line(self):
        newline = self.pending.index(b"\n")
        text = bytes(self.pending[:newline]).decode("ascii", errors="replace").strip()
        del self.pending[:newline + 1]
        if text:
            self.log.emit({"kind": "rx", "text": text})
        return text

    def read_line(self, deadline):
        while b"\n" not in self.pending:
            if time.monotonic() >= deadline:
                raise BenchError("request_timeout", "CLI response deadline exceeded")
            self.pending.extend(self.serial.read(4096))
        return self._line()

    def command(self, text, accept, timeout=8.0):
        """Send one command; return the first reply line matching accept."""
        self.log.emit({"kind": "tx", "text": text})
        self.serial.write((text + "\n").encode("ascii"))
        deadline = time.monotonic() + timeout
        while True:
            line = self.read_line(deadline)
            if re.match(accept, line):
                return line
            if line.startswith("ERR"):
                raise BenchError("target_error", f"{text}: {line}", "fail")

    def record(self, seconds):
        """Record a Field session; return the WAV path the recorder reports."""
        self.command(f"RECORD START {seconds}", r"OK RECORD (PREPARING|START)")
        deadline = time.monotonic() + seconds + 30.0
        while True:
            line = self.read_line(deadline)
            if line.startswith("OK RECORD PASS"):
                match = re.search(r"file=(\S+)", line)
                if not match:
                    raise BenchError("protocol_error", f"No file in: {line}", "fail")
                self.drain(quiet=1.0)  # RECORD DIAG and RECORD EVENTS follow
                return match[1]
            if line.startswith(("ERR RECORD", "OK RECORD ABORT")):
                raise BenchError("record_failed", line, "fail")

    def fetch(self, name, directory):
        self.drain()
        path, transfer = wav_inspect._transfer(
            self.serial, self.pending, self.log, self.clock,
            self.clock.now() + 300.0, _Run(), name, directory, 44)
        return path, transfer


def sweep(args):
    out = Path(args.out or f"build/pwm-sweep-{time.strftime('%Y%m%dT%H%M%S')}")
    out.mkdir(parents=True, exist_ok=True)
    log = _Log(out / "cli.jsonl")
    device = Device(args.port, log)
    results = []
    identity = None

    def step(name, mask, duty, rest):
        reply = device.command(raw_command(mask, duty, rest), r"OK DEMO LIGHTS ")
        if f"RAW={mask:04X}/{duty}/{rest}" not in reply:
            raise BenchError("protocol_error", f"RAW not applied: {reply}", "fail")
        time.sleep(0.3)
        wav = device.record(args.seconds)
        path, transfer = device.fetch(wav, out / "audio")
        local = out / "audio" / f"{len(results):02d}-{name}.WAV"
        path.replace(local)
        analysis = analyze(read_mic(local))
        result = {"name": name, "mask": mask, "duty": duty, "rest": rest, "file": wav,
                  "local": str(local), "crc32": transfer["crc32"], "analysis": analysis}
        results.append(result)
        log.emit({"kind": "step", **result})
        print(f"{name:>14}  {analysis['verdict']:>6}  {analysis['clicks_per_second']:7.2f}/s"
              f"  train {analysis['train_fraction']:.2f} @ {analysis['train_phase_ms']:6.1f} ms"
              f"  second {analysis['second_fraction']:.2f} @ {analysis['second_phase_ms']:6.1f} ms"
              f"  beats {analysis['beats_hit']}/{analysis['beats']}", flush=True)

    try:
        device.drain()
        identity = device.command("DIAG IDENTITY", r"OK IDENTITY ")
        print(identity, flush=True)
        device.command("ROLL OFF", r"OK ROLL")
        steps = plan(args.duty, args.rest,
                     args.channels.split(",") if args.channels else CHANNELS)
        for name, mask, duty, rest in steps:
            step(name, mask, duty, rest)
        if args.follow_up:
            for name, mask, duty, rest in follow_up(list(results), args.follow_up, args.rest):
                step(name, mask, duty, rest)
        step("steady-on-end", 0, 1000, 1000)
    finally:
        try:
            device.drain()
            device.command("DEMO LIGHTS RAW OFF", r"OK DEMO LIGHTS ")
            device.command("ROLL ON", r"OK ROLL")
        finally:
            device.close()
            summary = {"identity": identity, "seconds": args.seconds,
                       "beat_frames": BEAT_FRAMES, "steps": results}
            (out / "summary.json").write_text(json.dumps(summary, indent=2))
            log.close()
            print(f"results in {out}", flush=True)


# --- Group sweep on an image without RAW ------------------------------------------
#
# Outside a session the buttons follow DEMO LIGHTS IDLE and, on the Classic screen,
# all four encoders are lit white at DEMO LIGHTS ENCODER, each channel scaled by
# BALANCE. Lightness 1000 is duty 1000 (steady on) and 0 is duty 0 (steady off);
# lightness 750 is about duty 480, and BALANCE 500 halves a channel at level 1000.
# The steps run back to back inside one rolling window, which ROLL SAVE then pins.

GROUP_STEPS = (
    ("control-on", 1000, 1000, (1000, 1000, 1000)),
    ("buttons", 750, 1000, (1000, 1000, 1000)),
    ("enc-red", 1000, 1000, (500, 1000, 1000)),
    ("enc-green", 1000, 1000, (1000, 500, 1000)),
    ("enc-blue", 1000, 1000, (1000, 1000, 500)),
    ("enc-all", 1000, 1000, (500, 500, 500)),
    ("control-off", 0, 0, (1000, 1000, 1000)),
    ("control-on-end", 1000, 1000, (1000, 1000, 1000)),
)
GROUP_MARGIN_SECONDS = 1.0  # trimmed at each end of a step before analysis


def _fields(reply):
    return dict(re.findall(r"([A-Z_]+)=(\S+)", reply))


def groups(args):
    out = Path(args.out or f"build/pwm-groups-{time.strftime('%Y%m%dT%H%M%S')}")
    out.mkdir(parents=True, exist_ok=True)
    log = _Log(out / "cli.jsonl")
    device = Device(args.port, log)
    plan_out = {"step_seconds": args.step_seconds, "steps": []}
    try:
        device.drain()
        plan_out["identity"] = device.command("DIAG IDENTITY", r"OK IDENTITY ")
        status = _fields(device.command("DEMO STATUS", r"OK DEMO "))
        if status.get("SCREEN") != "CLASSIC":
            raise BenchError("precondition", "Demo must show the Classic screen "
                             f"(SCREEN={status.get('SCREEN')}), with no session", "fail")
        roll = _fields(device.command("ROLL", r"OK ROLL "))
        if roll.get("STATE") != "RUNNING":
            raise BenchError("precondition", f"Rolling capture is {roll.get('STATE')}", "fail")
        before = _fields(device.command("DEMO LIGHTS", r"OK DEMO LIGHTS "))
        plan_out["lights_before"] = before
        try:
            for name, idle, encoder, balance in GROUP_STEPS:
                device.command(f"DEMO LIGHTS IDLE {idle}", r"OK DEMO LIGHTS ")
                device.command(f"DEMO LIGHTS ENCODER {encoder}", r"OK DEMO LIGHTS ")
                device.command("DEMO LIGHTS BALANCE {} {} {}".format(*balance),
                               r"OK DEMO LIGHTS ")
                start = time.time()
                print(f"{time.strftime('%H:%M:%S')} {name}", flush=True)
                time.sleep(args.step_seconds)
                plan_out["steps"].append({"name": name, "idle": idle, "encoder": encoder,
                                          "balance": balance, "start": start,
                                          "end": time.time()})
            plan_out["save_requested"] = time.time()
            saves, failed = int(roll.get("SAVES", "0")), int(roll.get("FAILED", "0"))
            device.command("ROLL SAVE", r"OK ROLL ")
            deadline = time.monotonic() + 15.0
            while True:
                roll = _fields(device.command("ROLL", r"OK ROLL "))
                if int(roll.get("SAVES", "0")) > saves:
                    break
                if int(roll.get("FAILED", "0")) > failed:
                    raise BenchError("save_failed", f"ROLL SAVE failed: {roll}", "fail")
                if time.monotonic() > deadline:
                    raise BenchError("request_timeout", "ROLL SAVE did not commit", "fail")
                time.sleep(0.5)
            plan_out["capture"] = roll["LAST"]
            plan_out["capture_frames"] = int(roll["LAST_FRAMES"])
            print(f"saved {roll['LAST']} ({int(roll['LAST_FRAMES']) / SAMPLE_RATE:.1f} s); "
                  f"read CAPS/{roll['LAST']}.TXT and its segments from the card", flush=True)
        finally:
            balance = before.get("BALANCE", "1000,450,350").split(",")
            device.command(f"DEMO LIGHTS IDLE {before.get('IDLE', 400)}", r"OK DEMO LIGHTS ")
            device.command(f"DEMO LIGHTS ENCODER {before.get('ENCODER', 800)}",
                           r"OK DEMO LIGHTS ")
            device.command("DEMO LIGHTS BALANCE {} {} {}".format(*balance), r"OK DEMO LIGHTS ")
    finally:
        device.close()
        (out / "groups.json").write_text(json.dumps(plan_out, indent=2))
        log.close()
        print(f"plan in {out / 'groups.json'}", flush=True)


def read_capture(caps, name):
    """The microphone channel of a saved rolling capture, segments in order."""
    caps = Path(caps)
    descriptor = (caps / f"{name}.TXT").read_text()
    segments = re.findall(r"^(\S+\.WAV) frames=\d+", descriptor, re.M)
    if not segments:
        raise BenchError("capture_invalid", f"No segments in {name}.TXT", "fail")
    mic = array("h")
    for segment in segments:
        mic.extend(read_mic(caps / segment))
    return mic


def groups_analyze(args):
    plan_in = json.loads(Path(args.plan).read_text())
    mic = read_capture(args.caps, args.capture or plan_in["capture"])
    end_frame = len(mic)
    saved = plan_in["save_requested"]
    rows = []
    for step in plan_in["steps"]:
        first = end_frame - int((saved - step["start"] - GROUP_MARGIN_SECONDS) * SAMPLE_RATE)
        last = end_frame - int((saved - step["end"] + GROUP_MARGIN_SECONDS) * SAMPLE_RATE)
        if first < 0 or last <= first:
            rows.append({"name": step["name"], "analysis": None,
                         "note": "outside the saved window"})
            continue
        analysis = analyze(mic[first:last])
        rows.append({"name": step["name"], "from_s": round(first / SAMPLE_RATE, 2),
                     "to_s": round(last / SAMPLE_RATE, 2), "analysis": analysis})
        print(f"{step['name']:>15}  {first / SAMPLE_RATE:5.1f}-{last / SAMPLE_RATE:5.1f} s"
              f"  {analysis['verdict']:>6}  {analysis['clicks_per_second']:6.2f}/s"
              f"  train {analysis['train_fraction']:.2f} @ {analysis['train_phase_ms']:6.1f} ms"
              f"  beats {analysis['beats_hit']}/{analysis['beats']}", flush=True)
    # Clicks per half second over the whole capture, to check the step alignment.
    found = clicks(mic)
    half = SAMPLE_RATE // 2
    timeline = [0] * (len(mic) // half + 1)
    for index in found:
        timeline[index // half] += 1
    print("clicks per 0.5 s:", " ".join(str(v) for v in timeline), flush=True)
    result = {"plan": args.plan, "steps": rows, "timeline_half_seconds": timeline}
    Path(args.plan).with_name("groups-analysis.json").write_text(json.dumps(result, indent=2))


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    commands = p.add_subparsers(dest="action", required=True)
    a = commands.add_parser("analyze", help="offline: analyze local WAV files")
    a.add_argument("wav", nargs="+")
    a.add_argument("--start", type=float, help="seconds into each file")
    a.add_argument("--end", type=float, help="seconds into each file")
    s = commands.add_parser("sweep", help="bench: step the LED PWM channels (Demo image)")
    s.add_argument("--port", required=True, help="device CDC port, e.g. COM3")
    s.add_argument("--seconds", type=int, default=4, help="recording per step (default 4)")
    s.add_argument("--duty", type=int, default=500, help="duty of the switching channels")
    s.add_argument("--rest", type=int, default=1000, help="steady duty of the others")
    s.add_argument("--channels", help="comma-separated subset of " + ",".join(CHANNELS))
    s.add_argument("--follow-up", type=lambda t: [int(v) for v in t.split(",") if v],
                   default=[100, 900], help="duties to retry noisy channels at ('' for none)")
    s.add_argument("--out", help="output directory (default build/pwm-sweep-<time>)")
    g = commands.add_parser("groups", help="bench: step LED groups with IDLE/ENCODER/BALANCE "
                            "in one rolling window, then ROLL SAVE (any Demo image)")
    g.add_argument("--port", required=True, help="device CDC port, e.g. COM3")
    g.add_argument("--step-seconds", type=float, default=6.5,
                   help="per step; all steps must fit the 60 s window (default 6.5)")
    g.add_argument("--out", help="output directory (default build/pwm-groups-<time>)")
    ga = commands.add_parser("groups-analyze", help="offline: analyze a groups capture")
    ga.add_argument("--plan", required=True, help="groups.json written by groups")
    ga.add_argument("--caps", required=True, help="the card's CAPS directory, e.g. F:/CAPS")
    ga.add_argument("--capture", help="capture name, default the plan's (Cnnn)")
    args = p.parse_args(argv)
    if args.action == "groups":
        groups(args)
        return 0
    if args.action == "groups-analyze":
        groups_analyze(args)
        return 0
    if args.action == "analyze":
        for name in args.wav:
            print(json.dumps({"file": name, **analyze(read_mic(name, args.start, args.end))}))
        return 0
    sweep(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
