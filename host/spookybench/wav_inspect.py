"""Retrieve one recorded WAV through bounded CDC frames and inspect it locally.

The same transfer fetches a Field session's manifest (decision 0030 item 18).
"""
from array import array
import hashlib
import json
import math
import re
import struct
import sys
import time
import zlib
from pathlib import Path
from . import fake, serial_io, session_events
from .artifacts import JsonLines
from .result import BenchError

WAV_PATH = r"REC[0-9]{3}\.WAV|FIELD/F[0-9]{4}/AUDIO[0-9]{3}\.WAV"
SESSION = r"F[0-9]{4}"
START = re.compile(rb"^OK WAV START file=([A-Z0-9_./]+) bytes=([0-9]+) "
                   rb"chunk=([0-9]+) protocol=([0-9]+)\r?\n$")
HEADER = struct.Struct("<4sIHHI")
MAGIC = b"WV01"
DATA, END, ERROR = 1, 2, 4
MAX_FILE_BYTES = 256 * 1024 * 1024
MANIFEST_MAX_BYTES = 1024


def filename(text):
    value = text.upper()
    if not re.fullmatch(WAV_PATH, value):
        raise ValueError("WAV filename must be REC###.WAV or FIELD/Fnnnn/AUDIOnnn.WAV")
    return value


def session(text):
    value = text.upper()
    if not re.fullmatch(SESSION, value):
        raise ValueError("session must be Fnnnn")
    return value


def parse_manifest(text):
    """MANIFEST.TXT key=value lines (docs/design/session-format.md)."""
    fields = {}
    for line in text.splitlines():
        if not line:
            continue
        key, sep, value = line.partition("=")
        if not sep or not key:
            raise BenchError("protocol_error", f"Invalid manifest line: {line[:60]}", "fail")
        fields[key] = value
    return fields


def _read_exact(serial, pending, length, clock, deadline):
    while len(pending) < length:
        if clock.now() >= deadline:
            raise BenchError("request_timeout", "WAV transfer response deadline exceeded")
        data = serial.read(min(4096, length - len(pending)))
        if data:
            pending.extend(data)
    value = bytes(pending[:length])
    del pending[:length]
    return value


def _read_line(serial, pending, clock, deadline):
    while True:
        newline = pending.find(b"\n")
        if newline >= 0:
            if newline + 1 > 512:
                raise BenchError("protocol_error", "WAV start response exceeds 512 bytes", "fail")
            value = bytes(pending[:newline + 1])
            del pending[:newline + 1]
            return value
        if len(pending) > 512:
            raise BenchError("protocol_error", "WAV start response exceeds 512 bytes", "fail")
        pending.extend(_read_exact(serial, bytearray(), 1, clock, deadline))


def _write(serial, data):
    if serial.write(data) != len(data):
        raise OSError("short command write")


def read_format(path):
    """Validate the recorder's WAV container; return (fmt, data_offset, data_size)."""
    size = path.stat().st_size
    with path.open("rb") as stream:
        header = stream.read(12)
        if len(header) != 12 or header[:4] != b"RIFF" or header[8:] != b"WAVE":
            raise BenchError("wav_invalid", "Missing RIFF/WAVE header", "fail")
        riff_size = struct.unpack_from("<I", header, 4)[0]
        if riff_size + 8 != size:
            raise BenchError("wav_invalid", "RIFF size does not match the transferred file", "fail")
        fmt = None
        data_offset = data_size = None
        while stream.tell() < size:
            chunk_header = stream.read(8)
            if len(chunk_header) != 8:
                raise BenchError("wav_invalid", "Truncated WAV chunk header", "fail")
            kind, length = struct.unpack("<4sI", chunk_header)
            start = stream.tell()
            end = start + length
            if end > size:
                raise BenchError("wav_invalid", "WAV chunk exceeds the file boundary", "fail")
            if kind == b"fmt ":
                if fmt is not None or length < 16:
                    raise BenchError("wav_invalid", "Invalid or duplicate fmt chunk", "fail")
                raw = stream.read(length)
                fmt = struct.unpack_from("<HHIIHH", raw)
            elif kind == b"data":
                if data_offset is not None:
                    raise BenchError("wav_invalid", "Duplicate data chunk", "fail")
                data_offset, data_size = start, length
                stream.seek(length, 1)
            else:
                stream.seek(length, 1)
            if length & 1:
                if stream.read(1) == b"":
                    raise BenchError("wav_invalid", "Missing WAV chunk padding", "fail")
        if fmt is None or data_offset is None:
            raise BenchError("wav_invalid", "WAV requires fmt and data chunks", "fail")

    expected = (1, 3, 48000, 48000 * 6, 6, 16)
    if fmt != expected:
        raise BenchError("wav_format", "Expected PCM 48 kHz/16-bit/3-channel WAV", "fail")
    if not data_size or data_size % fmt[4]:
        raise BenchError("wav_invalid", "WAV data is empty or not frame-aligned", "fail")
    return fmt, data_offset, data_size


