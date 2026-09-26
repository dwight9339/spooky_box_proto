import contextlib
import io
import json
from pathlib import Path
import random
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from spookybench.cli import main
from spookybench.wav_align import analyze

RATE = 48000


def program(frames, seed=1):
    """Band-limited noise standing in for radio program material."""
    rng = random.Random(seed)
    value, out = 0.0, []
    for _ in range(frames):
        value = 0.7 * value + rng.uniform(-3000.0, 3000.0)
        out.append(value)
    return out


def write_wav(path, radio, mic):
    frames = len(radio)
    data = bytearray()
    for left, microphone in zip(radio, mic):
        sample = max(-32768, min(32767, int(left)))
        data += struct.pack("<hhh", sample, sample,
                            max(-32768, min(32767, int(microphone))))
    header = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE"
    header += b"fmt " + struct.pack("<IHHIIHH", 16, 1, 3, RATE, RATE * 6, 6, 16)
    header += b"data" + struct.pack("<I", len(data))
    path.write_bytes(header + bytes(data))
    return frames


def delayed(radio, lag, gain=0.5, noise=200.0, seed=2):
    rng = random.Random(seed)
    return [gain * (radio[n - lag] if 0 <= n - lag < len(radio) else 0.0) +
            rng.uniform(-noise, noise) for n in range(len(radio))]


class WavAlignTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name) / "REC000.WAV"
        self.radio = program(8 * RATE)

    def tearDown(self):
        self.directory.cleanup()

    def measure(self, mic):
        write_wav(self.path, self.radio, mic)
        return analyze(self.path, window_seconds=1.0, hop_seconds=2.0, max_lag_ms=30.0)

    def test_constant_positive_lag(self):
        result = self.measure(delayed(self.radio, 700))["alignment"]
        self.assertEqual(result["detected_windows"], result["windows"])
        self.assertEqual((result["lag_min_frames"], result["lag_max_frames"]), (700, 700))
        self.assertEqual(result["drift_frames"], 0)
        self.assertTrue(result["drift_reliable"])
        self.assertEqual(result["polarity"], 1)

    def test_microphone_leading_radio(self):
        result = self.measure(delayed(self.radio, -150))["alignment"]
        self.assertEqual(result["lag_median_frames"], -150)

    def test_one_frame_slip_is_reported_as_drift(self):
        early, late = delayed(self.radio, 300), delayed(self.radio, 301)
        middle = 4 * RATE
        result = self.measure(early[:middle] + late[middle:])["alignment"]
        self.assertEqual(result["lag_spread_frames"], 1)
        self.assertEqual(result["drift_frames"], 1)

    def test_even_polarity_split_does_not_claim_drift(self):
        mic = delayed(self.radio, 300)
        middle = 4 * RATE
        result = self.measure(mic[:middle] + [-value for value in mic[middle:]])["alignment"]
        self.assertEqual(result["endpoint_delta_frames"], 0)
        self.assertFalse(result["polarity_consistent"])
        self.assertFalse(result["drift_reliable"])
        self.assertIsNone(result["drift_frames"])
        self.assertIsNone(result["drift_ppm"])
        self.assertEqual(result["drift_reason"], "phase_ambiguous")

    def test_isolated_opposite_phase_windows_are_excluded_from_drift(self):
        detected = [
            {"start_seconds": 0.0, "lag_frames": 300, "polarity": 1},
            {"start_seconds": 2.0, "lag_frames": 300, "polarity": 1},
            {"start_seconds": 4.0, "lag_frames": 304, "polarity": -1},
            {"start_seconds": 6.0, "lag_frames": 300, "polarity": 1},
        ]
        summary = {}
        from spookybench.wav_align import _add_drift_summary
        _add_drift_summary(summary, detected)
        self.assertFalse(summary["polarity_consistent"])
        self.assertEqual(summary["drift_polarity"], 1)
        self.assertEqual(summary["drift_excluded_windows"], 1)
        self.assertEqual(summary["drift_phase_fraction"], 0.75)
        self.assertTrue(summary["drift_reliable"])
        self.assertEqual(summary["drift_frames"], 0)

    def test_nonmonotonic_lags_do_not_claim_drift(self):
        detected = [
            {"start_seconds": 0.0, "lag_frames": 300, "polarity": 1},
            {"start_seconds": 2.0, "lag_frames": 302, "polarity": 1},
            {"start_seconds": 4.0, "lag_frames": 301, "polarity": 1},
        ]
        summary = {}
        from spookybench.wav_align import _add_drift_summary
        _add_drift_summary(summary, detected)
        self.assertFalse(summary["lag_monotonic"])
        self.assertFalse(summary["drift_reliable"])
        self.assertEqual(summary["drift_reason"], "lag_not_continuous")

    def test_one_frame_jitter_is_stable(self):
        detected = [
            {"start_seconds": 0.0, "lag_frames": 300, "polarity": 1},
            {"start_seconds": 2.0, "lag_frames": 299, "polarity": 1},
            {"start_seconds": 4.0, "lag_frames": 300, "polarity": 1},
        ]
        summary = {}
        from spookybench.wav_align import _add_drift_summary
        _add_drift_summary(summary, detected)
        self.assertFalse(summary["lag_monotonic"])
        self.assertTrue(summary["lag_stable"])
        self.assertTrue(summary["drift_reliable"])
        self.assertEqual(summary["drift_frames"], 0)

    def test_inverted_microphone_keeps_lag(self):
        result = self.measure([-value for value in delayed(self.radio, 512)])["alignment"]
        self.assertEqual(result["lag_median_frames"], 512)
        self.assertEqual(result["polarity"], -1)

    def test_uncorrelated_microphone_is_not_detected(self):
        result = self.measure(program(8 * RATE, seed=9))["alignment"]
        self.assertEqual(result["detected_windows"], 0)
        self.assertNotIn("lag_median_frames", result)

    def test_cli_reports_offline_pass_and_fail(self):
        write_wav(self.path, self.radio, delayed(self.radio, 256))
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            code = main(["--json", "wav", "align", "--wav", str(self.path),
                         "--window-seconds", "1", "--hop-seconds", "2",
                         "--max-lag-ms", "30"])
        value = json.loads(output.getvalue())
        self.assertEqual((code, value["result"], value["execution"]), (0, "pass", "offline"))
        self.assertEqual(value["metrics"]["alignment"]["lag_median_frames"], 256)

        write_wav(self.path, self.radio, program(8 * RATE, seed=9))
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            code = main(["--json", "wav", "align", "--wav", str(self.path),
                         "--window-seconds", "1", "--hop-seconds", "2"])
        value = json.loads(output.getvalue())
        self.assertEqual((code, value["result"], value["reason"]),
                         (1, "fail", "alignment_not_detected"))

    def test_cli_rejects_missing_file(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            code = main(["--json", "wav", "align", "--wav", str(self.path)])
        value = json.loads(output.getvalue())
        self.assertEqual((code, value["reason"]), (2, "invalid_invocation"))


if __name__ == "__main__":
    unittest.main()
