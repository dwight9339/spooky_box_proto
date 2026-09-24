"""Export the pinned dependency from Git objects without changing its checkout.

Usage: python host/tools/export_probe.py --repo ../spookyprobe --output build/probe-package
Then install the resulting source package with pip, as documented in setup.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    lock = json.loads((Path(__file__).resolve().parents[1] / "spookybench/probe-lock.json").read_text())
    # Read and validate every object before writing any output. Never use dirty
    # working-tree files, reset the sibling repo, or fetch from the network.
    files = {}
    expected = {"pyproject.toml": lock["pyproject_sha256"]}
    expected.update({"spookyprobe/" + name: digest for name, digest in lock["modules"].items()})
    for name, digest in expected.items():
        raw = subprocess.check_output(["git", "-C", str(args.repo), "show", f"{lock['commit']}:host/{name}"], timeout=10)
        if hashlib.sha256(raw.replace(b"\r\n", b"\n")).hexdigest() != digest:
            raise SystemExit(f"Pinned source hash mismatch: {name}")
        files[name] = raw
    args.output.mkdir(parents=True, exist_ok=False)
    for name, raw in files.items():
        target = args.output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(raw)
    (args.output / "source-snapshot.json").write_text(json.dumps(lock, indent=2) + "\n")
    print(args.output.resolve())


if __name__ == "__main__":
    main()
