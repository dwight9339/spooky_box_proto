"""Declare a prebuilt pair with explicit provenance; never infer it from Git HEAD."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from spookybench.firmware import MAX_ELF, elf_ranges, validate_pair


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cm7", required=True)
    parser.add_argument("--cm4", required=True)
    parser.add_argument("--preset", required=True, choices=("Debug", "Release", "IpcSmoke", "IpcMismatch"))
    parser.add_argument("--source-revision", required=True, help="full Git SHA of the image build")
    parser.add_argument("--build-id", required=True)
    parser.add_argument("--source-snapshot", help="archived source snapshot/patch for a dirty image build")
    parser.add_argument("--compiler", help="compiler identity from the image build; unknown when omitted")
    parser.add_argument("--build-flag", action="append", help="repeat as --build-flag=-O2, etc.; unknown when omitted")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    snapshot = None
    if args.source_snapshot:
        digest = hashlib.sha256()
        with Path(args.source_snapshot).open("rb") as stream:
            for chunk in iter(lambda: stream.read(65536), b""):
                digest.update(chunk)
        snapshot = digest.hexdigest()
    manifest = {"schema_version": 1, "build_id": args.build_id, "preset": args.preset,
                "source_revision": args.source_revision, "dirty": bool(snapshot),
                "source_snapshot_sha256": snapshot, "compiler": args.compiler,
                "build_flags": args.build_flag, "images": {}}
    for core in ("CM7", "CM4"):
        path = Path(getattr(args, core.lower())).resolve()
        with path.open("rb") as stream:
            raw = stream.read(MAX_ELF + 1)
        if len(raw) > MAX_ELF:
            parser.error("Image exceeds 16 MiB")
        elf_ranges(raw, core)
        manifest["images"][core] = {"path": str(path), "sha256": hashlib.sha256(raw).hexdigest()}
    # Validate the complete declaration before publishing the output.
    import tempfile
    with tempfile.TemporaryDirectory(prefix="spooky-manifest-") as folder:
        temporary = Path(folder) / "build-info.json"
        temporary.write_text(json.dumps(manifest), encoding="utf-8")
        validate_pair(temporary)
    with Path(args.output).open("x", encoding="utf-8") as stream:
        json.dump(manifest, stream, indent=2)
        stream.write("\n")
    print(args.output)


if __name__ == "__main__":
    main()
