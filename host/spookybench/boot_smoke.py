"""Phase-2 paired boot, diagnostic, and interprocessor smoke test."""
import base64
import time
from . import fake, openocd, serial_io
from .artifacts import JsonLines
from .firmware import validate_pair
from .platform_io import ports, select_devices
from .result import BenchError

MASK = 0xffffffff
IPC_NUMERIC = {"VERSION", "PEER_VERSION", "TX", "RX", "ACK", "ROUNDTRIPS",
               "PEER_SEEN", "ACK_SEEN", "RX_AGE", "ACK_AGE", "ERROR",
               "PEER_ERROR", "BUSY"}
IPC_REQUIRED = IPC_NUMERIC | {"LINK"}
DIAG_ERRORS = ("RADIO_OVR", "PDM_OVR", "SD_ERR", "AUDIO_ERR", "HAS_FAULT")
LOG_ERRORS = ("DROP_WRITES", "DROP_BYTES", "TX_LOST", "TX_ERRORS", "CONTEXT")


def _stage(metrics, name, started, result="pass"):
    metrics.setdefault("stages", []).append({"name": name, "result": result,
        "duration_ms": max(0, round((time.monotonic() - started) * 1000))})


def _record(raw, complete):
    record = {"kind": "text", "text": raw.decode("utf-8", errors="replace"),
              "complete_line": complete, "raw_base64": base64.b64encode(raw).decode("ascii"),
              "host_receive_ns": time.time_ns()}
    if not complete:
        return record
    try:
        text = raw.decode("ascii").strip()
    except UnicodeDecodeError:
        return record
    if text.startswith("OK IPC DISABLED"):
        record["kind"] = "OK IPC DISABLED"
        return record
    if not text.startswith("OK IPC "):
        if text.startswith("ERR "):
            record["kind"] = "ERR"
        return record
    fields = {}
    error = None
    for token in text[7:].split():
        if "=" not in token:
            error = "unexpected IPC response token"
            continue
        key, value = token.split("=", 1)
        if key in fields:
            error = "duplicate field " + key
        if key in IPC_NUMERIC:
            if not value.isascii() or not value.isdecimal():
                error = "invalid unsigned field " + key
            else:
                value = int(value)
                if not 0 <= value <= MASK:
                    error = "invalid unsigned field " + key
        fields[key] = value
    if set(fields) != IPC_REQUIRED:
        error = "incomplete or unknown IPC status fields"
    record.update(kind="OK IPC", fields=fields)
    if error:
        record["error"] = error
    return record


def ipc_status(serial, emit, clock, deadline):
    """Issue one IPC STATUS request after the same quiet rule as diagnostics."""
    from spookyprobe.protocol import Lines
    command = "IPC STATUS"
    emit({"kind": "session_start", "command": command, "host_receive_ns": time.time_ns()})
    pending = Lines()
    try:
        serial_io.settle(serial, emit, clock, deadline)
        request_end = min(deadline, clock.now() + 8.0)
        wire = b"IPC STATUS\n"
        if serial.write(wire) != len(wire):
            raise OSError("short command write")
        while clock.now() < request_end:
            matches = []
            for raw, complete in pending.feed(serial.read(4096)):
                record = _record(raw, complete)
                emit(record)
                if record.get("error"):
                    raise BenchError("protocol_error", record["error"], "fail")
                if record["kind"] == "ERR":
                    raise BenchError("protocol_error", record["text"].strip(), "fail")
                if record["kind"] in ("OK IPC", "OK IPC DISABLED"):
                    matches.append(record)
            if len(matches) > 1:
                raise BenchError("protocol_error", "ambiguous IPC response", "fail")
            if matches:
                if matches[0]["kind"] == "OK IPC DISABLED":
                    raise BenchError("ipc_disabled", "Target reports IPC smoke support disabled", "fail")
                return matches[0]
        raise BenchError("request_timeout", "IPC response deadline exceeded")
    except BaseException as exc:
        for raw, complete in pending.finish():
            emit(_record(raw, complete))
        emit({"kind": "request_failed", "command": command, "error": str(exc),
              "host_receive_ns": time.time_ns()})
        raise
    finally:
        emit({"kind": "session_end", "host_receive_ns": time.time_ns()})


