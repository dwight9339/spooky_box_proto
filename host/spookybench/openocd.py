"""Pinned OpenOCD H755 operations. No arbitrary Tcl or user paths in Tcl."""
import hashlib
import json
import os
from pathlib import Path
import re
import time
import threading
from .firmware import stage_pair
from .process_io import run_process
from .result import BenchError
from . import serial_io

LOG_CAP = 1024 * 1024
CONTROL_COMMANDS = ("probe", "reset", "flash")


def verify_tool(config, run):
    lock = json.loads((Path(__file__).parent / "openocd-lock.json").read_text())
    executable = Path(config["executable"])
    if not executable.is_file() or executable.suffix.lower() != ".exe":
        raise BenchError("tool_missing", "Configured OpenOCD executable is missing")
    for name, expected in lock["binaries"].items():
        path = executable if name == "openocd.exe" else executable.parent / name
        with path.open("rb") as stream:
            raw = stream.read(32 * 1024**2 + 1)
        if len(raw) > 32 * 1024**2 or hashlib.sha256(raw).hexdigest() != expected:
            raise BenchError("tool_pin", f"OpenOCD binary differs from validated package: {name}")
    for name, expected in lock["scripts"].items():
        with (Path(config["scripts"]) / name).open("rb") as stream:
            raw = stream.read(1024 * 1024 + 1)
        if hashlib.sha256(raw).hexdigest() != expected:
            raise BenchError("tool_pin", f"OpenOCD script differs from validated package: {name}")
        run.reserve(len(raw))
        target = run.path / "scripts" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(raw)
    return dict(state="verified", version=lock["version"], executable=str(executable),
                binaries=lock["binaries"], scripts=lock["scripts"])


def configuration(config, dual=False):
    # All interpolated values are constrained ASCII tokens/integers by config.py.
    lines = ["gdb_port disabled", "tcl_port disabled", "telnet_port disabled",
             "source [find interface/cmsis-dap.cfg]", "cmsis_dap_backend usb_bulk",
             f"adapter serial {config['serial_number']}", "transport select swd",
             f"set DUAL_CORE {int(dual)}", "set DUAL_BANK 1",
             "source [find target/stm32h7x.cfg]",
             "stm32h7x.ap2 configure -defer-examine",
             "stm32h7x.cpu0 configure -event examine-end {}",
             "stm32h7x.cpu0 configure -event reset-init {}",
             f"adapter speed {config['adapter_khz']}"]
    if dual:
        lines += ["stm32h7x.cpu1 configure -defer-examine",
                  "stm32h7x.cpu1 configure -event examine-end {}"]
    return "\n".join(lines) + "\n"


def operation_script(command):
    # No program helper: keep reset/run strictly after BOTH verify operations.
    common = """init
targets stm32h7x.cpu0
stm32h7x.cpu0 arp_examine
stm32h7x.cpu0 arp_poll
echo "SB_M7 [stm32h7x.cpu0 curstate]"
"""
    if command == "probe":
        body = common + """if {[catch {stm32h7x.cpu1 arp_examine; stm32h7x.cpu1 arp_poll}]} {
    echo "SB_M4 unavailable"
} else {
    echo "SB_M4 [stm32h7x.cpu1 curstate]"
}
echo "SB_DONE probe"
"""
    elif command == "reset":
        body = common + """reset run
echo "SB_RESET completed"
stm32h7x.cpu0 arp_poll
echo "SB_FINAL_M7 [stm32h7x.cpu0 curstate]"
echo "SB_DONE reset"
"""
    else:
        body = common + """reset init
halt 1000
echo "SB_STAGE halted_m7"
flash write_image erase firmware/CM7.elf
verify_image firmware/CM7.elf
echo "SB_STAGE verified_cm7"
flash write_image erase firmware/CM4.elf
verify_image firmware/CM4.elf
echo "SB_STAGE verified_cm4"
reset run
echo "SB_RESET completed"
stm32h7x.cpu0 arp_poll
echo "SB_FINAL_M7 [stm32h7x.cpu0 curstate]"
echo "SB_DONE flash"
"""
    # Failure never performs reset/run. Halt is best effort, not paired-halt proof.
    cleanup = "" if command == "probe" else """catch {targets stm32h7x.cpu0; halt 500}
    catch {echo "SB_FAILURE_M7 [stm32h7x.cpu0 curstate]"}
"""
    return "if {[catch {\n" + body + "} sb_error]} {\n    echo \"SB_FAILED $sb_error\"\n" + cleanup + "    shutdown error\n} else {\n    shutdown\n}\n"


def interpret(command, code, output, metrics):
    text = output.decode("utf-8", errors="replace")
    states = {}
    for tag, state in re.findall(r"^SB_(M7|M4|FINAL_M7|FAILURE_M7) ([a-z-]+)\r?$", text, re.M):
        states[tag] = state
    metrics["swd"] = {"cm7": states.get("M7", "unknown"),
                      "cm4": states.get("M4", "not_examined"),
                      "cm4_note": "Unavailable can mean sleep/boot-held or access failure; no M4 liveness inference"}
    metrics["control"] = {"verified_cores": [core for core in ("cm7", "cm4")
        if f"SB_STAGE verified_{core}" in text.splitlines()],
        "reset_command_completed": "SB_RESET completed" in text.splitlines(),
        "final_cm7_state": states.get("FINAL_M7", states.get("FAILURE_M7", "unknown")),
        "final_cm4_state": "unknown", "attempts": 1, "tool_exit_code": code}
    # Aggregate paired state cannot be established through AP0 alone.
    metrics["final_target_state"] = "unknown"
    valid = states.get("M7") in ("running", "halted", "reset", "debug-running")
    if code or f"SB_DONE {command}" not in text.splitlines() or not valid:
        raise BenchError("openocd_failed", "OpenOCD failed or did not supply required completion evidence", "fail")
    if command == "flash" and metrics["control"]["verified_cores"] != ["cm7", "cm4"]:
        raise BenchError("verify_incomplete", "Both images must verify before success", "fail")
    if command in ("reset", "flash") and not metrics["control"]["reset_command_completed"]:
        raise BenchError("reset_incomplete", "Reset/run completion missing", "fail")


