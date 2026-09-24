"""Bounded scratch-file write/read verification with target health checks."""
import base64
import re
import time
from . import boot_smoke, fake, ipc_load, serial_io
from .artifacts import JsonLines
from .result import BenchError

MIB = 1024 * 1024
STATUS = re.compile(r"^OK SD PRESENT=1 MOUNTED=1 TYPE=(SDHC/SDXC|SDSC|OTHER) "
                    r"CAPACITY=([0-9]+)MiB FREE=([0-9]+)MiB BLOCKS=([0-9]+) "
                    r"BUS=4 CLOCKDIV=([0-9]+)$")
START = re.compile(r"^OK SD STRESS START size=([0-9]+)MiB passes=([0-9]+) "
                   r"file=SDTEST\.BIN chunk=([0-9]+); do not remove card$")
PHASE = re.compile(r"^SD STRESS pass=([0-9]+)/([0-9]+) phase=(WRITE|VERIFY)$")
PASS = re.compile(r"^OK SD STRESS PASS size=([0-9]+)MiB passes=([0-9]+) "
                  r"written=([0-9]+) verified=([0-9]+) elapsed=([0-9]+)ms "
                  r"write-max=([0-9]+)ms read-max=([0-9]+)ms "
                  r"aggregate=([0-9]+)\.([0-9]+)MiB/s file-removed=1$")
STOP = re.compile(r"^OK SD STRESS STOP cleaned=1 written=([0-9]+) verified=([0-9]+)$")


def _record(raw, complete):
    record = {"kind": "text", "text": raw.decode("utf-8", errors="replace"),
              "complete_line": complete,
              "raw_base64": base64.b64encode(raw).decode("ascii"),
              "host_receive_ns": time.time_ns()}
    if not complete:
        return record
    try:
        text = raw.decode("ascii").strip()
    except UnicodeDecodeError:
        return record
    if text.startswith("ERR "):
        record["kind"] = "ERR"
        return record
    if text.startswith("OK RECORD ACTIVE"):
        record["kind"] = "OK RECORD ACTIVE"
        return record
    idle = ipc_load.IDLE.fullmatch(text)
    if idle:
        record.update(kind="OK RECORD IDLE", fields={"last_file": idle[1],
            "frames": int(idle[2]), "max_write_ms": int(idle[3])})
        return record
    match = STATUS.fullmatch(text)
    if match:
        record.update(kind="OK SD STATUS", fields={"type": match[1],
            "capacity_mib": int(match[2]), "free_mib": int(match[3]),
            "blocks": int(match[4]), "bus_width": 4, "clock_div": int(match[5])})
        return record
    match = START.fullmatch(text)
    if match:
        record.update(kind="OK SD STRESS START", fields={"size_mib": int(match[1]),
            "passes": int(match[2]), "file": "SDTEST.BIN", "chunk_bytes": int(match[3])})
        return record
    match = PHASE.fullmatch(text)
    if match:
        record.update(kind="SD STRESS phase", fields={"pass": int(match[1]),
            "passes": int(match[2]), "phase": match[3].lower()})
        return record
    match = PASS.fullmatch(text)
    if match:
        record.update(kind="OK SD STRESS PASS", fields={"size_mib": int(match[1]),
            "passes": int(match[2]), "written_bytes": int(match[3]),
            "verified_bytes": int(match[4]), "elapsed_ms": int(match[5]),
            "max_write_ms": int(match[6]), "max_read_ms": int(match[7]),
            "aggregate_mib_s": int(match[8]) + int(match[9]) / (10 ** len(match[9])),
            "file_removed": True})
        return record
    match = STOP.fullmatch(text)
    if match:
        record.update(kind="OK SD STRESS STOP", fields={"cleaned": True,
            "written_bytes": int(match[1]), "verified_bytes": int(match[2])})
        return record
    if text == "OK SD STRESS already idle":
        record["kind"] = "OK SD STRESS STOP"
    return record


def _write(serial, data):
    if serial.write(data) != len(data):
        raise OSError("short command write")


def _wait(serial, pending, inbox, emit, clock, deadline, kinds):
    while clock.now() < deadline:
        records = []
        if inbox:
            records.append(inbox.pop(0))
        else:
            for raw, complete in pending.feed(serial.read(4096)):
                record = _record(raw, complete)
                emit(record)
                records.append(record)
        for index, record in enumerate(records):
            if record["kind"] in ("ERR", "OK RECORD ACTIVE"):
                inbox.extend(records[index + 1:])
                return record
            if record["kind"] in kinds:
                inbox.extend(records[index + 1:])
                return record
    raise BenchError("request_timeout", "SD response deadline exceeded")


