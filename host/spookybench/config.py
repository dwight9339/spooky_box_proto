"""Strict, bounded JSON profiles. Paths are data, never shell fragments."""
import json
import math
import os
import re
from pathlib import Path
from .result import BenchError

DEFAULT_LIMITS = {"run_bytes": 256 * 1024**2, "total_bytes": 2 * 1024**3,
                  "min_free_bytes": 512 * 1024**2, "uart_bytes": 64 * 1024**2,
                  "diag_bytes": 8 * 1024**2}


def number(value, name, minimum, maximum):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not minimum <= value <= maximum:
        raise BenchError("invalid_config", f"{name} must be finite in [{minimum}, {maximum}]")
    return value


def read_json(path, cap=65536):
    with Path(path).open("rb") as stream:
        raw = stream.read(cap + 1)
    if len(raw) > cap:
        raise BenchError("invalid_config", "JSON input exceeds size limit")
    def pairs(items):
        value = {}
        for key, item in items:
            if key in value:
                raise BenchError("invalid_config", f"Duplicate JSON key: {key}")
            value[key] = item
        return value
    return json.loads(raw, object_pairs_hook=pairs), raw


def selector(value, name):
    allowed = {"port", "serial_number", "vid", "pid", "interface"}
    if not isinstance(value, dict) or not value or value.keys() - allowed:
        raise BenchError("invalid_config", f"Invalid {name} selector")
    if not value.get("port") and not value.get("serial_number"):
        raise BenchError("invalid_config", f"{name} requires a port or serial_number")
    for key, item in value.items():
        if key in ("vid", "pid"):
            if type(item) is not int or not 0 <= item <= 65535:
                raise BenchError("invalid_config", f"{name}.{key} must be a 16-bit integer")
        elif not isinstance(item, str) or not item or len(item) > 256 or any(ord(c) < 32 for c in item):
            raise BenchError("invalid_config", f"Invalid {name}.{key}")
    return value


def load_profile(path):
    try:
        value, raw = read_json(path)
        if not isinstance(value, dict) or value.keys() - {"schema_version", "board_id", "artifact_root", "probe", "device", "limits", "openocd"}:
            raise BenchError("invalid_config", "Unknown profile fields")
        if type(value.get("schema_version")) is not int or value["schema_version"] != 1:
            raise BenchError("invalid_config", "Profile schema_version must be 1")
        board = value.get("board_id")
        if not isinstance(board, str) or not 1 <= len(board) <= 64 or not all(c.isascii() and (c.isalnum() or c in "-_") for c in board):
            raise BenchError("invalid_config", "board_id must contain 1..64 ASCII letters/digits/-/_")
        for role in ("probe", "device"):
            selector(value.get(role), role)
        if "openocd" in value:
            control = value["openocd"]
            if not isinstance(control, dict) or set(control) != {"executable", "scripts", "serial_number", "adapter_khz"}:
                raise BenchError("invalid_config", "Invalid openocd fields")
            serial = control["serial_number"]
            if not isinstance(serial, str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,128}", serial):
                raise BenchError("invalid_config", "OpenOCD serial must be an ASCII identifier")
            if serial != value["probe"].get("serial_number"):
                raise BenchError("invalid_config", "OpenOCD serial must match the probe CDC serial selector")
            if type(control["adapter_khz"]) is not int:
                raise BenchError("invalid_config", "adapter_khz must be an integer")
            number(control["adapter_khz"], "adapter_khz", 50, 4000)
            for field in ("executable", "scripts"):
                text = control[field]
                if not isinstance(text, str) or not text or len(text) > 2048 or any(ord(c) < 32 for c in text):
                    raise BenchError("invalid_config", f"Invalid openocd.{field}")
                location = Path(os.path.expandvars(text)).expanduser()
                if not location.is_absolute() or str(location).startswith("\\\\"):
                    raise BenchError("invalid_config", "OpenOCD paths must be absolute and local")
                control[field] = str(location)
        root = value.get("artifact_root")
        if not isinstance(root, str) or not root or len(root) > 2048:
            raise BenchError("invalid_config", "artifact_root is required")
        root = Path(os.path.expandvars(root)).expanduser()
        if not root.is_absolute() or str(root).startswith("\\\\"):
            raise BenchError("invalid_config", "artifact_root must be an absolute local path")
        value["artifact_root"] = str(root)
        limits = value.get("limits", {})
        if not isinstance(limits, dict) or limits.keys() - DEFAULT_LIMITS.keys():
            raise BenchError("invalid_config", "Unknown limit fields")
        limits = dict(DEFAULT_LIMITS, **limits)
        for key, item in limits.items():
            if type(item) is not int:
                raise BenchError("invalid_config", f"{key} must be an integer")
            number(item, key, 0 if key == "min_free_bytes" else 1, 16 * 1024**3)
        if not 262144 <= limits["run_bytes"] <= 1024**3 or limits["total_bytes"] < limits["run_bytes"]:
            raise BenchError("invalid_config", "run_bytes must be 256 KiB..1 GiB and fit total_bytes")
        if max(limits["uart_bytes"], limits["diag_bytes"]) > limits["run_bytes"] - 131072:
            raise BenchError("invalid_config", "stream limits must leave 128 KiB of run metadata capacity")
        value["limits"] = limits
        return value, raw
    except BenchError:
        raise
    except (OSError, ValueError, TypeError) as exc:
        raise BenchError("invalid_config", str(exc)) from exc
