"""Each worker owns ports, filesystem activity, and locks for one operation."""
import hashlib
import importlib.metadata
import json
import platform
import time
from pathlib import Path
from . import __version__
from . import fake, serial_io, openocd, boot_smoke, ipc_load
from .artifacts import JsonLines, Run
from .config import load_profile
from .dependency import verify_probe
from .platform_io import BenchLock, ports, select_devices
from .result import BenchError, outcome, utc_now


def source_identity():
    """Hash the actual runner sources (including local edits), without Git/tool calls."""
    digest = hashlib.sha256()
    root = Path(__file__).parent
    sources = {}
    for path in sorted([*root.glob("*.py"), *root.glob("*-lock.json")]):
        raw = path.read_bytes().replace(b"\r\n", b"\n")
        digest.update(path.name.encode())
        digest.update(b"\0")
        digest.update(raw)
        sources[path.name] = raw.decode("utf-8")
    try:
        serial_version = importlib.metadata.version("pyserial")
    except importlib.metadata.PackageNotFoundError:
        serial_version = None
    return {"version": __version__, "source_sha256": digest.hexdigest(),
            "python": platform.python_version(), "platform": platform.platform(),
            "pyserial": serial_version}, sources


def execute(options, run_notice=lambda path: None):
    started = options.get("started", time.monotonic())
    started_at = options.get("started_at", utc_now())
    execution = "simulated" if options["simulate"] else "hardware"
    command = options["command"]
    run = None
    metrics = {"target_health": "not_checked", "final_target_state": "unknown"}
    artifacts = {}

    def finish(result="pass", reason=None, detail=None):
        metrics["evidence_complete"] = result == "pass"
        value = outcome(command, execution, started_at, started, result=result,
                        reason=reason, detail=detail, metrics=metrics, artifacts=artifacts)
        if run:
            run.write_json("test-results.json", value, final=True)
        return value

    try:
        dependency = verify_probe()
        identity, sources = source_identity()
        metrics["tools"] = dict(identity, spookyprobe=dependency,
                                openocd={"state": "not_checked", "reason": "not_requested"})
        metrics["capabilities"] = {"capture": True, "diagnostics": True,
            "flash": False, "reset": False, "ipc_test": False, "power": False,
            "trace": False, "crash": False, "probe_counters": False}
        inventory = fake.inventory(options["scenario"]) if options["simulate"] else ports()
        metrics["ports"] = inventory
        if not options["profile"]:
            metrics["configured"] = False
            return finish(reason="simulated" if options["simulate"] else None)
        profile, profile_raw = load_profile(options["profile"])
        metrics["capabilities"].update(probe="openocd" in profile,
            flash="openocd" in profile, reset="openocd" in profile,
            ipc_test=True)
        # Controls require the named probe, not an already-booted target CDC.
        required = ("probe", "device") if command == "status" else ("probe",) if command == "console" or command in openocd.CONTROL_COMMANDS or command == "test boot-smoke" else ("device",)
        # Both board and artifact-root locks live outside the run root. The root
        # lock serializes quota reservations across different boards/profiles.
        root_key = str(Path(profile["artifact_root"]).resolve()).casefold()
        with BenchLock(profile["board_id"]), BenchLock("artifacts:" + root_key):
            run = Run(profile, profile_raw, {"schema_version": 1, "execution": execution,
                "command": command, "started_at": started_at, "tools": metrics["tools"]})
            run.execution = execution
            artifacts["run_dir"] = str(run.path)
            artifacts["result"] = str(run.path / "test-results.json")
            run_notice(str(run.path))
            try:
                run.write_json("bench-source.json", sources)
                selected = select_devices(profile, inventory, required)
                metrics.update(configured=True, selected=selected)
                run.write_json("devices.json", selected)
                if command == "status":
                    return finish(reason="simulated" if options["simulate"] else None)
                if command in openocd.CONTROL_COMMANDS:
                    openocd.control(options, profile, run, metrics, artifacts)
                    return finish(reason="simulated" if options["simulate"] else None)
                if command == "test boot-smoke":
                    boot_smoke.run(options, profile, run, metrics, artifacts)
                    return finish(reason="simulated" if options["simulate"] else None)
                if command == "test ipc-load":
                    ipc_load.run(options, profile, run, metrics, artifacts)
                    return finish(reason="simulated" if options["simulate"] else None)
                clock = fake.Clock() if options["simulate"] else serial_io.Clock()
                role = "probe" if command == "console" else "device"
                serial = fake.Serial(clock, role, options["scenario"]) if options["simulate"] else serial_io.open_serial(selected[role]["port"])
                # Hard supervisor budget includes initialization and serial close.
                try:
                    remaining = options["deadline"] - time.monotonic() - 0.5
                    if remaining <= 0:
                        raise BenchError("operation_timeout", "Initialization exhausted operation budget")
                    if command == "console":
                        artifacts["uart"] = str(run.path / "uart")
                        if options["seconds"] > remaining:
                            raise BenchError("operation_timeout", "Insufficient time for requested capture")
                        metrics["capture"] = {}
                        serial_io.capture(serial, run, options["seconds"], clock, metrics["capture"])
                    else:
                        artifacts["diagnostics"] = str(run.path / "diagnostics.jsonl")
                        lines = JsonLines(run, "diagnostics.jsonl", profile["limits"]["diag_bytes"])
                        try:
                            metrics["response"] = serial_io.diagnostics(serial, command.upper(), lines.emit,
                                                                       clock, clock.now() + remaining)
                        finally:
                            lines.close()
                finally:
                    serial.close()
                return finish(reason="simulated" if options["simulate"] else None)
            except BenchError as exc:
                return finish(exc.result, exc.reason, str(exc))
            except TimeoutError as exc:
                return finish("error", "request_timeout", str(exc))
            except OSError as exc:
                return finish("error", "io_error", str(exc))
    except BenchError as exc:
        result, reason, detail = exc.result, exc.reason, str(exc)
    except OSError as exc:
        result, reason, detail = "error", "io_error", str(exc)
    except Exception as exc:
        result, reason, detail = "error", "internal_error", f"{type(exc).__name__}: {exc}"
    # This path also handles failure while writing the authoritative result.
    # Do not repeatedly write to a failing disk or claim completion was archived.
    artifacts.pop("result", None)
    return outcome(command, execution, started_at, started, result=result, reason=reason,
                   detail=detail, metrics=metrics, artifacts=artifacts)


def worker(options, result_buffer, result_length, notice_buffer, notice_length):
    def notice(path):
        raw = path.encode("utf-8")
        if len(raw) <= len(notice_buffer):
            notice_buffer[:len(raw)] = raw
            notice_length.value = len(raw)
    value = execute(options, notice)
    raw = json.dumps(value, ensure_ascii=True, allow_nan=False).encode("utf-8")
    if len(raw) > len(result_buffer):
        raw = json.dumps(outcome(options["command"], "simulated" if options["simulate"] else "hardware",
            utc_now(), time.monotonic(), result="error", reason="result_limit")).encode()
    result_buffer[:len(raw)] = raw
    result_length.value = len(raw)  # Commit last; parent only reads after exit.
