"""Public CLI: one JSON result on stdout, no hardware work during parsing."""
import argparse
import json
import math
import sys
import time
from .result import BenchError, exit_code, outcome, utc_now
from .supervisor import supervise


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


def parser():
    p = Parser(description="Spooky Bench: local observation and bounded SWD controls")
    p.add_argument("--json", action="store_true", help="emit one machine-readable result")
    p.add_argument("--profile", help="JSON bench profile; optional only for discovery status")
    p.add_argument("--simulate", action="store_true", help="use fake devices; never hardware acceptance")
    p.add_argument("--scenario", choices=("happy", "missing", "ambiguous", "disconnect", "incomplete", "invalid-schema", "no-response", "m4-unavailable", "tool-timeout", "tool-failure", "partial-flash"), default="happy")
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
    for name in ("test", "power", "trace", "crash"):
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
        if args.action in ("test", "power", "trace", "crash"):
            raise BenchError("not_implemented", "This operation is not implemented", "unsupported")
        if not args.profile and args.action != "status":
            raise BenchError("invalid_invocation", "--profile is required for this operation")
        options = {"command": command, "profile": args.profile, "simulate": args.simulate,
                   "scenario": args.scenario, "seconds": getattr(args, "seconds", None),
                   "manifest": getattr(args, "manifest", None)}
        seconds = 120.0 if command == "flash" else 10.0 if command == "status" else args.seconds + 12.0 if command == "console" else 15.0
        value = supervise(options, seconds)
    except BenchError as exc:
        value = outcome(command, execution, started_at, started, result=exc.result, reason=exc.reason, detail=str(exc))
    except KeyboardInterrupt:
        value = outcome(command, execution, started_at, started, result="error", reason="interrupted")
    except Exception as exc:
        value = outcome(command, execution, started_at, started, result="error", reason="internal_error", detail=f"{type(exc).__name__}: {exc}")
    if "--json" in argv:
        print(json.dumps(value, ensure_ascii=True, allow_nan=False))
    else:
        print(f"{value['result'].upper()}: {value['command']} ({value['execution']})")
        print(json.dumps(value, indent=2, ensure_ascii=True, allow_nan=False))
    return exit_code(value)
