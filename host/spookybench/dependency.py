"""Require the tested Spookyprobe source snapshot, not just its version string."""
import hashlib
import importlib
import json
from pathlib import Path
from .result import BenchError


def verify_probe():
    lock = json.loads(Path(__file__).with_name("probe-lock.json").read_text(encoding="utf-8"))
    try:
        package = importlib.import_module("spookyprobe")
    except ImportError as exc:
        raise BenchError("dependency_missing", "Install the pinned Spookyprobe snapshot; see docs/procedures/spooky-bench-setup.md") from exc
    root = Path(package.__file__).parent
    for name, expected in lock["modules"].items():
        try:
            raw = (root / name).read_bytes().replace(b"\r\n", b"\n")
        except OSError as exc:
            raise BenchError("dependency_mismatch", f"Missing Spookyprobe module {name}") from exc
        if hashlib.sha256(raw).hexdigest() != expected:
            raise BenchError("dependency_mismatch", f"Spookyprobe {name} differs from tested commit {lock['commit']}")
    return {"package": "spookyprobe", "version": "0.1.0", "source_commit": lock["commit"],
            "verified_modules": sorted(lock["modules"])}