def _remaining_deadline(clock, overall, maximum):
    remaining = overall - time.monotonic()
    if remaining <= 0:
        raise BenchError("operation_timeout", "No boot-smoke budget remains")
    return clock.now() + min(maximum, remaining)


def _serial(profile, selected, options, clock, shared):
    if options["simulate"]:
        return fake.Serial(clock, "device", options["scenario"], shared)
    return serial_io.open_serial(selected["device"]["port"])


def _query(profile, selected, options, clock, shared, emit, command):
    serial = _serial(profile, selected, options, clock, shared)
    try:
        deadline = _remaining_deadline(clock, options["deadline"], 14.0)
        if command == "IPC STATUS":
            return ipc_status(serial, emit, clock, deadline)
        return serial_io.diagnostics(serial, command, emit, clock, deadline)
    finally:
        serial.close()


def _wait_target(profile, options, metrics):
    started = time.monotonic()
    deadline = min(options["deadline"] - 1.0, started + 45.0)
    attempts = 0
    last = None
    while time.monotonic() < deadline:
        attempts += 1
        inventory = fake.inventory(options["scenario"]) if options["simulate"] else ports()
        metrics["ports_after_reset"] = inventory
        try:
            selected = select_devices(profile, inventory, ("device",))
            if not options["simulate"]:
                probe = serial_io.open_serial(selected["device"]["port"])
                probe.close()
            metrics["rediscovery"] = {"attempts": attempts,
                "duration_ms": round((time.monotonic() - started) * 1000),
                "device": selected["device"]}
            return selected
        except BenchError as exc:
            if exc.reason not in ("device_missing", "serial_open"):
                raise
            last = exc
            if options["simulate"]:
                break
            time.sleep(min(0.25, max(0, deadline - time.monotonic())))
    metrics["rediscovery"] = {"attempts": attempts,
        "duration_ms": round((time.monotonic() - started) * 1000), "device": None}
    raise BenchError("target_cdc_timeout", str(last or "Target CDC did not enumerate"), "fail")


def _check_diag(answer):
    fields = answer["fields"]
    if fields["V"] != 1 or fields["CORE"] != 7:
        raise BenchError("diag_unhealthy", "Unexpected diagnostic schema/core", "fail")
    bad = {key: fields[key] for key in DIAG_ERRORS if fields[key] != 0}
    if bad:
        raise BenchError("diag_unhealthy", "Diagnostic error counters or fault are nonzero", "fail")


def _check_ipc(answer):
    fields = answer["fields"]
    if fields["LINK"] == "INCOMPATIBLE" or fields["VERSION"] != 1 or fields["PEER_VERSION"] != 1:
        raise BenchError("ipc_incompatible", "IPC ABI is not 1/1", "fail")
    if fields["LINK"] != "UP" or fields["PEER_SEEN"] != 1 or fields["ACK_SEEN"] != 1:
        raise BenchError("ipc_unhealthy", "IPC link is not UP with both peers seen", "fail")
    if fields["ERROR"] or fields["PEER_ERROR"]:
        raise BenchError("ipc_unhealthy", "IPC error fields are nonzero", "fail")


def _progress(first, second):
    deltas = {}
    for key in ("TX", "RX", "ACK", "ROUNDTRIPS"):
        delta = (second["fields"][key] - first["fields"][key]) & MASK
        if not 0 < delta < 0x80000000:
            return None
        deltas[key] = delta
    return deltas


