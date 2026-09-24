"""Adapters around the pinned capture/decoder; one short command per session."""
import base64
import time
from .result import BenchError


class Clock:
    now = staticmethod(time.monotonic)
    sleep = staticmethod(time.sleep)


def open_serial(port):
    try:
        import serial
        return serial.Serial(port, baudrate=115200, timeout=0.1, write_timeout=1,
                             bytesize=8, parity="N", stopbits=1, rtscts=False, dsrdtr=False)
    except ImportError as exc:
        raise BenchError("dependency_missing", "Install pyserial 3.5") from exc
    except OSError as exc:
        raise BenchError("serial_open", f"Cannot open {port}: {exc}") from exc


def capture(serial, run, seconds, clock, stats=None):
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
    end = clock.now() + seconds
    if stats is None:
        stats = {}
    try:
        capture_session(serial, archive, lambda: clock.now() >= end)
    finally:
        stats.update({"received_bytes": archive.received_bytes, "archived_bytes": archive.accepted_bytes,
             "host_dropped_bytes": archive.dropped_bytes, "host_dropped_chunks": archive.dropped_chunks,
             "driver_loss": "unknown", "probe_loss": "unknown", "target_loss": "unknown",
             "timestamp_source": "host read completion", "target_health": "not_checked"})
    if archive.dropped_bytes:
        raise BenchError("capture_loss", f"Host queue dropped {archive.dropped_bytes} bytes", "fail")
    return stats


def diagnostics(serial, command, emit, clock, deadline):
    from spookyprobe.protocol import Lines, parse_line
    from spookyprobe.client import Client
    emit({"kind": "session_start", "command": command, "host_receive_ns": time.time_ns()})
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
    remaining = deadline - clock.now()
    if remaining <= 0:
        raise BenchError("request_timeout", "No command budget remains")
    timeout = min(8.0, remaining)
    request_end = clock.now() + timeout
    client = Client(serial, emit, clock=clock.now, timeout=timeout)
    try:
        answer = client.request(command)
        if clock.now() > min(deadline, request_end):
            raise BenchError("request_timeout", "Late response exceeded operation budget")
        # The upstream parser preserves fields; validate required status fields too.
        fields = answer.get("fields", {})
        required = {
            "LOG STATUS": {"QUEUED", "PEAK", "DROP_WRITES", "DROP_BYTES", "TX_LOST", "TX_BYTES", "TX_ERRORS", "CONTEXT", "FLIGHT"},
            "DIAG STATUS": {"V", "CORE", "COUNT", "OVERWRITTEN", "SD_MAX_MS", "LOOP_MAX_MS", "RADIO_OVR", "PDM_OVR", "SD_ERR", "AUDIO_ERR", "HAS_FAULT"}}
        if command in required and not required[command] <= fields.keys():
            raise ValueError("incomplete status response")
        if command == "DIAG LAST" and answer.get("text", "").strip() != "OK DIAG LAST NONE":
            if not {"SEQ", "MS", "EVENT", "A", "B"} <= fields.keys() or not isinstance(fields["EVENT"], str):
                raise ValueError("incomplete last-fault response")
        return answer
    except TimeoutError as exc:
        raise BenchError("request_timeout", str(exc)) from exc
    except (ValueError, RuntimeError) as exc:
        raise BenchError("protocol_error", str(exc), "fail") from exc
    finally:
        client.finish()
        emit({"kind": "session_end", "host_receive_ns": time.time_ns()})
