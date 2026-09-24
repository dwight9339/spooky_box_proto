import ctypes
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from spookybench.config import load_profile
from spookybench.firmware import elf_ranges, validate_pair
from spookybench.openocd import configuration, interpret, operation_script, LiveCapture
from spookybench.artifacts import Run
from spookybench import serial_io
from spookybench.process_io import run_process
from spookybench.result import BenchError
from spookybench.runner import execute
from spookybench.supervisor import supervise


def image(core):
    base = 0x08000000 if core == "CM7" else 0x08100000
    stack = 0x20020000 if core == "CM7" else 0x10048000
    raw = bytearray(272)
    raw[:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into("<HHIIIIIHHHHHH", raw, 16, 2, 40, 1, base + 9, 52, 0, 0, 52, 32, 1, 0, 0, 0)
    struct.pack_into("<8I", raw, 52, 1, 256, base, base, 16, 16, 5, 4)
    struct.pack_into("<II", raw, 256, stack, base + 9)
    return bytes(raw)


def child_spawner(options, *unused):
    child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"],
                             creationflags=subprocess.CREATE_NO_WINDOW)
    Path(options["pid_file"]).write_text(str(child.pid))
    time.sleep(60)


class ControlTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="spooky control spaces ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.profile = {"schema_version": 1, "board_id": "control-" + uuid.uuid4().hex,
            "artifact_root": str(self.root / "runs"), "probe": {"serial_number": "SIM-PROBE"},
            "device": {"serial_number": "SIM-TARGET"},
            "openocd": {"executable": str(self.root / "tool.exe"), "scripts": str(self.root / "scripts"),
                        "serial_number": "SIM-PROBE", "adapter_khz": 1000},
            "limits": {"run_bytes": 4 * 1024**2, "total_bytes": 64 * 1024**2,
                       "min_free_bytes": 0, "uart_bytes": 65536, "diag_bytes": 65536}}
        self.path = self.root / "profile.json"
        self.path.write_text(json.dumps(self.profile))
        self.manifest = {"schema_version": 1, "build_id": "test-build", "preset": "IpcSmoke",
            "source_revision": "a" * 40, "dirty": False, "source_snapshot_sha256": None, "images": {}}
        for core in ("CM7", "CM4"):
            name = core + " spaces $ [unsafe] {x}.elf"
            raw = image(core)
            (self.root / name).write_bytes(raw)
            self.manifest["images"][core] = {"path": name, "sha256": hashlib.sha256(raw).hexdigest()}
        self.manifest_path = self.root / "build-info.json"
        self.save_manifest()

    def save_manifest(self):
        self.manifest_path.write_text(json.dumps(self.manifest))

    def options(self, command="probe", scenario="happy"):
        return {"command": command, "scenario": scenario, "simulate": True,
            "profile": str(self.path), "manifest": str(self.manifest_path),
            "seconds": None, "deadline": time.monotonic() + 20}

    def test_control_config_rejects_tcl_and_mismatched_identity(self):
        for serial in ("x;reset", "x[exit]", "other", "x\nexit"):
            self.profile["openocd"]["serial_number"] = serial
            self.path.write_text(json.dumps(self.profile))
            with self.assertRaises(BenchError):
                load_profile(self.path)

    def test_pair_and_safe_staging(self):
        result = execute(self.options("flash"))
        self.assertEqual(result["result"], "pass", result)
        self.assertEqual(result["metrics"]["control"]["verified_cores"], ["cm7", "cm4"])
        self.assertEqual(result["metrics"]["final_target_state"], "unknown")
        root = Path(result["artifacts"]["run_dir"])
        for core in ("CM7", "CM4"):
            self.assertEqual((root / "firmware" / (core + ".elf")).read_bytes(), image(core))
        script = (root / "operation.cfg").read_text()
        self.assertNotIn("unsafe", script)
        self.assertNotIn(str(self.root), script)
        self.assertEqual(json.loads(Path(result["artifacts"]["result"]).read_text()), result)

    def test_manifest_rejects_missing_pair_hash_and_dirty_provenance(self):
        original = json.loads(json.dumps(self.manifest))
        for change in (lambda m: m["images"].pop("CM4"),
                       lambda m: m["images"]["CM4"].update(sha256="0" * 64),
                       lambda m: m.update(dirty=True),
                       lambda m: m.update(source_revision="short")):
            self.manifest = json.loads(json.dumps(original))
            change(self.manifest)
            self.save_manifest()
            with self.assertRaises(BenchError):
                validate_pair(self.manifest_path)

    def test_elf_bad_loads_vectors_and_architecture(self):
        for offset, value in ((18, 62), (64, 0x08100000), (68, 99999),
                              (256, 0x24000000), (260, 0x08001001)):
            raw = bytearray(image("CM7"))
            struct.pack_into("<H" if offset == 18 else "<I", raw, offset, value)
            with self.subTest(offset=offset), self.assertRaises(BenchError):
                elf_ranges(raw, "CM7")
        with self.assertRaises(BenchError):
            elf_ranges(image("CM4"), "CM7")
        with self.assertRaises(BenchError):
            elf_ranges(image("CM7")[:80], "CM7")

    def test_probe_unavailable_m4_is_explicit(self):
        result = execute(self.options("probe", "m4-unavailable"))
        self.assertEqual(result["result"], "pass", result)
        self.assertEqual(result["metrics"]["swd"]["cm4"], "unavailable")
        self.assertEqual(result["metrics"]["target_health"], "not_checked")

    def test_reset_and_partial_flash_no_success_or_resume(self):
        self.assertEqual(execute(self.options("reset"))["result"], "pass")
        result = execute(self.options("flash", "partial-flash"))
        self.assertEqual(result["result"], "fail", result)
        self.assertEqual(result["metrics"]["control"]["verified_cores"], ["cm7"])
        self.assertFalse(result["metrics"]["control"]["reset_command_completed"])
        self.assertTrue(result["metrics"]["human_required"])
        self.assertFalse(result["metrics"]["evidence_complete"])
        log = Path(result["artifacts"]["openocd_log"]).read_text()
        self.assertNotIn("SB_RESET", log)

    def test_timeout_and_nonzero_cannot_pass(self):
        for scenario in ("tool-timeout", "tool-failure"):
            result = execute(self.options("reset", scenario))
            self.assertNotEqual(result["result"], "pass")
            self.assertTrue(result["metrics"]["human_required"])
        for code, output in ((1, b"SB_M7 running\nSB_DONE probe\n"), (0, b""),
                              (0, b"SB_M7 unknown\nSB_DONE probe\n")):
            with self.assertRaises(BenchError):
                interpret("probe", code, output, {})

    def test_tool_process_limits_and_disk_failure(self):
        with self.assertRaisesRegex(BenchError, "deadline"):
            run_process([sys.executable, "-c", "import time; time.sleep(60)"],
                        self.root, time.monotonic() + 0.25, lambda b: None)
        with self.assertRaises(BenchError):
            run_process([sys.executable, "-c", "print('x'*9000)"], self.root,
                        time.monotonic() + 5, lambda b: None, cap=4096)
        def broken(data):
            raise OSError("disk full")
        with self.assertRaisesRegex(OSError, "disk full"):
            run_process([sys.executable, "-c", "print('output')"], self.root,
                        time.monotonic() + 5, broken)
        code, raw = run_process([sys.executable, "-c", "print('ok')"], self.root,
                               time.monotonic() + 5, lambda b: None)
        self.assertEqual(code, 0)
        self.assertIn(b"ok", raw)

    def test_short_write_burst_is_not_false_overflow(self):
        code, raw = run_process([sys.executable, "-u", "-c",
            "import os; [os.write(1, b'x\\n') for _ in range(2000)]"],
            self.root, time.monotonic() + 5, lambda b: time.sleep(0.001))
        self.assertEqual(code, 0)
        self.assertEqual(raw, b"x\n" * 2000)

    @unittest.skipUnless(os.name == "nt", "Windows process-tree test")
    def test_supervisor_kills_tool_descendant(self):
        pid_file = self.root / "child.pid"
        result = supervise(dict(self.options(), pid_file=str(pid_file)), 4.0, target=child_spawner)
        self.assertEqual(result["reason"], "operation_timeout", result)
        self.assertTrue(pid_file.exists())
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.OpenProcess.restype = ctypes.c_void_p
        kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        handle = kernel.OpenProcess(0x100000, False, int(pid_file.read_text()))
        if handle:
            try:
                self.assertEqual(kernel.WaitForSingleObject(handle, 1000), 0)
            finally:
                kernel.CloseHandle(handle)

    def test_spawned_simulated_flash(self):
        result = supervise(self.options("flash"), 15)
        self.assertEqual(result["result"], "pass", result)
        self.assertEqual(result["execution"], "simulated")

    def test_manifest_generator_records_explicit_provenance(self):
        helper = Path(__file__).resolve().parents[1] / "tools/make_manifest.py"
        output = self.root / "generated manifest.json"
        snapshot = self.root / "source.patch"
        snapshot.write_bytes(b"declared source changes")
        args = [sys.executable, "-B", str(helper), "--cm7",
                str(self.root / self.manifest["images"]["CM7"]["path"]), "--cm4",
                str(self.root / self.manifest["images"]["CM4"]["path"]),
                "--preset", "IpcSmoke", "--source-revision", "a" * 40,
                "--build-id", "explicit-build", "--source-snapshot", str(snapshot),
                "--compiler", "test-compiler", "--build-flag=-O2", "--output", str(output)]
        result = subprocess.run(args, capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest, _ = validate_pair(output)
        self.assertTrue(manifest["dirty"])
        self.assertEqual(manifest["source_snapshot_sha256"], hashlib.sha256(snapshot.read_bytes()).hexdigest())
        self.assertEqual(manifest["compiler"], "test-compiler")
        self.assertEqual(manifest["build_flags"], ["-O2"])
        previous = output.read_bytes()
        result = subprocess.run(args, capture_output=True, text=True, timeout=5)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(output.read_bytes(), previous)

    def test_no_hardware_calls_in_simulation(self):
        with patch("spookybench.openocd.run_process", side_effect=AssertionError("real tool")), \
             patch("spookybench.serial_io.open_serial", side_effect=AssertionError("real serial")):
            for command in ("probe", "reset", "flash"):
                self.assertEqual(execute(self.options(command))["result"], "pass")

    def test_integrated_capture_readiness_and_close(self):
        class Serial:
            closed = False
            pending = b"boot bytes\r\n"
            def read(self, size):
                time.sleep(0.01)
                raw, self.pending = self.pending, b""
                return raw
            def close(self):
                self.closed = True
        serial = Serial()
        profile, raw = load_profile(self.path)
        run = Run(profile, raw, {})
        run.execution = "simulated"
        metrics, artifacts = {}, {}
        capture = LiveCapture(run, metrics, artifacts, time.monotonic() + 5)
        with patch.object(serial_io, "open_serial", return_value=serial):
            capture.start("fake")
            self.assertTrue(capture.ready.is_set())
            time.sleep(0.05)
            capture.close()
        self.assertTrue(serial.closed)
        self.assertEqual((run.path / "uart/raw.bin").read_bytes(), b"boot bytes\r\n")
        self.assertEqual(metrics["capture"]["archived_bytes"], 12)

    def test_hash_failure_precedes_tool_invocation(self):
        self.manifest["images"]["CM4"]["sha256"] = "0" * 64
        self.save_manifest()
        with patch("spookybench.openocd.verify_tool", side_effect=AssertionError("tool inspected")), \
             patch("spookybench.openocd.run_process", side_effect=AssertionError("tool started")):
            result = execute(self.options("flash"))
        self.assertEqual(result["reason"], "firmware_hash")

    def test_installed_openocd_configuration_and_failure_tcl(self):
        root = Path.home() / ".platformio/packages/tool-openocd"
        exe = root / "bin/openocd.exe"
        if not exe.is_file():
            self.skipTest("Pinned Windows OpenOCD not installed")
        # Configuration exits before init: no USB/SWD access.
        script = self.root / "config test.cfg"
        script.write_text(configuration(self.profile["openocd"], True) + "echo CONFIG_OK\nshutdown\n")
        result = subprocess.run([str(exe), "-s", str(root / "openocd/scripts"), "-f", str(script)],
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("CONFIG_OK", result.stdout + result.stderr)
        # Exercise actual Jim Tcl catch/shutdown control flow with all target
        # operations mocked. Failure of CM4 verify must never reach reset run.
        mocks = '''proc init {} {}
proc targets {args} {}
proc stm32h7x.cpu0 {args} {if {[lindex $args 0] == "curstate"} {return halted}}
proc reset {mode} {echo "RESET_MODE $mode"}
proc halt {args} {}
proc flash {args} {}
proc verify_image {path} {if {$path == "firmware/CM4.elf"} {error TEST_VERIFY_FAILURE}}
'''
        script.write_text(mocks + operation_script("flash"))
        result = subprocess.run([str(exe), "-f", str(script)], capture_output=True, text=True, timeout=5)
        output = result.stdout + result.stderr
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("SB_STAGE verified_cm7", output)
        self.assertIn("SB_FAILED", output)
        self.assertNotIn("RESET_MODE run", output)
        self.assertNotIn("SB_DONE flash", output)


if __name__ == "__main__":
    unittest.main()
