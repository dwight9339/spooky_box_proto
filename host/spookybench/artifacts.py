"""Finite, append-only run evidence. No automatic retention/deletion."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import uuid
from .platform_io import no_links
from .result import BenchError, utc_now


def encode(value):
    return (json.dumps(value, ensure_ascii=True, allow_nan=False, separators=(",", ":")) + "\n").encode("utf-8")


def usage(root, max_entries=100000):
    total = entries = 0
    pending = [Path(root)]
    while pending:
        with os.scandir(pending.pop()) as stream:
            for item in stream:
                entries += 1
                if entries > max_entries:
                    raise BenchError("storage_inventory_limit", "Artifact inventory exceeds entry limit")
                no_links(item.path)
                if item.is_dir(follow_symlinks=False):
                    pending.append(Path(item.path))
                else:
                    total += item.stat(follow_symlinks=False).st_size
    return total


class Run:
    def __init__(self, profile, profile_raw, metadata):
        self.limits = profile["limits"]
        self.used = 0
        root = Path(profile["artifact_root"])
        no_links(root)
        # Enforce this in a source checkout. Installed wheels have no .git here.
        checkout = Path(__file__).resolve().parents[2]
        if (checkout / ".git").exists() and root.resolve().is_relative_to(checkout):
            raise BenchError("unsafe_artifact_path", "Keep bench run storage outside the source checkout")
        root.mkdir(parents=True, exist_ok=True)
        if usage(root) + self.limits["run_bytes"] > self.limits["total_bytes"]:
            raise BenchError("storage_quota", "Existing evidence plus reserved run budget exceeds total quota")
        if shutil.disk_usage(root).free < self.limits["run_bytes"] + self.limits["min_free_bytes"]:
            raise BenchError("storage_free_space", "Insufficient free space for run budget and reserve")
        name = utc_now().replace(":", "").replace("+", "_") + "-" + uuid.uuid4().hex[:8]
        self.path = root / name
        self.path.mkdir()
        self.write_json("metadata.json", dict(metadata, state="started",
            board_id=profile["board_id"], profile_sha256=hashlib.sha256(profile_raw).hexdigest(),
            target_firmware_identity=None, probe_firmware_identity=None))
        self.write_json("profile.json", profile)

    def reserve(self, length):
        # Keep 64 KiB for authoritative completion/error metadata.
        if self.used + length > self.limits["run_bytes"] - 65536:
            raise BenchError("artifact_limit", "Run artifact budget exhausted", "fail")
        self.used += length

    def write_json(self, name, value, final=False):
        payload = encode(value)
        if final:
            if len(payload) > 32768:
                raise BenchError("result_limit", "Final result exceeds 32 KiB")
        else:
            self.reserve(len(payload))
        target = self.path / name
        temporary = self.path / (name + ".tmp")
        with temporary.open("xb") as stream:
            stream.write(payload)
        temporary.replace(target)


class JsonLines:
    def __init__(self, run, name, limit):
        self.run, self.limit, self.count = run, limit, 0
        self.stream = (run.path / name).open("xb")

    def emit(self, value):
        payload = encode(value)
        if self.count + len(payload) > self.limit:
            raise BenchError("artifact_limit", "Diagnostic stream cap reached", "fail")
        self.run.reserve(len(payload))
        self.stream.write(payload)
        self.stream.flush()
        self.count += len(payload)

    def close(self):
        self.stream.close()