def _analyze(path):
    fmt, data_offset, data_size = read_format(path)
    audio_format, channels, sample_rate, byte_rate, block_align, bits = fmt
    count = data_size // block_align
    sums = [0, 0, 0]
    squares = [0, 0, 0]
    peaks = [0, 0, 0]
    minima = [32767, 32767, 32767]
    maxima = [-32768, -32768, -32768]
    zeros = [0, 0, 0]
    clipped = [0, 0, 0]
    cross = [0, 0, 0]
    remaining = data_size
    with path.open("rb") as stream:
        stream.seek(data_offset)
        while remaining:
            raw = stream.read(min(remaining, 6 * 16384))
            if len(raw) % 6:
                raise BenchError("wav_invalid", "Short sample frame", "fail")
            remaining -= len(raw)
            samples = array("h")
            samples.frombytes(raw)
            if sys.byteorder != "little":
                samples.byteswap()
            for index in range(0, len(samples), 3):
                a, b, c = samples[index], samples[index + 1], samples[index + 2]
                values = (a, b, c)
                cross[0] += a * b
                cross[1] += a * c
                cross[2] += b * c
                for channel, value in enumerate(values):
                    magnitude = abs(value)
                    sums[channel] += value
                    squares[channel] += value * value
                    peaks[channel] = max(peaks[channel], magnitude)
                    minima[channel] = min(minima[channel], value)
                    maxima[channel] = max(maxima[channel], value)
                    zeros[channel] += value == 0
                    clipped[channel] += value in (-32768, 32767)

    if any(minima[index] == maxima[index] for index in range(3)):
        raise BenchError("wav_silent_channel", "At least one WAV channel is constant", "fail")

    names = ("radio_left", "radio_right", "microphone")
    channels_out = {}
    for index, name in enumerate(names):
        channels_out[name] = {"minimum": minima[index], "maximum": maxima[index],
            "peak": peaks[index], "mean": sums[index] / count,
            "rms": math.sqrt(squares[index] / count),
            "zero_samples": zeros[index], "clipped_samples": clipped[index]}

    correlations = {}
    for name, left, right, product in (("radio_left_radio_right", 0, 1, cross[0]),
        ("radio_left_microphone", 0, 2, cross[1]),
        ("radio_right_microphone", 1, 2, cross[2])):
        numerator = count * product - sums[left] * sums[right]
        denominator = math.sqrt((count * squares[left] - sums[left] ** 2) *
                                (count * squares[right] - sums[right] ** 2))
        correlations[name] = numerator / denominator if denominator else None

    return {"container": "RIFF/WAVE", "audio_format": audio_format,
        "channels": channels, "sample_rate_hz": sample_rate,
        "bits_per_sample": bits, "block_align": block_align,
        "byte_rate": byte_rate, "data_bytes": data_size, "frames": count,
        "duration_seconds": count / sample_rate, "signal": channels_out,
        "correlations": correlations}


