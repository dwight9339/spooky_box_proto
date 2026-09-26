"""Public CLI: one JSON result on stdout, no hardware work during parsing."""
import argparse
import json
import math
import sys
import time
from .result import BenchError, exit_code, outcome, utc_now
from .supervisor import supervise
from . import regression, wav_align
from .wav_inspect import filename as wav_filename


class Parser(argparse.ArgumentParser):
    def error(self, message):
        raise BenchError("invalid_invocation", message)


def duration(text):
    try:
        value = float(text)
        if not math.isfinite(value) or not 0 < value <= 3600:
            raise ValueError()
        return value
    except ValueError as exc:
        raise argparse.ArgumentTypeError("seconds must be finite and in (0, 3600]") from exc


def load_duration(text):
    value = duration(text)
    if not 10 <= value <= 600 or not value.is_integer():
        raise argparse.ArgumentTypeError("load-test seconds must be a whole number in [10, 600]")
    return int(value)


def bounded_integer(text, name, minimum, maximum):
    try:
        value = int(text)
        if str(value) != text.strip() or not minimum <= value <= maximum:
            raise ValueError()
        return value
    except ValueError as exc:
        raise argparse.ArgumentTypeError(
            f"{name} must be a whole number in [{minimum}, {maximum}]") from exc


def sd_timeout(text):
    value = duration(text)
    if not 30 <= value <= 1800:
        raise argparse.ArgumentTypeError("SD timeout must be in [30, 1800] seconds")
    return value


