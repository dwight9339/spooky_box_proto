"""Windows-first OS seams; lock lifetime belongs to the worker process."""
import os
from pathlib import Path
import stat
import tempfile
import hashlib
from .result import BenchError


def no_links(path):
    path = Path(path).absolute()
    for parent in [path, *path.parents]:
        try:
            info = parent.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
            raise BenchError("unsafe_artifact_path", "Symlinks/junctions are not allowed in artifact paths")


class BenchLock:
    """OS-backed, non-waiting, cross-process lock shared across profiles/run roots.

    Profiles describing the same board must use the same board_id. It is a
    per-user cooperative lock, not a lock against unrelated serial terminals.
    """
    def __init__(self, board_id):
        name = hashlib.sha256(board_id.encode()).hexdigest()[:24]
        self.path = Path(tempfile.gettempdir()) / f"spookybench-{name}.lock"
        self.stream = None

    def __enter__(self):
        no_links(self.path)
        self.stream = self.path.open("a+b")
        self.stream.seek(0, 2)
        if self.stream.tell() == 0:
            self.stream.write(b"0")
            self.stream.flush()
        self.stream.seek(0)
        try:
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(self.stream.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as exc:
            self.stream.close()
            self.stream = None
            raise BenchError("bench_busy", "Another operation owns this board_id") from exc
        return self

    def __exit__(self, *args):
        if self.stream:
            self.stream.close()  # OS releases the lock, including on worker death.


def ports():
    try:
        from serial.tools.list_ports import comports
    except ImportError as exc:
        raise BenchError("dependency_missing", "Install pyserial 3.5 in the bench Python environment") from exc
    found = []
    for info in comports():
        found.append({"port": info.device, "serial_number": info.serial_number,
                      "vid": info.vid, "pid": info.pid, "interface": info.interface,
                      "description": info.description})
        if len(found) > 256:
            raise BenchError("discovery_limit", "Too many serial ports")
    return found


def select_devices(profile, inventory, required):
    selected = {}
    for role in ("probe", "device"):
        criteria = profile[role]
        matches = [p for p in inventory if all(
            (str(p.get(key)).casefold() == value.casefold() if key == "port"
             else p.get(key) == value) for key, value in criteria.items())]
        if len(matches) > 1:
            raise BenchError("device_ambiguous", f"Multiple ports match {role}")
        if not matches and role in required:
            raise BenchError("device_missing", f"No port matches {role}")
        selected[role] = matches[0] if matches else None
    if selected["probe"] and selected["device"] and selected["probe"]["port"].casefold() == selected["device"]["port"].casefold():
        raise BenchError("role_collision", "Probe and device CDC resolve to the same port")
    return selected
