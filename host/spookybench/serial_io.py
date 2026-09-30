"""Adapters around the pinned capture/decoder; one short command per session."""
import base64
import re
import time
from .result import BenchError


class Clock:
    now = staticmethod(time.monotonic)
    sleep = staticmethod(time.sleep)


def target_identity(answer):
    fields = answer.get("fields", {})
    required = {"V", "CORE", "BUILD", "BOOT", "RESET", "CAPS"}
    if not required <= fields.keys():
        raise ValueError("incomplete identity response")
    if fields["V"] != 1 or fields["CORE"] != 7:
        raise ValueError("unsupported target identity schema/core")
    if not isinstance(fields["BUILD"], str) or not re.fullmatch(
            r"[A-Za-z0-9_.-]{1,128}", fields["BUILD"]):
        raise ValueError("invalid target build identity")
    for key in ("BOOT", "RESET", "CAPS"):
        if not isinstance(fields[key], int) or not 0 <= fields[key] <= 0xffffffff:
            raise ValueError("invalid target identity field " + key)
    if fields["BOOT"] == 0 or fields["CAPS"] & 0x3 != 0x3:
        raise ValueError("target lacks identity/boot-epoch capabilities")
    return {"state": "observed", "schema_version": fields["V"],
            "core": fields["CORE"], "build_id": fields["BUILD"],
            "boot_epoch": fields["BOOT"], "reset_flags": fields["RESET"],
            "capabilities": fields["CAPS"]}


def _identity_request(serial, emit, clock, timeout):
    """Request the additive identity command absent from pinned spookyprobe 0.1.0."""
    from spookyprobe.protocol import Lines, parse_line

    wire = b"DIAG IDENTITY\n"
    if serial.write(wire) != len(wire):
        raise OSError("short command write")
    lines = Lines()
    deadline = clock.now() + timeout
    try:
        while clock.now() < deadline:
            for raw, complete in lines.feed(serial.read(4096)):
                record = parse_line(raw, complete)
                if complete:
                    try:
                        text = raw.decode("ascii").strip()
                    except UnicodeDecodeError:
                        text = ""
                    if text == "OK IDENTITY" or text.startswith("OK IDENTITY "):
                        fields = {}
                        for token in text[len("OK IDENTITY"):].split():
                            if "=" not in token:
                                continue
                            key, value = token.split("=", 1)
                            if key in fields:
                                record["error"] = "duplicate field " + key
                            fields[key] = (int(value) if key != "BUILD" and
                                           value.isascii() and value.isdecimal()
                                           else value)
                        record.update(kind="OK IDENTITY", fields=fields)
                    elif text.startswith("ERR unknown command"):
                        record.update(kind="ERR IDENTITY", text=text)
                    elif text.startswith("ERR "):
                        record.update(kind="ERR IDENTITY", text=text)
                record["raw_base64"] = base64.b64encode(raw).decode("ascii")
                record["host_receive_ns"] = time.time_ns()
                emit(record)
                if record.get("error"):
                    raise ValueError(record["error"])
                if text.startswith("ERR unknown command"):
                    raise BenchError("identity_unsupported",
                                     "Target firmware does not support DIAG IDENTITY",
                                     "unsupported")
                if record["kind"].startswith("ERR ") or text.startswith("ERR "):
                    raise RuntimeError(record["text"].strip())
                if record["kind"] == "OK IDENTITY":
                    return record
        raise TimeoutError("diagnostic response deadline exceeded")
    finally:
        for raw, complete in lines.finish():
            record = parse_line(raw, complete)
            record["raw_base64"] = base64.b64encode(raw).decode("ascii")
            record["host_receive_ns"] = time.time_ns()
            emit(record)


def open_serial(port):
    try:
        import serial
        return serial.Serial(port, baudrate=115200, timeout=0.1, write_timeout=1,
                             bytesize=8, parity="N", stopbits=1, rtscts=False, dsrdtr=False)
    except ImportError as exc:
        raise BenchError("dependency_missing", "Install pyserial 3.5") from exc
    except OSError as exc:
        raise BenchError("serial_open", f"Cannot open {port}: {exc}") from exc


