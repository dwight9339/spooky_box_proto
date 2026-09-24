"""Bounded subprocess output and Windows process-tree ownership."""
import ctypes
from ctypes import wintypes
import os
import queue
import subprocess
import threading
import time
from .result import BenchError


class WindowsJob:
    """Parent-owned kill-on-close job; assign worker before allowing it to run."""
    def __init__(self):
        if os.name != "nt":
            raise BenchError("platform_unsupported", "Phase 1B controls require Windows", "unsupported")
        class Basic(ctypes.Structure):
            _fields_ = [("process_time", ctypes.c_int64), ("job_time", ctypes.c_int64),
                        ("flags", wintypes.DWORD), ("min_ws", ctypes.c_size_t),
                        ("max_ws", ctypes.c_size_t), ("active_limit", wintypes.DWORD),
                        ("affinity", ctypes.c_size_t), ("priority", wintypes.DWORD),
                        ("scheduling", wintypes.DWORD)]
        class IO(ctypes.Structure):
            _fields_ = [(name, ctypes.c_uint64) for name in
                        ("read_ops", "write_ops", "other_ops", "read_bytes", "write_bytes", "other_bytes")]
        class Extended(ctypes.Structure):
            _fields_ = [("basic", Basic), ("io", IO), ("process_mem", ctypes.c_size_t),
                        ("job_mem", ctypes.c_size_t), ("peak_process", ctypes.c_size_t),
                        ("peak_job", ctypes.c_size_t)]
        self.api = ctypes.WinDLL("kernel32", use_last_error=True)
        signatures = {
            "CreateJobObjectW": ([ctypes.c_void_p, wintypes.LPCWSTR], wintypes.HANDLE),
            "SetInformationJobObject": ([wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD], wintypes.BOOL),
            "OpenProcess": ([wintypes.DWORD, wintypes.BOOL, wintypes.DWORD], wintypes.HANDLE),
            "AssignProcessToJobObject": ([wintypes.HANDLE, wintypes.HANDLE], wintypes.BOOL),
            "CloseHandle": ([wintypes.HANDLE], wintypes.BOOL),
        }
        for name, (args, result) in signatures.items():
            getattr(self.api, name).argtypes = args
            getattr(self.api, name).restype = result
        self.handle = self.api.CreateJobObjectW(None, None)
        info = Extended()
        info.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        if not self.handle or not self.api.SetInformationJobObject(self.handle, 9, ctypes.byref(info), ctypes.sizeof(info)):
            self.close()
            raise BenchError("process_guard", "Cannot create kill-on-close Windows job")

    def assign(self, pid):
        process = self.api.OpenProcess(0x0100 | 0x0001, False, pid)  # SET_QUOTA | TERMINATE
        try:
            if not process or not self.api.AssignProcessToJobObject(self.handle, process):
                raise BenchError("process_guard", "Cannot contain worker and OpenOCD; no control operation started")
        finally:
            if process:
                self.api.CloseHandle(process)

    def close(self):
        if getattr(self, "handle", None):
            self.api.CloseHandle(self.handle)
            self.handle = None


def run_process(argv, cwd, deadline, emit, cap=1024 * 1024):
    """No shell. Drain a bounded queue; kill/reap on deadline, overflow or disk error."""
    if time.monotonic() >= deadline:
        raise BenchError("operation_timeout", "No time remaining to start tool")
    flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    process = subprocess.Popen(argv, cwd=cwd, stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, creationflags=flags)
    chunks = queue.Queue(maxsize=16)
    done, stop_reader = threading.Event(), threading.Event()
    read_errors = []

    def read():
        try:
            while not stop_reader.is_set():
                data = process.stdout.read1(4096)
                if not data:
                    break
                # Pipe backpressure is safe for a batch tool. Do not mistake a
                # burst of short OpenOCD writes for loss before the main thread
                # gets scheduled; the deadline/output cap still bound the run.
                while not stop_reader.is_set():
                    try:
                        chunks.put(data, timeout=0.05)
                        break
                    except queue.Full:
                        continue
        except OSError as exc:
            read_errors.append(exc)
        finally:
            done.set()

    reader = threading.Thread(target=read, daemon=True)
    reader.start()
    captured = bytearray()
    try:
        while not (done.is_set() and chunks.empty() and process.poll() is not None):
            if time.monotonic() >= deadline:
                raise BenchError("tool_timeout", "Tool exceeded its deadline")
            try:
                data = chunks.get(timeout=0.02)
            except queue.Empty:
                continue
            if len(captured) + len(data) > cap:
                raise BenchError("tool_output_limit", "Tool output cap reached", "fail")
            emit(data)
            captured.extend(data)
        if read_errors:
            raise BenchError("tool_output_error", "Incomplete tool output")
        return process.returncode, bytes(captured)
    finally:
        stop_reader.set()
        if process.poll() is None:
            process.kill()
        try:
            process.wait(timeout=0.5)
        except subprocess.TimeoutExpired as exc:
            raise BenchError("tool_cleanup", "Tool could not be reaped; human intervention required") from exc
        reader.join(0.2)
        if not reader.is_alive():
            process.stdout.close()