def _raise_target_error(record):
    text = record["text"].strip()
    if "already exists" in text:
        raise BenchError("sd_scratch_exists", text)
    if "no card" in text or "mount failed" in text:
        raise BenchError("sd_unavailable", text)
    if "insufficient or unknown free space" in text:
        raise BenchError("sd_capacity", text)
    if "cleanup failed" in text:
        raise BenchError("sd_cleanup", text, "fail")
    if "SD STRESS failed" in text:
        raise BenchError("sd_verify_failed", text, "fail")
    if "RECORD" in text or "recording" in text:
        raise BenchError("recording_busy", text)
    raise BenchError("protocol_error", text, "fail")


def _command(serial, pending, inbox, emit, clock, wire, kind, seconds=8.0):
    _write(serial, wire)
    answer = _wait(serial, pending, inbox, emit, clock, clock.now() + seconds, {kind})
    if answer["kind"] == "ERR":
        _raise_target_error(answer)
    if answer["kind"] == "OK RECORD ACTIVE":
        raise BenchError("recording_busy", "A recording is already active")
    return answer


def _check_result(requested_size, requested_passes, start, phases, result):
    if start != {"size_mib": requested_size, "passes": requested_passes,
                 "file": "SDTEST.BIN", "chunk_bytes": 16384}:
        raise BenchError("sd_protocol", "SD start parameters do not match the request", "fail")
    expected_phases = [{"pass": pass_number, "passes": requested_passes, "phase": phase}
        for pass_number in range(1, requested_passes + 1) for phase in ("write", "verify")]
    if phases != expected_phases:
        raise BenchError("sd_evidence", "SD write/verify phase evidence is incomplete", "fail")
    expected_bytes = requested_size * requested_passes * MIB
    if result["size_mib"] != requested_size or result["passes"] != requested_passes or \
       result["written_bytes"] != expected_bytes or result["verified_bytes"] != expected_bytes or \
       not result["file_removed"] or result["elapsed_ms"] <= 0:
        raise BenchError("sd_accounting", "SD result accounting or cleanup is inconsistent", "fail")


def _status_query(selected, options, clock, shared, emit):
    from spookyprobe.protocol import Lines
    serial = (fake.Serial(clock, "device", options["scenario"], shared)
              if options["simulate"] else serial_io.open_serial(selected["device"]["port"]))
    pending = Lines()
    inbox = []
    try:
        remaining = options["deadline"] - time.monotonic()
        if remaining <= 2.0:
            raise BenchError("operation_timeout", "No budget remains for final SD status")
        serial_io.settle(serial, emit, clock, clock.now() + min(7.0, remaining))
        return _command(serial, pending, inbox, emit, clock,
                        b"SD STATUS\n", "OK SD STATUS")["fields"]
    finally:
        serial.close()