def control(options, profile, run, metrics, artifacts):
    command = options["command"]
    config = profile.get("openocd")
    if not config:
        raise BenchError("control_not_configured", "Add the openocd profile section before using controls")
    simulated = options["simulate"]
    if not simulated and os.name != "nt":
        raise BenchError("platform_unsupported", "Phase 1B controls require Windows", "unsupported")
    # Admission precedes any hardware tool invocation.
    run.reserve(LOG_CAP)
    if command == "flash":
        metrics["firmware"] = stage_pair(options["manifest"], run)
        artifacts["firmware"] = str(run.path / "firmware")
        artifacts["manifest"] = str(run.path / "build-info.json")
    metrics["tools"]["openocd"] = ({"state": "simulated", "version": "simulated"}
        if simulated else verify_tool(config, run))
    script = configuration(config, command == "probe") + operation_script(command)
    run.reserve(len(script.encode()))
    (run.path / "operation.cfg").write_text(script, encoding="ascii")
    artifacts["openocd_log"] = str(run.path / "openocd.log")
    metrics["human_required"] = False
    started_tool = False
    capture = None
    try:
        if command in ("reset", "flash") and not simulated:
            capture = LiveCapture(run, metrics, artifacts, options["deadline"])
            capture.start(metrics["selected"]["probe"]["port"])
        with (run.path / "openocd.log").open("xb") as stream:
            def emit(raw):
                stream.write(raw)
                stream.flush()
            if simulated:
                code, raw = simulate(command, options["scenario"])
                emit(raw)
            else:
                argv = [config["executable"], "-s", "scripts", "-f", "operation.cfg"]
                run.write_json("openocd-command.json", {"argv": argv, "cwd": str(run.path)})
                started_tool = True
                code, raw = run_process(argv, run.path,
                    options["deadline"] - (3.0 if capture else 1.0), emit)
            interpret(command, code, raw, metrics)
            if capture:
                # Preserve a bounded boot-log tail; this is not a boot/IPC verdict.
                time.sleep(min(2.0, max(0, options["deadline"] - time.monotonic() - 0.8)))
    except BaseException:
        if command in ("flash", "reset") and (started_tool or simulated):
            metrics["human_required"] = True
            metrics["final_target_state"] = "unknown"
        raise
    finally:
        if capture:
            try:
                capture.close()
            except BaseException:
                if started_tool:
                    metrics["human_required"] = True
                raise


class LiveCapture:
    """Own the Pico port under the same board lock; ready before any reset/flash."""
    def __init__(self, run, metrics, artifacts, deadline):
        self.run, self.metrics, self.deadline = run, metrics, deadline
        self.stop, self.ready = threading.Event(), threading.Event()
        self.errors = []
        self.thread = None
        artifacts["uart"] = str(run.path / "uart")
        metrics["capture"] = {}

    def start(self, port):
        serial = serial_io.open_serial(port)
        def collect():
            try:
                serial_io.capture(serial, self.run, max(0, self.deadline - time.monotonic()),
                    serial_io.Clock(), self.metrics["capture"], self.stop, self.ready)
            except BaseException as exc:
                self.errors.append(exc)
            finally:
                serial.close()
        self.thread = threading.Thread(target=collect, daemon=True)
        self.thread.start()
        if not self.ready.wait(min(1.0, max(0, self.deadline - time.monotonic()))):
            raise BenchError("capture_not_ready", "UART archive did not become ready before control")
        if self.errors:
            raise self.errors[0]

    def close(self):
        self.stop.set()
        if self.thread:
            self.thread.join(0.75)
            if self.thread.is_alive():
                raise BenchError("capture_cleanup", "UART shutdown did not complete within its budget")
        if self.errors:
            raise self.errors[0]


def simulate(command, scenario):
    if scenario == "tool-timeout":
        raise BenchError("tool_timeout", "Simulated OpenOCD timeout")
    text = "SB_M7 running\n"
    if command == "probe":
        text += "SB_M4 " + ("unavailable" if scenario == "m4-unavailable" else "running") + "\n"
    if command == "flash":
        text += "SB_STAGE halted_m7\nSB_STAGE verified_cm7\n"
        if scenario == "partial-flash":
            return 1, (text + "SB_FAILED CM4 verify failed\nSB_FAILURE_M7 halted\n").encode()
        text += "SB_STAGE verified_cm4\n"
    if scenario == "tool-failure":
        return 1, (text + "SB_FAILED simulated tool failure\n").encode()
    if command != "probe":
        text += "SB_RESET completed\nSB_FINAL_M7 running\n"
    return 0, (text + f"SB_DONE {command}\n").encode()