def _transfer(serial, pending, records, clock, deadline, run, name, directory, minimum):
    """Fetch one file into directory; returns (path, transfer metrics).

    On any failure after the device started sending, the transfer is aborted.
    """
    started = clock.now()
    transfer_started = False
    completed = False
    stream = None
    _write(serial, f"WAV FETCH {name}\n".encode("ascii"))
    line = _read_line(serial, pending, clock, deadline)
    records.emit({"kind": "wav_start", "raw": line.decode("ascii", errors="replace"),
                  "host_receive_ns": time.time_ns()})
    if line.startswith(b"ERR WAV"):
        raise BenchError("wav_unavailable", line.decode("ascii", errors="replace").strip(), "fail")
    match = START.fullmatch(line)
    if not match:
        raise BenchError("protocol_error", "Invalid WAV transfer start response", "fail")
    got, size, chunk, protocol = match[1].decode(), int(match[2]), int(match[3]), int(match[4])
    if got != name or protocol != 1 or not 1 <= chunk <= 65519 or \
       not minimum <= size <= MAX_FILE_BYTES:
        raise BenchError("protocol_error", "Invalid WAV transfer parameters", "fail")
    try:
        run.reserve(size)
        directory.mkdir(exist_ok=True)
        local = name.replace("/", "_")
        partial, final = directory / (local + ".partial"), directory / local
        stream = partial.open("xb")
        transfer_started = True
        expected = frames = whole_crc = 0
        next_flush = 1024 * 1024
        sha256 = hashlib.sha256()
        while True:
            raw_header = _read_exact(serial, pending, HEADER.size, clock, deadline)
            magic, offset, length, flags, frame_crc = HEADER.unpack(raw_header)
            if magic != MAGIC or offset != expected or length > chunk or flags not in (DATA, END, ERROR):
                raise BenchError("protocol_error", "Invalid WAV frame header", "fail")
            payload = _read_exact(serial, pending, length, clock, deadline)
            if flags == ERROR:
                if zlib.crc32(payload) & 0xffffffff != frame_crc:
                    raise BenchError("transfer_crc", "Corrupt WAV error frame", "fail")
                raise BenchError("wav_transfer", payload.decode("utf-8", errors="replace"), "fail")
            if flags == END:
                if length or expected != size or frame_crc != whole_crc:
                    raise BenchError("transfer_crc", "WAV final size or CRC does not match", "fail")
                completed = True
                break
            if not length or expected + length > size or \
               zlib.crc32(payload) & 0xffffffff != frame_crc:
                raise BenchError("transfer_crc", "WAV data frame CRC or extent does not match", "fail")
            stream.write(payload)
            sha256.update(payload)
            whole_crc = zlib.crc32(payload, whole_crc) & 0xffffffff
            expected += length
            frames += 1
            if expected >= next_flush:
                stream.flush()
                next_flush += 1024 * 1024
            _write(serial, f"WAV ACK {expected}\n".encode("ascii"))
        stream.flush()
        stream.close()
        stream = None
        partial.replace(final)
    except BaseException:
        if transfer_started and not completed:
            try:
                _write(serial, b"WAV ABORT\n")
            except BaseException:
                pass
        raise
    finally:
        if stream is not None:
            stream.close()
    transfer = {"file": name, "bytes": size, "frames": frames,
                "crc32": f"{whole_crc:08x}", "sha256": sha256.hexdigest(),
                "duration_ms": round((clock.now() - started) * 1000)}
    records.emit({"kind": "wav_end", **transfer, "host_receive_ns": time.time_ns()})
    return final, transfer


def _session_io(options, profile, run, metrics, artifacts, log="wav-transfer.jsonl"):
    clock = fake.Clock() if options["simulate"] else serial_io.Clock()
    shared = options.get("sim_state", {})
    serial = fake.Serial(clock, "device", options["scenario"], shared) if options["simulate"] \
        else serial_io.open_serial(metrics["selected"]["device"]["port"])
    records = JsonLines(run, log, profile["limits"]["diag_bytes"])
    artifacts["transfer"] = str(run.path / log)
    deadline = clock.now() + max(0, options["deadline"] - time.monotonic())
    return clock, serial, records, deadline