def _session(selected, options, clock, shared, emit, metrics):
    from spookyprobe.protocol import Lines
    serial = (fake.Serial(clock, "device", options["scenario"], shared)
              if options["simulate"] else serial_io.open_serial(selected["device"]["port"]))
    pending = Lines()
    inbox = []
    summary = {"size_mib": options["size_mib"], "passes": options["passes"],
               "scratch_file": "SDTEST.BIN", "cleanup": "not_needed", "phases": []}
    metrics["sd"] = summary
    possible_active = False
    retained = False
    failure = None
    emit({"kind": "session_start", "command": "SD BASIC", "host_receive_ns": time.time_ns()})
    try:
        remaining = options["deadline"] - time.monotonic()
        if remaining <= 18.0:
            raise BenchError("operation_timeout", "Insufficient SD test and cleanup budget")
        serial_io.settle(serial, emit, clock, clock.now() + min(7.0, remaining - 10.0))
        recording = _command(serial, pending, inbox, emit, clock,
                             b"RECORD STATUS\n", "OK RECORD IDLE")
        summary["recording_status"] = recording["fields"]
        status = _command(serial, pending, inbox, emit, clock,
                          b"SD STATUS\n", "OK SD STATUS")
        summary["baseline_status"] = status["fields"]
        if status["fields"]["free_mib"] < options["size_mib"]:
            raise BenchError("sd_capacity", "Reported SD free space is below the requested scratch size")

        _write(serial, f"SD STRESS {options['size_mib']} {options['passes']}\n".encode("ascii"))
        possible_active = True
        stress_end = clock.now() + max(1.0, options["deadline"] - time.monotonic() - 8.0)
        start = result = None
        while clock.now() < stress_end and result is None:
            try:
                answer = _wait(serial, pending, inbox, emit, clock, stress_end,
                               {"OK SD STRESS START", "SD STRESS phase",
                                "OK SD STRESS PASS"})
            except BenchError as exc:
                if exc.reason == "request_timeout":
                    raise BenchError("sd_timeout",
                                     "SD stress test did not complete before its cleanup reserve") from exc
                raise
            kind = answer["kind"]
            if kind == "ERR":
                possible_active = False
                retained = "retained" in answer["text"] or "cleanup failed" in answer["text"] or \
                           "already exists" in answer["text"]
                _raise_target_error(answer)
            if kind == "OK SD STRESS START":
                if start is not None:
                    raise BenchError("sd_protocol", "Duplicate SD start response", "fail")
                start = answer["fields"]
                summary["start"] = start
            elif kind == "SD STRESS phase":
                summary["phases"].append(answer["fields"])
            else:
                result = answer["fields"]
                summary["result"] = result
                possible_active = False
                summary["cleanup"] = "target_removed"
        if result is None:
            raise BenchError("sd_timeout", "SD stress test did not complete before its cleanup reserve")
        if start is None:
            raise BenchError("sd_protocol", "Missing SD start response", "fail")
        _check_result(options["size_mib"], options["passes"], start,
                      summary["phases"], result)
    except BaseException as exc:
        failure = exc
    finally:
        if possible_active:
            try:
                _write(serial, b"SD STRESS STOP\n")
                cleanup_end = clock.now() + min(6.0, max(0, options["deadline"] - time.monotonic()))
                stopped = _wait(serial, pending, inbox, emit, clock, cleanup_end,
                                {"OK SD STRESS STOP"})
                if stopped["kind"] == "ERR":
                    _raise_target_error(stopped)
                summary["cleanup"] = "stopped_and_removed"
                summary["stop"] = stopped.get("fields", {})
                possible_active = False
            except BaseException as cleanup_exc:
                summary["cleanup"] = "failed"
                summary["cleanup_error"] = str(cleanup_exc)[:500]
        elif retained:
            summary["cleanup"] = "manual_required"
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
        metrics["human_required"] = summary["cleanup"] in ("failed", "manual_required")
        raise failure


def run(options, profile, run, metrics, artifacts):
    metrics["test"] = {"name": "sd-basic",
                       "criterion": "bounded scratch write/read/compare and cleanup"}
    metrics["checks"] = {}
    metrics["final_target_state"] = "running"
    clock = fake.Clock() if options["simulate"] else serial_io.Clock()
    shared = {}
    selected = metrics["selected"]
    artifacts["diagnostics"] = str(run.path / "diagnostics.jsonl")
    lines = JsonLines(run, "diagnostics.jsonl", profile["limits"]["diag_bytes"])
    failure = None
    try:
        baseline_diag = boot_smoke._query(profile, selected, options, clock, shared,
                                          lines.emit, "DIAG STATUS")
        boot_smoke._check_diag(baseline_diag)
        baseline_log = boot_smoke._query(profile, selected, options, clock, shared,
                                         lines.emit, "LOG STATUS")
        ipc_load._check_log(baseline_log)
        metrics["checks"]["baseline_health"] = "pass"

        _session(selected, options, clock, shared, lines.emit, metrics)
        metrics["checks"].update(sd_write="pass", sd_verify="pass", scratch_cleanup="pass")

        final_sd = _status_query(selected, options, clock, shared, lines.emit)
        baseline_sd = metrics["sd"]["baseline_status"]
        if any(final_sd[key] != baseline_sd[key] for key in ("type", "capacity_mib", "blocks")):
            raise BenchError("sd_card_changed", "SD identity changed during the test", "fail")
        if final_sd["free_mib"] + 1 < baseline_sd["free_mib"]:
            raise BenchError("sd_cleanup", "Scratch cleanup did not restore reported free space", "fail")
        metrics["sd"]["final_status"] = final_sd
        metrics["checks"]["card_identity"] = "pass"

        final_diag = boot_smoke._query(profile, selected, options, clock, shared,
                                       lines.emit, "DIAG STATUS")
        boot_smoke._check_diag(final_diag)
        final_log = boot_smoke._query(profile, selected, options, clock, shared,
                                      lines.emit, "LOG STATUS")
        ipc_load._check_log(final_log)
        metrics["diagnostics"] = {"baseline_status": baseline_diag,
            "baseline_log": baseline_log, "final_status": final_diag, "final_log": final_log}
        metrics["checks"]["post_test_health"] = "pass"
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
        metrics.setdefault("human_required", False)
        metrics["final_target_state"] = ("unknown" if metrics["human_required"] else "running")
        raise failure
    metrics.update(target_health="healthy", final_target_state="running", human_required=False)