def run(options, profile, run, metrics, artifacts):
    """Run the complete test while the caller owns board and artifact locks."""
    metrics["test"] = {"name": "boot-smoke", "criterion": "IpcSmoke paired boot"}
    metrics["checks"] = {}
    preflight = time.monotonic()
    manifest, _ = validate_pair(options["manifest"])
    if manifest["preset"] != "IpcSmoke":
        raise BenchError("ipc_disabled", "boot-smoke requires an IpcSmoke manifest", "fail")
    _stage(metrics, "manifest_preflight", preflight)

    capture = None
    shared = {}
    clock = fake.Clock() if options["simulate"] else serial_io.Clock()
    # Simulation still archives boot bytes, but never opens hardware or starts a tool.
    if options["simulate"]:
        artifacts["uart"] = str(run.path / "uart")
        metrics["capture"] = {}
        simulated_probe = fake.Serial(clock, "probe", options["scenario"], shared)
        try:
            serial_io.capture(simulated_probe, run, 0.3, clock, metrics["capture"])
        finally:
            simulated_probe.close()

    flash = time.monotonic()
    control_options = dict(options, required_preset="IpcSmoke")
    capture = openocd.control(control_options, profile, run, metrics, artifacts,
                              operation="flash", keep_capture=not options["simulate"])
    _stage(metrics, "paired_flash_reset", flash)
    metrics["checks"]["paired_flash"] = "pass"

    failure = None
    lines = None
    try:
        rediscovery = time.monotonic()
        selected = _wait_target(profile, options, metrics)
        metrics["selected_after_reset"] = selected
        _stage(metrics, "target_cdc_rediscovery", rediscovery)
        metrics["checks"]["target_cdc"] = "pass"
        structured = time.monotonic()

        artifacts["diagnostics"] = str(run.path / "diagnostics.jsonl")
        lines = JsonLines(run, "diagnostics.jsonl", profile["limits"]["diag_bytes"])

        diag = _query(profile, selected, options, clock, shared, lines.emit, "DIAG STATUS")
        _check_diag(diag)
        metrics["diagnostics"] = {"status": diag}
        metrics["checks"]["m7_liveness"] = "pass"

        ipc_started = clock.now()
        snapshots = []
        deltas = None
        attempts = 0
        # One pair takes two quiet-settled requests plus the required interval.
        # Never begin a pair that cannot fit the 30-second acquisition cap.
        while attempts < 3 and clock.now() - ipc_started <= 18.0:
            attempts += 1
            first = _query(profile, selected, options, clock, shared, lines.emit, "IPC STATUS")
            _check_ipc(first)
            clock.sleep(1.0)
            second = _query(profile, selected, options, clock, shared, lines.emit, "IPC STATUS")
            _check_ipc(second)
            snapshots.extend((first, second))
            deltas = _progress(first, second)
            if deltas:
                break
        metrics["ipc"] = {"snapshots": snapshots, "pairs_attempted": attempts,
                          "deltas": deltas, "liveness": "M4 inferred from echo/ack progress"}
        if not deltas:
            raise BenchError("ipc_no_progress", "IPC counters did not make unambiguous forward progress", "fail")
        metrics["checks"]["m4_ipc_liveness"] = "pass"

        log = _query(profile, selected, options, clock, shared, lines.emit, "LOG STATUS")
        bad_log = {key: log["fields"][key] for key in LOG_ERRORS if log["fields"][key] != 0}
        if bad_log:
            raise BenchError("logger_unhealthy", "Logger loss/error counters are nonzero", "fail")
        last = _query(profile, selected, options, clock, shared, lines.emit, "DIAG LAST")
        dump = _query(profile, selected, options, clock, shared, lines.emit, "DIAG DUMP")
        if not dump["complete"] or dump["gaps"]:
            raise BenchError("dump_incomplete", "Diagnostic dump is incomplete or contains gaps", "fail")
        metrics["diagnostics"].update(log_status=log, last=last, dump=dump)
        metrics["checks"].update(logger="pass", diagnostic_history="pass")
        _stage(metrics, "structured_health_checks", structured)
    except BaseException as exc:
        failure = exc
    finally:
        if lines:
            try:
                lines.close()
            except BaseException as exc:
                if failure is None:
                    failure = exc
        if capture:
            try:
                capture.close()
            except BaseException as exc:
                if failure is None:
                    failure = exc

    capture_metrics = metrics.get("capture", {})
    uart_good = (capture_metrics.get("archived_bytes", 0) > 0 and
                 capture_metrics.get("host_dropped_bytes", 0) == 0)
    metrics["checks"]["probe_uart"] = "pass" if uart_good else "fail"
    if failure is None and not uart_good:
        failure = BenchError("uart_evidence_missing", "Required probe UART evidence is absent or incomplete", "fail")
    if failure:
        metrics["target_health"] = "failed"
        m7_running = metrics["checks"].get("m7_liveness") == "pass"
        metrics["final_target_state"] = "running" if m7_running else "unknown"
        metrics["human_required"] = not m7_running
        raise failure
    metrics["target_health"] = "healthy"
    metrics["final_target_state"] = "running"
    metrics["human_required"] = False