def parser():
    p = Parser(description="Spooky Bench: local observation and bounded SWD controls")
    p.add_argument("--json", action="store_true", help="emit one machine-readable result")
    p.add_argument("--profile", help="JSON bench profile; optional only for discovery status")
    p.add_argument("--simulate", action="store_true", help="use fake devices; never hardware acceptance")
    p.add_argument("--scenario", choices=("happy", "missing", "ambiguous", "disconnect", "incomplete", "invalid-schema", "no-response", "m4-unavailable", "tool-timeout", "tool-failure", "partial-flash", "cdc-missing", "com-renumber", "uart-empty", "ipc-disabled", "ipc-stale", "ipc-error", "diag-fault", "record-abort", "record-overrun", "record-ipc-stale", "record-disconnect", "record-busy", "wav-missing", "wav-corrupt-frame", "wav-truncated", "wav-silent", "wav-bad-header", "sd-no-card", "sd-existing", "sd-corrupt", "sd-cleanup", "sd-timeout", "sd-disconnect", "sd-card-changed", "sd-full", "audio-stopped", "loopback-missing"), default="happy")
    commands = p.add_subparsers(dest="action", required=True)
    commands.add_parser("status", help="list ports; with profile, validate both selections")
    capture = commands.add_parser("console", help="bounded receive-only Pico UART capture")
    capture.add_argument("--seconds", type=duration, required=True)
    diag = commands.add_parser("diag", help="one device CDC diagnostic request")
    diag.add_argument("query", choices=("status", "last", "dump"))
    log = commands.add_parser("log", help="device logger counters")
    log.add_argument("query", choices=("status",))
    commands.add_parser("probe", help="examine SWD cores without reset or flash")
    commands.add_parser("reset", help="system reset/run via M7; no boot verdict")
    flash = commands.add_parser("flash", help="validate, program and verify a declared build pair")
    flash.add_argument("--manifest", required=True, help="paired build-info JSON with hashes")
    test = commands.add_parser("test", help="supervised end-to-end hardware tests")
    tests = test.add_subparsers(dest="query", required=True)
    boot = tests.add_parser("boot-smoke", help="flash IpcSmoke and prove both-core progress")
    boot.add_argument("--manifest", required=True, help="paired IpcSmoke build-info JSON")
    load = tests.add_parser("ipc-load", help="prove IPC progress during a bounded recording")
    load.add_argument("--seconds", type=load_duration, required=True,
                      help="recording duration, whole seconds in [10, 600]")
    regress = tests.add_parser("recording-regression",
        help="flash IpcSmoke, record under IPC load, retrieve and inspect the WAV")
    regress.add_argument("--manifest", required=True, help="paired IpcSmoke build-info JSON")
    regress.add_argument("--seconds", type=load_duration, required=True,
                         help="recording duration, whole seconds in [10, 600]")
    regress.add_argument("--stimulus", choices=regression.STIMULI, default="ambient",
                         help="ambient (default) or loopback: earbud feeds radio to the mic")
    regress.add_argument("--timeout", type=lambda text: bounded_integer(text, "timeout", 300, 7200),
                         help="whole-operation deadline in seconds (default scales with --seconds)")
    sd = tests.add_parser("sd-basic", help="write, verify, and remove one SD scratch file")
    sd.add_argument("--size-mib", type=lambda text: bounded_integer(text, "size-mib", 1, 64),
                    default=8, help="scratch file size in MiB, 1..64 (default 8)")
    sd.add_argument("--passes", type=lambda text: bounded_integer(text, "passes", 1, 4),
                    default=1, help="write/verify passes, 1..4 (default 1)")
    sd.add_argument("--timeout", type=sd_timeout, default=180.0,
                    help="whole-operation deadline in seconds, 30..1800 (default 180)")
    wav = commands.add_parser("wav", help="retrieve and inspect a recorded WAV")
    wav_commands = wav.add_subparsers(dest="query", required=True)
    inspect = wav_commands.add_parser("inspect", help="CRC-transfer and analyze one REC###.WAV")
    inspect.add_argument("--file", type=wav_filename, required=True, dest="filename")
    inspect.add_argument("--timeout", type=duration, default=600.0,
                         help="whole-operation deadline in seconds (default 600)")
    align = wav_commands.add_parser(
        "align", help="offline: measure radio-to-mic lag in a local WAV (loopback stimulus)")
    align.add_argument("--wav", required=True, help="local recorder WAV, e.g. a run's audio file")
    align.add_argument("--window-seconds", type=duration, default=2.0)
    align.add_argument("--hop-seconds", type=duration, default=5.0)
    align.add_argument("--max-lag-ms", type=duration, default=50.0)
    for name in ("power", "trace", "crash"):
        commands.add_parser(name, help="not implemented")
    return p


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    started, started_at = time.monotonic(), utc_now()
    execution = "simulated" if "--simulate" in argv else "hardware"
    command = "parse"
    try:
        args = parser().parse_args(argv)
        command = args.action + (" " + args.query if hasattr(args, "query") else "")
        if args.scenario != "happy" and not args.simulate:
            raise BenchError("invalid_invocation", "--scenario requires --simulate")
        if command == "test sd-basic" and args.size_mib * args.passes > 128:
            raise BenchError("invalid_invocation", "SD size-mib times passes must not exceed 128")
        if command == "test recording-regression" and args.stimulus == "loopback" and \
           args.seconds > regression.LOOPBACK_MAX_SECONDS:
            raise BenchError("invalid_invocation", "loopback alignment is limited to "
                             f"{regression.LOOPBACK_MAX_SECONDS} seconds")
        if args.action in ("power", "trace", "crash"):
            raise BenchError("not_implemented", "This operation is not implemented", "unsupported")
        if command == "wav align":
            # Pure local computation: no ports, locks, profile or run directory.
            execution = "offline"
            metrics, (result, reason, detail) = wav_align.run_local({
                "wav": args.wav, "window_seconds": args.window_seconds,
                "hop_seconds": args.hop_seconds, "max_lag_ms": args.max_lag_ms})
            metrics["wav"] = args.wav
            value = outcome(command, execution, started_at, started, result=result,
                            reason=reason, detail=detail, metrics=metrics)
            return _emit(value, argv)
        if not args.profile and args.action != "status":
            raise BenchError("invalid_invocation", "--profile is required for this operation")
        options = {"command": command, "profile": args.profile, "simulate": args.simulate,
                   "scenario": args.scenario, "seconds": getattr(args, "seconds", None),
                   "manifest": getattr(args, "manifest", None),
                   "filename": getattr(args, "filename", None),
                   "size_mib": getattr(args, "size_mib", None),
                   "passes": getattr(args, "passes", None),
                   "stimulus": getattr(args, "stimulus", None)}
        if command == "test recording-regression":
            seconds = args.timeout or regression.budget(args.seconds, args.stimulus)
        else:
            seconds = args.timeout if command in ("wav inspect", "test sd-basic") else args.seconds + 60.0 if command == "test ipc-load" else 240.0 if command == "test boot-smoke" else 120.0 if command == "flash" else 10.0 if command == "status" else args.seconds + 12.0 if command == "console" else 15.0
        value = supervise(options, seconds)
    except BenchError as exc:
        value = outcome(command, execution, started_at, started, result=exc.result, reason=exc.reason, detail=str(exc))
    except KeyboardInterrupt:
        value = outcome(command, execution, started_at, started, result="error", reason="interrupted")
    except Exception as exc:
        value = outcome(command, execution, started_at, started, result="error", reason="internal_error", detail=f"{type(exc).__name__}: {exc}")
    return _emit(value, argv)


def _emit(value, argv):
    if "--json" in argv:
        print(json.dumps(value, ensure_ascii=True, allow_nan=False))
    else:
        print(f"{value['result'].upper()}: {value['command']} ({value['execution']})")
        print(json.dumps(value, indent=2, ensure_ascii=True, allow_nan=False))
    return exit_code(value)
