"""Bounded recording workload with live two-core IPC progress checks."""
import base64
import re
import time
from . import boot_smoke, fake, serial_io
from .artifacts import JsonLines
from .result import BenchError

START = re.compile(r"^OK RECORD START file=(REC[0-9]{3}\.WAV) duration=([0-9]+)s "
                   r"format=48000Hz/16-bit/3ch \[radio-L,radio-R,mic\]$")
PROGRESS = re.compile(r"^RECORD progress=([0-9]+)\.([0-9])s "
                      r"queues=([0-9]+)/([0-9]+),([0-9]+)/([0-9]+) "
                      r"max-write=([0-9]+)ms$")
PASS = re.compile(r"^OK RECORD PASS file=(REC[0-9]{3}\.WAV) frames=([0-9]+) "
                  r"bytes=([0-9]+) audio=([0-9]+)\.([0-9]{3})s elapsed=([0-9]+)ms"
                  r"(?: reason=(.+))?$")
DIAG = re.compile(r"^RECORD DIAG queues radio=([0-9]+)/([0-9]+) "
                  r"pdm=([0-9]+)/([0-9]+) max-write=([0-9]+)ms "
                  r"peaks=([0-9]+),([0-9]+),([0-9]+)"
                  r"(?: margin=(OK|LOW))?$")  # margin: decision 0014; absent before it
IDLE = re.compile(r"^OK RECORD IDLE last-file=([^ ]+) frames=([0-9]+) "
                  r"max-write=([0-9]+)ms$")


def _record(raw, complete):
    if complete:
        try:
            initial_text = raw.decode("ascii").strip()
        except UnicodeDecodeError:
            initial_text = ""
        if initial_text.startswith("ERR RECORD"):
            return {"kind": "ERR RECORD", "text": raw.decode("utf-8", errors="replace"),
                    "complete_line": True,
                    "raw_base64": base64.b64encode(raw).decode("ascii"),
                    "host_receive_ns": time.time_ns()}
    ipc = boot_smoke._record(raw, complete)
    if ipc["kind"] != "text":
        return ipc
    if not complete:
        return ipc
    try:
        text = raw.decode("ascii").strip()
    except UnicodeDecodeError:
        return ipc
    value = {"kind": "text", "text": raw.decode("utf-8", errors="replace"),
             "complete_line": True, "raw_base64": base64.b64encode(raw).decode("ascii"),
             "host_receive_ns": time.time_ns()}
    match = START.fullmatch(text)
    if match:
        value.update(kind="OK RECORD START", fields={"file": match[1],
                     "duration_s": int(match[2])})
        return value
    match = PROGRESS.fullmatch(text)
    if match:
        value.update(kind="RECORD progress", fields={"audio_s": int(match[1]) + int(match[2]) / 10,
            "radio_queue": int(match[3]), "radio_depth": int(match[4]),
            "pdm_queue": int(match[5]), "pdm_depth": int(match[6]),
            "max_write_ms": int(match[7])})
        return value
    match = PASS.fullmatch(text)
    if match:
        value.update(kind="OK RECORD PASS", fields={"file": match[1], "frames": int(match[2]),
            "bytes": int(match[3]), "audio_ms": int(match[4]) * 1000 + int(match[5]),
            "elapsed_ms": int(match[6]), "reason": match[7]})
        return value
    match = DIAG.fullmatch(text)
    if match:
        value.update(kind="RECORD DIAG", fields={"radio_high_water": int(match[1]),
            "radio_depth": int(match[2]), "pdm_high_water": int(match[3]),
            "pdm_depth": int(match[4]), "max_write_ms": int(match[5]),
            "peaks": [int(match[6]), int(match[7]), int(match[8])],
            "storage_margin": match[9]})
        return value
    match = IDLE.fullmatch(text)
    if match:
        value.update(kind="OK RECORD IDLE", fields={"last_file": match[1],
                     "frames": int(match[2]), "max_write_ms": int(match[3])})
        return value
    if text.startswith("OK RECORD ACTIVE "):
        value["kind"] = "OK RECORD ACTIVE"
        return value
    if text.startswith("ERR RECORD"):
        value["kind"] = "ERR RECORD"
    elif text.startswith("OK RECORD STOP") or text == "OK RECORD already idle":
        value["kind"] = "OK RECORD STOP"
    return value


