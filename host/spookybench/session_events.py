"""Decode and check a Field session's EVENTS.BIN (decision 0030; byte layouts in
docs/design/session-format.md). Offline: works on a local file."""
import struct
from .result import BenchError

HEADER = struct.Struct("<8sHHHHI12x")
RECORD = struct.Struct("<QIHHI12s")
MAGIC = b"SPKYEVTS"
SIZE = 32

KINDS = {0x0001: "session_start", 0x0002: "session_end", 0x0010: "tune_start",
         0x0011: "tune_end", 0x0020: "gap_start", 0x0021: "gap_end", 0x0030: "ptt_on",
         0x0031: "ptt_off", 0x0040: "classic", 0x00F0: "events_lost"}
BANDS = {0: "FM", 1: "AM", 2: "SW", 3: "LW"}
OUTCOMES = {1: "tuned", 2: "failed", 3: "abandoned"}
CLASSIC_EVENTS = {0: "run_state", 1: "direction", 2: "rate", 3: "distance", 4: "edge",
                  5: "hold_time", 255: "snapshot"}
RUN_STATES = {0: "running", 1: "paused", 2: "sweep_complete", 3: "unable", 4: "holding"}
EDGES = {0: "wrap", 1: "bounce", 2: "stop"}
END_REASONS = {1: "stopped", 2: "duration complete", 3: "card full", 4: "SD card removed",
               5: "write failed", 6: "radio queue overrun", 7: "PDM queue overrun",
               8: "radio DMA error", 9: "PDM DMA error", 10: "radio timeline unavailable",
               11: "PDM DMA did not start", 255: "other"}
FLAG_CLIPPED, FLAG_BEFORE_ORIGIN = 1, 2


def _payload(kind, raw):
    if kind in (0x0001, 0x0010):
        band, frequency = raw[0], struct.unpack_from("<I", raw, 4)[0]
        key = "frequency_khz" if kind == 0x0001 else "target_khz"
        return {"band": BANDS.get(band, band), key: frequency}
    if kind == 0x0002:
        return {"end_reason": END_REASONS.get(raw[0], raw[0])}
    if kind == 0x0011:
        target, frequency = struct.unpack_from("<II", raw, 4)
        return {"band": BANDS.get(raw[0], raw[0]), "outcome": OUTCOMES.get(raw[1], raw[1]),
                "rssi_dbuv": raw[2], "snr_db": raw[3], "target_khz": target,
                "frequency_khz": frequency}
    if kind in (0x0020, 0x0021):
        value = {"cause": "band_switch" if raw[0] == 1 else raw[0],
                 "band": BANDS.get(raw[1], raw[1])}
        if kind == 0x0021:
            value["frequency_khz"] = struct.unpack_from("<I", raw, 4)[0]
        return value
    if kind == 0x0040:
        setting, rate, distance = struct.unpack_from("<HHH", raw, 6)
        return {"event": CLASSIC_EVENTS.get(raw[0], raw[0]),
                "run_state": RUN_STATES.get(raw[1], raw[1]), "unable_reason": raw[2],
                "edge": EDGES.get(raw[3], raw[3]), "direction_up": bool(raw[4] & 1),
                "rate_limited": bool(raw[4] & 2), "hold_seconds": raw[5],
                "rate_setting_per_min": setting, "rate_per_min": rate,
                "distance_channels": distance}
    if kind == 0x00F0:
        return {"records_dropped": struct.unpack_from("<I", raw, 0)[0]}
    return {}


def decode(data):
    """Return (header, records) or raise BenchError("fail")."""
    if len(data) < SIZE or (len(data) - SIZE) % SIZE:
        raise BenchError("events_invalid", "EVENTS.BIN size is not a header plus whole records",
                         "fail")
    magic, major, minor, record_size, header_size, rate = HEADER.unpack_from(data, 0)
    if magic != MAGIC or major != 1 or record_size != SIZE or header_size != SIZE:
        raise BenchError("events_invalid", "Not a version 1 session event log", "fail")
    records = []
    for offset in range(SIZE, len(data), SIZE):
        frame, uncertainty, kind, flags, sequence, raw = RECORD.unpack_from(data, offset)
        record = {"sequence": sequence, "frame": frame, "uncertainty": uncertainty,
                  "kind": KINDS.get(kind, f"0x{kind:04x}"), "flags": flags}
        record.update(_payload(kind, raw))
        records.append(record)
    return {"version": f"{major}.{minor}", "sample_rate_hz": rate}, records


def summarize(records):
    """Counts and the reader-rule checks of decision 0030 items 10 to 13."""
    counts = {}
    for record in records:
        counts[record["kind"]] = counts.get(record["kind"], 0) + 1
    gaps = sum(b["sequence"] - a["sequence"] - 1 for a, b in zip(records, records[1:])
               if b["sequence"] > a["sequence"] + 1)
    reported = sum(r.get("records_dropped", 0) for r in records if r["kind"] == "events_lost")
    problems = []
    if not records or records[0]["kind"] != "session_start" or records[0]["frame"] != 0:
        problems.append("does not open with session start at frame 0")
    if not records or records[-1]["kind"] != "session_end":
        problems.append("does not close with session end")
    if any(b["sequence"] <= a["sequence"] for a, b in zip(records, records[1:])):
        problems.append("sequence numbers are not increasing")
    if gaps != reported:
        problems.append(f"sequence gaps ({gaps}) differ from events-lost counts ({reported})")
    last = records[-1]["frame"] if records and records[-1]["kind"] == "session_end" else None
    if last is not None and any(r["frame"] > last for r in records):
        problems.append("a record lies after session end")
    return {"records": len(records), "kinds": counts, "sequence_gaps": gaps,
            "records_dropped": reported, "last_frame": last, "problems": problems}


def band_spans(records):
    """(first frame, band) for each band in effect: the session's start band,
    then each band switch from the end of its gap."""
    spans = [(r["frame"], r["band"]) for r in records if r["kind"] == "session_start"]
    spans += [(r["frame"] + 1, r["band"]) for r in records if r["kind"] == "gap_end"]
    return sorted(spans)


def gaps(records):
    """Decision 0004 gaps in session frames, both edges exact and inclusive. A gap
    still open at session end runs to session end."""
    intervals, start, last = [], None, None
    for record in records:
        if record["kind"] == "gap_start":
            start = record
        elif record["kind"] == "gap_end" and start is not None:
            intervals.append({"start": start["frame"], "end": record["frame"],
                              "band_before": start["band"], "band_after": record["band"],
                              "frequency_khz": record["frequency_khz"], "open": False})
            start = None
        elif record["kind"] == "session_end":
            last = record["frame"]
    if start is not None and last is not None:
        intervals.append({"start": start["frame"], "end": last, "band_before": start["band"],
                          "band_after": None, "frequency_khz": None, "open": True})
    return intervals


def tunes(records):
    """Tune intervals in session frames, paired start-to-end (one in flight).

    A tune still in flight when the session ends has no end record (its end was
    observed after the last frame), so its interval runs to session end."""
    intervals, start, last = [], None, None
    for record in records:
        if record["kind"] == "tune_start":
            start = record
        elif record["kind"] == "tune_end" and start is not None:
            intervals.append({"start": start["frame"], "end": record["frame"],
                              "band": record["band"], "outcome": record["outcome"],
                              "target_khz": record["target_khz"]})
            start = None
        elif record["kind"] == "session_end":
            last = record["frame"]
    if start is not None and last is not None:
        intervals.append({"start": start["frame"], "end": last, "band": start["band"],
                          "outcome": "open", "target_khz": start["target_khz"]})
    return intervals
