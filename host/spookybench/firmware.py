"""Validate declared build pairs and stage only hash-verified ARM ELF inputs."""
import hashlib
from pathlib import Path
import re
import struct
from .config import read_json
from .result import BenchError

MAX_ELF = 16 * 1024**2


def elf_ranges(raw, core):
    def reject(detail):
        raise BenchError("invalid_firmware", f"{core}: {detail}")
    if len(raw) < 52 or raw[:7] != b"\x7fELF\x01\x01\x01":
        reject("expected ELF32 little-endian")
    kind, machine, version, entry, phoff = struct.unpack_from("<HHIII", raw, 16)
    ehsize, phsize, phnum = struct.unpack_from("<HHH", raw, 40)
    if (kind, machine, version, ehsize, phsize) != (2, 40, 1, 52, 32) or not 1 <= phnum <= 128:
        reject("invalid executable/program headers")
    if phoff < 52 or phoff + phsize * phnum > len(raw):
        reject("truncated program headers")
    base = 0x08000000 if core == "CM7" else 0x08100000
    ranges = []
    vector = None
    executable_entry = False
    for i in range(phnum):
        ptype, offset, virtual, physical, size, memory, flags, align = struct.unpack_from("<8I", raw, phoff + i * 32)
        if ptype != 1:
            continue
        if size > memory or offset + size > len(raw):
            reject("invalid LOAD size")
        if not size:  # NOLOAD RAM/mailbox sections are not programmed.
            continue
        if not base <= physical < physical + size <= base + 0x100000:
            reject("LOAD bytes escape the assigned 1 MiB flash bank")
        if any(physical < r["end"] and physical + size > r["start"] for r in ranges):
            reject("overlapping LOAD ranges")
        ranges.append({"start": physical, "end": physical + size})
        if flags & 1 and virtual <= (entry & ~1) < virtual + size:
            executable_entry = True
        if physical == base and size >= 8:
            vector = struct.unpack_from("<II", raw, offset)
    if not ranges or vector is None or not executable_entry or not entry & 1 or not base <= (entry & ~1) < base + 0x100000:
        reject("missing flash vectors or valid Thumb entry point")
    stack, reset = vector
    ram_start, ram_end = (0x20000000, 0x20020000) if core == "CM7" else (0x10000000, 0x10048000)
    if not ram_start < stack <= ram_end or stack % 8 or reset != entry:
        reject("unexpected initial stack or reset vector")
    return sorted(ranges, key=lambda r: r["start"])


def validate_pair(path):
    try:
        manifest, _ = read_json(path)
        required = {"schema_version", "build_id", "preset", "source_revision", "dirty", "source_snapshot_sha256", "images"}
        if not isinstance(manifest, dict) or not required <= set(manifest) or set(manifest) - required - {"compiler", "build_flags"} or type(manifest["schema_version"]) is not int or manifest["schema_version"] != 1:
            raise ValueError("Invalid manifest schema")
        if manifest["preset"] not in ("Debug", "Release", "IpcSmoke", "IpcMismatch"):
            raise ValueError("Unknown build preset")
        compiler, flags = manifest.get("compiler"), manifest.get("build_flags")
        if compiler is not None and (not isinstance(compiler, str) or not 1 <= len(compiler) <= 256):
            raise ValueError("Invalid compiler identity")
        if flags is not None and (not isinstance(flags, list) or len(flags) > 128 or any(not isinstance(f, str) or len(f) > 256 for f in flags)):
            raise ValueError("Invalid build flags")
        manifest.update(compiler=compiler, build_flags=flags)
        if not isinstance(manifest["build_id"], str) or not re.fullmatch(r"[A-Za-z0-9_.-]{1,128}", manifest["build_id"]):
            raise ValueError("Invalid build_id")
        if not isinstance(manifest["source_revision"], str) or not re.fullmatch(r"[0-9a-f]{40}", manifest["source_revision"]):
            raise ValueError("source_revision must be a full Git SHA")
        snapshot = manifest["source_snapshot_sha256"]
        if type(manifest["dirty"]) is not bool or (snapshot is not None and (not isinstance(snapshot, str) or not re.fullmatch(r"[0-9a-f]{64}", snapshot))) or (manifest["dirty"] and snapshot is None):
            raise ValueError("Dirty builds require source_snapshot_sha256")
        if not isinstance(manifest["images"], dict) or set(manifest["images"]) != {"CM7", "CM4"}:
            raise ValueError("Manifest must declare exactly CM7 and CM4")
        images = {}
        for core in ("CM7", "CM4"):
            item = manifest["images"][core]
            if not isinstance(item, dict) or set(item) != {"path", "sha256"} or not isinstance(item["path"], str) or not item["path"] or len(item["path"]) > 2048:
                raise ValueError(f"Invalid {core} image declaration")
            source = Path(path).resolve().parent / item["path"]
            with source.open("rb") as stream:
                raw = stream.read(MAX_ELF + 1)
            if len(raw) > MAX_ELF:
                raise ValueError("ELF exceeds 16 MiB cap")
            if hashlib.sha256(raw).hexdigest() != item["sha256"]:
                raise BenchError("firmware_hash", f"{core} hash does not match manifest")
            images[core] = (raw, elf_ranges(raw, core))
        return manifest, images
    except BenchError:
        raise
    except (OSError, ValueError, TypeError, KeyError, struct.error) as exc:
        raise BenchError("invalid_manifest", str(exc)) from exc


def stage_pair(path, run):
    manifest, images = validate_pair(path)
    run.reserve(sum(len(raw) for raw, _ in images.values()))
    (run.path / "firmware").mkdir()
    for core, (raw, _) in images.items():
        (run.path / "firmware" / (core + ".elf")).write_bytes(raw)
    run.write_json("build-info.json", manifest)
    return {"build": manifest, "provenance": "manifest_declared_not_target_reported",
            "load_ranges": {core: ranges for core, (_, ranges) in images.items()}}
