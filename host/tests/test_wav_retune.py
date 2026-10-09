import contextlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from spookybench.cli import main
from spookybench.wav_retune import classify, parse_retunes, verdict, zero_runs

ORIGIN = (1, 1000000)


def write_wav(path, radio):
    """3-channel 16-bit 48 kHz WAV; radio is one value per frame on both channels."""
    data = bytearray()
    for value in radio:
        data += struct.pack("<hhh", value, value, 7)
    header = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " + \
        struct.pack("<IHHIIHH", 16, 1, 3, 48000, 288000, 6, 16) + \
        b"data" + struct.pack("<I", len(data))
    path.write_bytes(header + bytes(data))


def line(start, end, band="FM", outcome="TUNED"):
    epoch, origin = ORIGIN
    return (f"[retune] start={epoch}:{origin + start} end={epoch}:{origin + end} unc=3600 "
            f"band={band} target=99100 outcome={outcome} freq=99100")


class ZeroRuns(unittest.TestCase):
    def test_finds_runs_of_at_least_one_millisecond_on_both_channels(self):
        left = [1] * 10 + [0] * 48 + [1] * 5 + [0] * 47 + [2] + [0] * 60
        right = list(left)
        right[20] = 3  # one channel nonzero splits the first run below 1 ms
        self.assertEqual(zero_runs(left, right), [(111, 170)])
        self.assertEqual(zero_runs(left, left), [(10, 57), (111, 170)])


class Classification(unittest.TestCase):
    def test_overhang_past_end_sets_a_half_buffer_margin(self):
        tunes, _ = parse_retunes([line(100, 400), line(1000, 1300, band="AM")], ORIGIN)
        metrics = classify([(150, 450), (1100, 1900)], tunes, 5000)
        self.assertEqual(metrics["unexplained"], 0)
        self.assertEqual(metrics["max_overhang_frames"], {"FM": 50, "AM": 600})
        self.assertEqual(metrics["settle_margin_frames"], {"FM": 512, "AM": 1024})
        self.assertEqual(verdict(dict(metrics, unstamped_tunes=0))[0], "pass")

    def test_run_outside_every_interval_fails_and_runs_before_the_first_tune_are_ignored(self):
        tunes, _ = parse_retunes([line(1000, 1200)], ORIGIN)
        metrics = classify([(10, 100), (3000, 3100)], tunes, 5000)
        self.assertEqual(metrics["zero_runs_checked"], 1)
        self.assertEqual(metrics["unexplained_examples"],
                         [{"first_frame": 3000, "frames": 101, "after_end_frames": 1800}])
        self.assertEqual(verdict(dict(metrics, unstamped_tunes=0))[1], "zero_run_outside_retune")

    def test_unstamped_and_other_epoch_lines(self):
        lines = ["[retune] start=1:- end=1:2000 unc=3600 band=FM target=1 outcome=TUNED freq=1",
                 "[retune] start=2:5 end=2:9 unc=3600 band=FM target=1 outcome=TUNED freq=1",
                 "noise " + line(10, 20, outcome="FAILED")]
        tunes, unstamped = parse_retunes(lines, ORIGIN)
        self.assertEqual(unstamped, 1)
        self.assertEqual([(t["start"], t["outcome"]) for t in tunes], [(10, "FAILED")])


class Cli(unittest.TestCase):
    def test_offline_command_reports_pass(self):
        with tempfile.TemporaryDirectory() as root:
            wav, log = Path(root, "REC.WAV"), Path(root, "uart.log")
            write_wav(wav, [5] * 2000 + [0] * 300 + [5] * 2000)
            log.write_text(line(1900, 2250) + "\r\n", encoding="utf-8")
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                code = main(["--json", "wav", "retune", "--wav", str(wav), "--log", str(log),
                             "--origin", "1:1000000"])
        value = json.loads(out.getvalue())
        self.assertEqual((code, value["result"], value["execution"]), (0, "pass", "offline"))
        self.assertEqual(value["metrics"]["settle_margin_frames"], {"FM": 512})


if __name__ == "__main__":
    unittest.main()