def run(options, profile, run, metrics, artifacts):
    clock, serial, records, deadline = _session_io(options, profile, run, metrics, artifacts)
    pending = bytearray()
    completed = False
    try:
        serial_io.settle(serial, records.emit, clock,
                         min(clock.now() + 14.0, deadline - 1.0))
        final, transfer = _transfer(serial, pending, records, clock, deadline, run,
                                    options["filename"], run.path / "audio", 44)
        completed = True
        artifacts["wav"] = str(final)
        metrics["transfer"] = transfer
        metrics["wav"] = _analyze(final)
        metrics.update(target_health="healthy", final_target_state="running",
                       human_required=False)
    except BaseException as exc:
        metrics.setdefault("human_required", False)
        metrics["target_health"] = ("failed" if isinstance(exc, BenchError) and
                                    exc.result == "fail" else "not_checked")
        metrics["final_target_state"] = "running" if completed else "unknown"
        raise
    finally:
        records.close()
        serial.close()


def run_manifest(options, profile, run, metrics, artifacts):
    """Fetch and parse one Field session's MANIFEST.TXT."""
    clock, serial, records, deadline = _session_io(options, profile, run, metrics, artifacts,
                                                   "manifest-transfer.jsonl")
    pending = bytearray()
    completed = False
    try:
        serial_io.settle(serial, records.emit, clock,
                         min(clock.now() + 14.0, deadline - 1.0))
        name = f"FIELD/{options['session']}/MANIFEST.TXT"
        final, transfer = _transfer(serial, pending, records, clock, deadline, run,
                                    name, run.path / "session", 1)
        completed = True
        artifacts["manifest"] = str(final)
        metrics["transfer"] = transfer
        if transfer["bytes"] > MANIFEST_MAX_BYTES:
            raise BenchError("manifest_invalid", "Manifest exceeds 1,024 bytes", "fail")
        try:
            text = final.read_bytes().decode("ascii")
        except UnicodeDecodeError:
            raise BenchError("manifest_invalid", "Manifest is not ASCII", "fail")
        manifest = parse_manifest(text)
        metrics["manifest"] = manifest
        if manifest.get("format") != "spooky-session" or manifest.get("format_version") != "1":
            raise BenchError("manifest_invalid", "Not a version 1 session manifest", "fail")
        metrics.update(target_health="healthy", final_target_state="running",
                       human_required=False)
    except BaseException as exc:
        metrics.setdefault("human_required", False)
        metrics["target_health"] = ("failed" if isinstance(exc, BenchError) and
                                    exc.result == "fail" else "not_checked")
        metrics["final_target_state"] = "running" if completed else "unknown"
        raise
    finally:
        records.close()
        serial.close()


def run_events(options, profile, run, metrics, artifacts):
    """Fetch, decode and check one Field session's EVENTS.BIN."""
    clock, serial, records, deadline = _session_io(options, profile, run, metrics, artifacts,
                                                   "events-transfer.jsonl")
    pending = bytearray()
    completed = False
    try:
        serial_io.settle(serial, records.emit, clock,
                         min(clock.now() + 14.0, deadline - 1.0))
        name = f"FIELD/{options['session']}/EVENTS.BIN"
        final, transfer = _transfer(serial, pending, records, clock, deadline, run,
                                    name, run.path / "session", session_events.SIZE)
        completed = True
        artifacts["events"] = str(final)
        metrics["transfer"] = transfer
        header, decoded = session_events.decode(final.read_bytes())
        dump = run.path / "session" / (final.name + ".jsonl")
        with dump.open("x", encoding="utf-8") as stream:
            for record in decoded:
                stream.write(json.dumps(record) + "\n")
        artifacts["events_dump"] = str(dump)
        summary = session_events.summarize(decoded)
        metrics["events"] = dict(summary, header=header)
        if summary["problems"]:
            raise BenchError("events_invalid", "; ".join(summary["problems"]), "fail")
        metrics.update(target_health="healthy", final_target_state="running",
                       human_required=False)
    except BaseException as exc:
        metrics.setdefault("human_required", False)
        metrics["target_health"] = ("failed" if isinstance(exc, BenchError) and
                                    exc.result == "fail" else "not_checked")
        metrics["final_target_state"] = "running" if completed else "unknown"
        raise
    finally:
        records.close()
        serial.close()