def capture(serial, run, seconds, clock, stats=None, stop=None, ready=None):
    from spookyprobe.capture import Archive, capture_session

    class CappedArchive(Archive):
        def __init__(self):
            self.received_bytes = 0
            self.accepted_bytes = 0
            run.reserve(16384)  # Session metadata and filesystem completion reserve.
            super().__init__(run.path / "uart", {"transport": "probe UART",
                "execution": run.execution, "start_host_ns": time.time_ns()})

        def submit(self, data, host_ns=None, monotonic_ns=None):
            self.received_bytes += len(data)
            if self.received_bytes > run.limits["uart_bytes"]:
                raise BenchError("artifact_limit", "UART byte cap reached", "fail")
            # Reserve before handing bytes to the asynchronous writer. Index rows
            # are under 512 bytes; rejected chunks conservatively consume budget.
            run.reserve(len(data) + 512)
            accepted = super().submit(data, host_ns, monotonic_ns)
            if accepted:
                self.accepted_bytes += len(data)
            return accepted

    archive = CappedArchive()
    if ready is not None:
        ready.set()
    end = clock.now() + seconds
    if stats is None:
        stats = {}
    try:
        capture_session(serial, archive, lambda: clock.now() >= end or (stop is not None and stop.is_set()))
    finally:
        stats.update({"received_bytes": archive.received_bytes, "archived_bytes": archive.accepted_bytes,
             "host_dropped_bytes": archive.dropped_bytes, "host_dropped_chunks": archive.dropped_chunks,
             "driver_loss": "unknown", "probe_loss": "unknown", "target_loss": "unknown",
             "timestamp_source": "host read completion", "target_health": "not_checked"})
    if archive.dropped_bytes:
        raise BenchError("capture_loss", f"Host queue dropped {archive.dropped_bytes} bytes", "fail")
    return stats


def diagnostics(serial, command, emit, clock, deadline):
    from spookyprobe.client import Client
    emit({"kind": "session_start", "command": command, "host_receive_ns": time.time_ns()})
    settle(serial, emit, clock, deadline)
    remaining = deadline - clock.now()
    if remaining <= 0:
        raise BenchError("request_timeout", "No command budget remains")
    timeout = min(8.0, remaining)
    request_end = clock.now() + timeout
    client = None
    try:
        if command == "DIAG IDENTITY":
            answer = _identity_request(serial, emit, clock, timeout)
        else:
            client = Client(serial, emit, clock=clock.now, timeout=timeout)
            answer = client.request(command)
        if clock.now() > min(deadline, request_end):
            raise BenchError("request_timeout", "Late response exceeded operation budget")
        # The upstream parser preserves fields; validate required status fields too.
        fields = answer.get("fields", {})
        required = {
            "LOG STATUS": {"QUEUED", "PEAK", "DROP_WRITES", "DROP_BYTES", "TX_LOST", "TX_BYTES", "TX_ERRORS", "CONTEXT", "FLIGHT"},
            "DIAG STATUS": {"V", "CORE", "COUNT", "OVERWRITTEN", "SD_MAX_MS", "LOOP_MAX_MS", "RADIO_OVR", "PDM_OVR", "SD_ERR", "AUDIO_ERR", "HAS_FAULT"},
            "DIAG IDENTITY": {"V", "CORE", "BUILD", "BOOT", "RESET", "CAPS"}}
        if command in required and not required[command] <= fields.keys():
            raise ValueError("incomplete status response")
        if command == "DIAG LAST" and answer.get("text", "").strip() != "OK DIAG LAST NONE":
            if not {"SEQ", "MS", "EVENT", "A", "B"} <= fields.keys() or not isinstance(fields["EVENT"], str):
                raise ValueError("incomplete last-fault response")
        if command == "DIAG IDENTITY":
            target_identity(answer)
        return answer
    except TimeoutError as exc:
        raise BenchError("request_timeout", str(exc)) from exc
    except (ValueError, RuntimeError) as exc:
        raise BenchError("protocol_error", str(exc), "fail") from exc
    finally:
        if client is not None:
            client.finish()
        emit({"kind": "session_end", "host_receive_ns": time.time_ns()})


def settle(serial, emit, clock, deadline):
    """Apply the CDC quiet rule and archive everything preceding one request."""
    from spookyprobe.protocol import Lines, parse_line
    pending = Lines()
    quiet = clock.now()
    settle_until = quiet + 5.2
    # Reserve time for the request; never let incoming text extend the deadline.
    sync_end = min(deadline - 1.0, quiet + 6.5)
    try:
        while clock.now() < settle_until or clock.now() - quiet < 0.3:
            if clock.now() >= sync_end:
                raise BenchError("sync_timeout", "Device CDC did not become quiet; stop other streams")
            data = serial.read(4096)
            if data:
                quiet = clock.now()
                for raw, complete in pending.feed(data):
                    emit(dict(parse_line(raw, complete), raw_base64=base64.b64encode(raw).decode("ascii"),
                              phase="before_commands", host_receive_ns=time.time_ns()))
    finally:
        for raw, complete in pending.finish():
            emit(dict(parse_line(raw, complete), raw_base64=base64.b64encode(raw).decode("ascii"),
                      phase="before_commands", host_receive_ns=time.time_ns()))
