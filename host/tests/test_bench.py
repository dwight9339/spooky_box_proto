import contextlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from spookybench import fake
from spookybench.cli import main
from spookybench.config import load_profile
from spookybench.dependency import verify_probe
from spookybench.platform_io import BenchLock, select_devices
from spookybench.result import BenchError, exit_code
from spookybench.runner import execute
from spookybench.serial_io import diagnostics
from spookybench.supervisor import supervise


def stuck_worker(options, *unused):
    with BenchLock(options["test_board"]):
        time.sleep(60)


class BenchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="spooky bench tests ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.profile = {"schema_version": 1, "board_id": "test-" + uuid.uuid4().hex,
                        "artifact_root": str(self.root / "runs"),
                        "probe": {"serial_number": "SIM-PROBE"},
                        "device": {"serial_number": "SIM-TARGET"},
                        "limits": {"run_bytes": 524288, "total_bytes": 8 * 1024**2,
                                   "min_free_bytes": 0, "uart_bytes": 65536, "diag_bytes": 65536}}
        self.path = self.root / "profile.json"
        self.save()

    def save(self):
        self.path.write_text(json.dumps(self.profile), encoding="utf-8")

    def options(self, command="status", scenario="happy"):
        return {"profile": str(self.path), "simulate": True, "scenario": scenario,
                "command": command, "seconds": 0.3, "deadline": time.monotonic() + 15}

    def run_direct(self, command="status", scenario="happy"):
        return execute(self.options(command, scenario))

    def test_profile_limits_and_duplicates(self):
        original = dict(self.profile["limits"])
        for value in (-1, float("nan"), float("inf"), True, 0, 2**50):
            with self.subTest(value=value):
                self.profile["limits"]["run_bytes"] = value
                self.save()
                with self.assertRaises(BenchError):
                    load_profile(self.path)
        self.profile["limits"] = original
        self.profile["probe"] = {"vid": 11914, "pid": 12}
        self.save()
        with self.assertRaises(BenchError):
            load_profile(self.path)
        self.path.write_text('{"schema_version":1,"schema_version":1}')
        with self.assertRaisesRegex(BenchError, "Duplicate"):
            load_profile(self.path)

    def test_selection_renumber_ambiguity_and_collision(self):
        ports = fake.inventory("happy")
        ports[0]["port"] = "COM99"
        self.assertEqual(select_devices(self.profile, ports, ("probe",))["probe"]["port"], "COM99")
        with self.assertRaisesRegex(BenchError, "Multiple"):
            select_devices(self.profile, fake.inventory("ambiguous"), ("probe",))
        with self.assertRaisesRegex(BenchError, "No port"):
            select_devices(self.profile, [], ("device",))
        self.profile["device"] = {"port": "com99"}
        with self.assertRaisesRegex(BenchError, "same port"):
            select_devices(self.profile, ports, ("device",))

    def test_status_artifacts_and_provenance(self):
        result = self.run_direct()
        self.assertEqual(result["result"], "pass")
        self.assertEqual(result["execution"], "simulated")
        self.assertEqual(result["reason"], "simulated")
        self.assertEqual(result["metrics"]["target_health"], "not_checked")
        self.assertFalse(result["metrics"]["capabilities"]["flash"])
        self.assertEqual(json.loads(Path(result["artifacts"]["result"]).read_text()), result)
        self.assertTrue((Path(result["artifacts"]["run_dir"]) / "profile.json").exists())
        self.assertTrue((Path(result["artifacts"]["run_dir"]) / "bench-source.json").exists())
        self.assertEqual(len(result["metrics"]["tools"]["source_sha256"]), 64)

    def test_missing_and_ambiguous_are_not_pass(self):
        for scenario, reason in (("missing", "device_missing"), ("ambiguous", "device_ambiguous")):
            result = self.run_direct(scenario=scenario)
            self.assertEqual(result["reason"], reason)
            self.assertEqual(exit_code(result), 2)
            self.assertTrue(Path(result["artifacts"]["result"]).exists())

    def test_all_diagnostic_commands(self):
        for command in ("log status", "diag status", "diag last", "diag dump",
                        "diag identity"):
            with self.subTest(command=command):
                result = self.run_direct(command)
                self.assertEqual(result["result"], "pass", result)
                records = [json.loads(line) for line in Path(result["artifacts"]["diagnostics"]).read_text().splitlines()]
                self.assertEqual(records[0]["kind"], "session_start")
                self.assertEqual(records[-1]["kind"], "session_end")
                self.assertTrue(any("raw_base64" in record for record in records))
                if command == "diag dump":
                    self.assertTrue(result["metrics"]["response"]["complete"])
                if command == "diag identity":
                    identity = result["metrics"]["firmware_identity"]["target"]
                    self.assertEqual(identity["state"], "observed")
                    self.assertEqual(identity["build_id"], "test-build")
                    self.assertEqual(identity["boot_epoch"], 1)

    def test_identity_rejects_unsupported_schema(self):
        result = self.run_direct("diag identity", "identity-v2")
        self.assertEqual(result["result"], "fail", result)
        self.assertEqual(result["reason"], "protocol_error")

    def test_identity_preserves_numeric_build_id(self):
        result = self.run_direct("diag identity", "identity-numeric")
        self.assertEqual(result["result"], "pass", result)
        identity = result["metrics"]["firmware_identity"]["target"]
        self.assertEqual(identity["build_id"], "20260929")
        self.assertIsInstance(identity["build_id"], str)

    def test_identity_errors_do_not_wait_for_timeout(self):
        result = self.run_direct("diag identity", "identity-uninitialized")
        self.assertEqual((result["result"], result["reason"]),
                         ("fail", "protocol_error"), result)
        result = self.run_direct("diag identity", "identity-unsupported")
        self.assertEqual((result["result"], result["reason"]),
                         ("unsupported", "identity_unsupported"), result)

    def test_incomplete_schema_disconnect_deadlines(self):
        for scenario, reason in (("incomplete", "request_timeout"), ("invalid-schema", "protocol_error"),
                                 ("disconnect", "io_error"), ("no-response", "request_timeout")):
            result = self.run_direct("diag dump", scenario)
            self.assertEqual(result["reason"], reason, result)
            self.assertNotEqual(result["result"], "pass")
            self.assertFalse(result["metrics"]["evidence_complete"])
            if scenario == "incomplete":
                records = [json.loads(line) for line in Path(result["artifacts"]["diagnostics"]).read_text().splitlines()]
                failed = next(r for r in records if r["kind"] == "request_failed")
                self.assertFalse(failed["dump"]["complete"])

    def test_capture_raw_and_session(self):
        result = self.run_direct("console")
        self.assertEqual(result["result"], "pass", result)
        folder = Path(result["artifacts"]["uart"])
        raw = (folder / "raw.bin").read_bytes()
        self.assertIn(b"\xffpartial", raw)
        index = [json.loads(line) for line in (folder / "chunks.jsonl").read_text().splitlines()]
        self.assertEqual(sum(r["length"] for r in index), len(raw))
        session = json.loads((folder / "session.json").read_text())
        self.assertEqual(session["execution"], "simulated")
        self.assertEqual(result["metrics"]["capture"]["archived_bytes"], len(raw))

    def test_capture_disconnect_and_cap(self):
        result = self.run_direct("console", "disconnect")
        self.assertEqual(result["reason"], "io_error")
        self.profile["limits"]["uart_bytes"] = 32
        self.save()
        result = self.run_direct("console")
        self.assertEqual(result["reason"], "artifact_limit")
        self.assertEqual(result["result"], "fail")
        self.assertEqual((Path(result["artifacts"]["uart"]) / "raw.bin").stat().st_size, 0)

    def test_queue_loss_is_visible(self):
        from spookyprobe.capture import Archive
        def drop(archive, data, *args):
            archive.dropped_bytes += len(data)
            archive.dropped_chunks += 1
            return False
        with patch.object(Archive, "submit", drop):
            result = self.run_direct("console")
        self.assertEqual(result["reason"], "capture_loss")
        self.assertGreater(result["metrics"]["capture"]["host_dropped_bytes"], 0)

    def test_disk_failure_is_visible(self):
        with patch("spookybench.artifacts.JsonLines.emit", side_effect=OSError("disk full")):
            result = self.run_direct("diag status")
        self.assertEqual(result["reason"], "io_error")
        with patch("spookybench.artifacts.Run.write_json", side_effect=OSError("disk full")):
            result = self.run_direct()
        self.assertEqual(result["reason"], "io_error")
        self.assertNotIn("result", result["artifacts"])

    def test_quota_counts_unknown_and_incomplete_files(self):
        self.profile["limits"]["total_bytes"] = self.profile["limits"]["run_bytes"]
        self.save()
        root = Path(self.profile["artifact_root"])
        root.mkdir()
        (root / "unknown-incomplete.bin").write_bytes(b"x")
        result = self.run_direct()
        self.assertEqual(result["reason"], "storage_quota")
        self.assertEqual((root / "unknown-incomplete.bin").read_bytes(), b"x")

    def test_free_space_and_diagnostic_stream_cap(self):
        class Disk:
            free = 1
        with patch("spookybench.artifacts.shutil.disk_usage", return_value=Disk()):
            self.assertEqual(self.run_direct()["reason"], "storage_free_space")
        self.profile["limits"]["diag_bytes"] = 200
        self.save()
        result = self.run_direct("diag dump")
        self.assertEqual(result["reason"], "artifact_limit")
        self.assertLessEqual(Path(result["artifacts"]["diagnostics"]).stat().st_size, 200)

    def test_lock_contention_and_release(self):
        with BenchLock(self.profile["board_id"]):
            self.assertEqual(self.run_direct()["reason"], "bench_busy")
        self.assertEqual(self.run_direct()["result"], "pass")

    def test_synchronization_archives_unsolicited_data(self):
        clock = fake.Clock()
        serial = fake.Serial(clock, "device")
        serial.pending = b"unrelated text\r\npartial"
        records = []
        answer = diagnostics(serial, "DIAG STATUS", records.append, clock, 14.0)
        self.assertEqual(answer["kind"], "OK DIAG")
        before = [r for r in records if r.get("phase") == "before_commands"]
        self.assertEqual(len(before), 2)
        self.assertFalse(before[1]["complete_line"])
        self.assertEqual(serial.writes, [b"DIAG STATUS\n"])
        self.assertGreaterEqual(clock.now(), 5.2)

    def test_never_quiet_and_incomplete_status(self):
        clock = fake.Clock()
        serial = fake.Serial(clock, "device")
        def endless(size):
            clock.sleep(0.1)
            return b"noise\r\n"
        serial.read = endless
        with self.assertRaisesRegex(BenchError, "quiet"):
            diagnostics(serial, "DIAG STATUS", lambda r: None, clock, 14)
        self.assertEqual(serial.writes, [])
        clock = fake.Clock()
        serial = fake.Serial(clock, "device")
        def bad_write(data):
            serial.pending = b"OK LOG QUEUED=0\r\n"
            return len(data)
        serial.write = bad_write
        with self.assertRaisesRegex(BenchError, "incomplete status"):
            diagnostics(serial, "LOG STATUS", lambda r: None, clock, 14)

    def test_dependency_pin(self):
        self.assertEqual(verify_probe()["source_commit"], "ce039cab6d15171aa069991dd743e1e14e64aeef")
        import spookyprobe
        with patch.object(spookyprobe, "__file__", str(self.root / "__init__.py")):
            with self.assertRaisesRegex(BenchError, "Missing"):
                verify_probe()

    def test_artifact_root_cannot_be_in_checkout(self):
        self.profile["artifact_root"] = str(Path(__file__).resolve().parents[2] / "build/bench-test-runs")
        self.save()
        self.assertEqual(self.run_direct()["reason"], "unsafe_artifact_path")

    def test_symlink_or_junction_rejected(self):
        # Emulate the Windows reparse-point attribute without requiring link privileges.
        import stat
        class Info:
            st_mode = stat.S_IFDIR
            st_file_attributes = 0x400
        with patch("spookybench.platform_io.Path.lstat", return_value=Info()):
            from spookybench.platform_io import no_links
            with self.assertRaisesRegex(BenchError, "junctions"):
                no_links(self.root)

    def test_serial_closed_after_operation_failure(self):
        instances = []
        serial_type = fake.Serial
        def make(clock, role, scenario):
            instance = serial_type(clock, role, "disconnect")
            instances.append(instance)
            return instance
        with patch("spookybench.runner.fake.Serial", side_effect=make):
            result = self.run_direct("diag status")
        self.assertEqual(result["reason"], "io_error")
        self.assertTrue(instances[0].closed)

    def test_json_errors_and_unsupported(self):
        for argv, code, reason in ((["console", "--seconds", "nan"], 2, "invalid_invocation"),
                                  (["console", "--seconds", "inf"], 2, "invalid_invocation"),
                                  (["console", "--seconds", "-1"], 2, "invalid_invocation"),
                                  (["diag", "status"], 2, "invalid_invocation"),
                                  (["reset"], 2, "invalid_invocation"),
                                  (["test", "ipc-load", "--seconds", "9"], 2, "invalid_invocation"),
                                  (["test", "sd-basic", "--size-mib", "65"], 2, "invalid_invocation"),
                                  (["test", "sd-basic", "--size-mib", "64", "--passes", "3"], 2, "invalid_invocation"),
                                  (["wav", "inspect", "--file", "other.wav"], 2, "invalid_invocation"),
                                  (["power"], 3, "not_implemented")):
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                actual = main(["--json", *argv])
            self.assertEqual(actual, code)
            self.assertEqual(len(output.getvalue().splitlines()), 1)
            self.assertEqual(json.loads(output.getvalue())["reason"], reason)

    def test_spawned_cli_with_spaces_and_saved_result(self):
        environment = os.environ.copy()
        environment["PYTHONPATH"] = os.pathsep.join(sys.path)
        command = [sys.executable, "-B", "-m", "spookybench", "--json", "--simulate",
                   "--profile", str(self.path), "diag", "dump"]
        process = subprocess.run(command, capture_output=True, text=True, env=environment, timeout=20)
        self.assertEqual(process.returncode, 0, process.stderr + process.stdout)
        self.assertEqual(len(process.stdout.splitlines()), 1)
        value = json.loads(process.stdout)
        self.assertEqual(value, json.loads(Path(value["artifacts"]["result"]).read_text()))
        self.assertTrue(value["metrics"]["response"]["complete"])

    def test_worker_timeout_releases_lock(self):
        options = dict(self.options(), test_board=self.profile["board_id"])
        started = time.monotonic()
        result = supervise(options, 3.0, target=stuck_worker)
        self.assertEqual(result["reason"], "operation_timeout")
        self.assertEqual(result["metrics"]["cleanup"], "terminated")
        self.assertLess(time.monotonic() - started, 5.0)
        with BenchLock(self.profile["board_id"]):
            pass


if __name__ == "__main__":
    unittest.main()