def _open(selected, options, clock, shared):
    if options["simulate"]:
        return fake.Serial(clock, "device", options["scenario"], shared)
    return serial_io.open_serial(selected["device"]["port"])


def _write(serial, wire):
    if serial.write(wire) != len(wire):
        raise OSError("short command write")


def _check_log(answer):
    fields = answer["fields"]
    bad = {key: fields[key] for key in boot_smoke.LOG_ERRORS if fields[key] != 0}
    if bad:
        raise BenchError("logger_unhealthy", "Logger loss/error counters are nonzero", "fail")


def _check_recording(requested, start, passed, diag, progress_count):
    if start["duration_s"] != requested or start["file"] != passed["file"]:
        raise BenchError("recording_protocol", "Recording identity or duration changed", "fail")
    if passed["bytes"] != passed["frames"] * 6:
        raise BenchError("recording_protocol", "WAV byte/frame accounting is inconsistent", "fail")
    if not requested * 1000 <= passed["audio_ms"] <= requested * 1000 + 100:
        raise BenchError("recording_duration", "Recorded audio duration is outside one block", "fail")
    if not passed["audio_ms"] <= passed["elapsed_ms"] <= passed["audio_ms"] + 5000:
        raise BenchError("recording_duration", "Wall/audio duration relationship is invalid", "fail")
    if not diag["radio_depth"] or not diag["pdm_depth"] or \
       diag["radio_high_water"] >= diag["radio_depth"] or \
       diag["pdm_high_water"] >= diag["pdm_depth"]:
        raise BenchError("recorder_overrun", "Recorder queue high-water reached its capacity", "fail")
    if progress_count < max(1, requested // 5 - 1):
        raise BenchError("recording_evidence", "Too few progress reports were observed", "fail")


def _recording_session(selected, options, clock, shared, emit, baseline_ipc, metrics):
    from spookyprobe.protocol import Lines
    serial = _open(selected, options, clock, shared)
    pending = Lines()
    requested = options["seconds"]
    summary = {"requested_seconds": requested, "progress_count": 0,
               "max_observed_queues": {"radio": 0, "pdm": 0},
               "max_observed_write_ms": 0, "cleanup": "not_needed"}
    metrics["recording"] = summary
    ipc_samples = []
    possible_active = False
    awaiting_ipc = False
    start_fields = pass_fields = diag_fields = None
    record_start_clock = None
    # Avoid the target's five-second progress cadence so its best-effort USB
    # response is not requested while a progress line is still in flight.
    schedule = [requested / 3.0 + 1.3,
                min(requested - 1.0, requested * 2.0 / 3.0 + 1.3)]
    schedule_index = 0
    last_ipc = baseline_ipc
    failure = None

    def consume(record):
        nonlocal possible_active, awaiting_ipc, start_fields, pass_fields
        nonlocal diag_fields, record_start_clock, schedule_index, last_ipc
        kind = record["kind"]
        if record.get("error"):
            raise BenchError("protocol_error", record["error"], "fail")
        if kind == "OK RECORD START":
            if start_fields is not None:
                raise BenchError("recording_protocol", "Duplicate RECORD START", "fail")
            start_fields = record["fields"]
            summary["start"] = start_fields
            record_start_clock = clock.now()
            possible_active = True
        elif kind == "RECORD progress":
            fields = record["fields"]
            summary["progress_count"] += 1
            summary["last_progress_audio_s"] = fields["audio_s"]
            summary["max_observed_queues"]["radio"] = max(
                summary["max_observed_queues"]["radio"], fields["radio_queue"])
            summary["max_observed_queues"]["pdm"] = max(
                summary["max_observed_queues"]["pdm"], fields["pdm_queue"])
            summary["max_observed_write_ms"] = max(summary["max_observed_write_ms"],
                                                    fields["max_write_ms"])
        elif kind == "OK IPC DISABLED":
            raise BenchError("ipc_disabled", "Target reports IPC smoke support disabled", "fail")
        elif kind == "OK IPC":
            if not awaiting_ipc:
                raise BenchError("protocol_error", "Unsolicited or duplicate IPC response", "fail")
            boot_smoke._check_ipc(record)
            delta = boot_smoke._progress(last_ipc, record)
            if not delta:
                raise BenchError("ipc_no_progress", "IPC stalled during recording", "fail")
            ipc_samples.append({"phase": f"recording-{len(ipc_samples) + 1}",
                                "response": record, "delta": delta})
            last_ipc = record
            awaiting_ipc = False
        elif kind == "OK RECORD PASS":
            if pass_fields is not None:
                raise BenchError("recording_protocol", "Duplicate RECORD PASS", "fail")
            pass_fields = record["fields"]
            summary["result"] = pass_fields
            possible_active = False
        elif kind == "RECORD DIAG":
            diag_fields = record["fields"]
            summary["diagnostics"] = diag_fields
        elif kind == "ERR RECORD":
            possible_active = False
            raise BenchError("recording_aborted", record["text"].strip(), "fail")
        elif kind == "ERR":
            raise BenchError("protocol_error", record["text"].strip(), "fail")

    emit({"kind": "session_start", "command": "RECORD START + IPC STATUS",
          "host_receive_ns": time.time_ns()})
    try:
        serial_io.settle(serial, emit, clock,
                         boot_smoke._remaining_deadline(clock, options["deadline"], 14.0))
        _write(serial, b"RECORD STATUS\n")
        status_end = clock.now() + 8.0
        idle = None
        while clock.now() < status_end and idle is None:
            for raw, complete in pending.feed(serial.read(4096)):
                record = _record(raw, complete)
                emit(record)
                if record["kind"] == "OK RECORD ACTIVE":
                    summary["initial_status"] = "active"
                    raise BenchError("recording_busy", "A recording is already active")
                if record["kind"] == "OK RECORD IDLE":
                    idle = record["fields"]
                elif record["kind"] in ("ERR", "ERR RECORD"):
                    raise BenchError("protocol_error", record["text"].strip(), "fail")
        if idle is None:
            raise BenchError("request_timeout", "RECORD STATUS response deadline exceeded")
        summary["initial_status"] = idle
        _write(serial, f"RECORD START {requested}\n".encode("ascii"))
        possible_active = True  # The command may have started even if its reply is lost.
        record_deadline = clock.now() + min(requested + 15.0,
            max(0, options["deadline"] - time.monotonic()))
        while clock.now() < record_deadline:
            data = serial.read(4096)
            for raw, complete in pending.feed(data):
                record = _record(raw, complete)
                emit(record)
                consume(record)
            if start_fields is not None and pass_fields is None and not awaiting_ipc and \
               schedule_index < len(schedule) and \
               clock.now() - record_start_clock >= schedule[schedule_index]:
                _write(serial, b"IPC STATUS\n")
                awaiting_ipc = True
                schedule_index += 1
            if pass_fields is not None and diag_fields is not None and not awaiting_ipc:
                break
        if start_fields is None:
            raise BenchError("request_timeout", "RECORD START response deadline exceeded")
        if awaiting_ipc:
            raise BenchError("ipc_evidence_incomplete", "IPC response was lost during recording", "fail")
        if pass_fields is None or diag_fields is None:
            raise BenchError("recording_timeout", "Recording did not supply PASS and diagnostics", "fail")
        if len(ipc_samples) != len(schedule):
            raise BenchError("ipc_evidence_incomplete", "Missing IPC sample during recording", "fail")
        _check_recording(requested, start_fields, pass_fields, diag_fields,
                         summary["progress_count"])
    except BaseException as exc:
        failure = exc
    finally:
        if possible_active:
            try:
                _write(serial, b"RECORD STOP\n")
                cleanup_end = clock.now() + min(4.0,
                    max(0, options["deadline"] - time.monotonic()))
                while clock.now() < cleanup_end and possible_active:
                    for raw, complete in pending.feed(serial.read(4096)):
                        record = _record(raw, complete)
                        emit(record)
                        if record["kind"] in ("OK RECORD PASS", "ERR RECORD"):
                            possible_active = False
                summary["cleanup"] = "stopped" if not possible_active else "incomplete"
            except BaseException as cleanup_exc:
                summary["cleanup"] = "failed"
                summary["cleanup_error"] = str(cleanup_exc)[:500]
        try:
            for raw, complete in pending.finish():
                emit(_record(raw, complete))
        except BaseException as evidence_exc:
            if failure is None:
                failure = evidence_exc
        try:
            serial.close()
        except BaseException as close_exc:
            summary["cleanup"] = "failed"
            if failure is None:
                failure = close_exc
        try:
            emit({"kind": "session_end", "host_receive_ns": time.time_ns()})
        except BaseException as evidence_exc:
            if failure is None:
                failure = evidence_exc
    if failure:
        metrics["human_required"] = (summary["cleanup"] in ("failed", "incomplete") or
            summary.get("initial_status") == "active")
        raise failure
    metrics["human_required"] = False
    return ipc_samples, last_ipc


def run(options, profile, run, metrics, artifacts, evidence="diagnostics.jsonl"):
    metrics["test"] = {"name": "ipc-load", "criterion": "IPC progress during recording"}
    metrics["checks"] = {}
    metrics["final_target_state"] = "running"
    clock = fake.Clock() if options["simulate"] else serial_io.Clock()
    shared = options.get("sim_state", {})
    selected = metrics["selected"]
    artifacts["diagnostics"] = str(run.path / evidence)
    lines = JsonLines(run, evidence, profile["limits"]["diag_bytes"])
    failure = None
    try:
        baseline_diag = boot_smoke._query(profile, selected, options, clock, shared,
                                          lines.emit, "DIAG STATUS")
        boot_smoke._check_diag(baseline_diag)
        baseline_log = boot_smoke._query(profile, selected, options, clock, shared,
                                         lines.emit, "LOG STATUS")
        _check_log(baseline_log)
        baseline_ipc = boot_smoke._query(profile, selected, options, clock, shared,
                                         lines.emit, "IPC STATUS")
        boot_smoke._check_ipc(baseline_ipc)
        metrics["checks"]["baseline_health"] = "pass"

        samples, last_ipc = _recording_session(selected, options, clock, shared,
                                               lines.emit, baseline_ipc, metrics)
        metrics["checks"].update(recording="pass", ipc_under_load="pass")

        post_ipc = boot_smoke._query(profile, selected, options, clock, shared,
                                     lines.emit, "IPC STATUS")
        boot_smoke._check_ipc(post_ipc)
        post_delta = boot_smoke._progress(last_ipc, post_ipc)
        if not post_delta:
            raise BenchError("ipc_no_progress", "IPC did not progress after recording", "fail")
        metrics["ipc"] = {"snapshots": [{"phase": "before", "response": baseline_ipc},
            *samples, {"phase": "after", "response": post_ipc, "delta": post_delta}],
            "liveness": "M4 inferred from progress before, during, and after recording"}

        final_diag = boot_smoke._query(profile, selected, options, clock, shared,
                                       lines.emit, "DIAG STATUS")
        boot_smoke._check_diag(final_diag)
        final_log = boot_smoke._query(profile, selected, options, clock, shared,
                                      lines.emit, "LOG STATUS")
        _check_log(final_log)
        last = boot_smoke._query(profile, selected, options, clock, shared,
                                 lines.emit, "DIAG LAST")
        dump = boot_smoke._query(profile, selected, options, clock, shared,
                                 lines.emit, "DIAG DUMP")
        if not dump["complete"] or dump["gaps"]:
            raise BenchError("dump_incomplete", "Diagnostic dump is incomplete or contains gaps", "fail")
        metrics["diagnostics"] = {"baseline_status": baseline_diag,
            "baseline_log": baseline_log, "final_status": final_diag,
            "final_log": final_log, "last": last, "dump": dump}
        metrics["checks"].update(post_load_health="pass", diagnostic_history="pass")
    except BaseException as exc:
        failure = exc
    finally:
        try:
            lines.close()
        except BaseException as exc:
            if failure is None:
                failure = exc
    if failure:
        metrics["target_health"] = "failed"
        if "human_required" not in metrics:
            metrics["human_required"] = False
        preexisting_recording = metrics.get("recording", {}).get("initial_status") == "active"
        metrics["final_target_state"] = ("running" if preexisting_recording else
            "unknown" if metrics["human_required"] or
            metrics["checks"].get("baseline_health") != "pass" else "running")
        raise failure
    metrics["target_health"] = "healthy"
    metrics["final_target_state"] = "running"
    metrics["human_required"] = False
