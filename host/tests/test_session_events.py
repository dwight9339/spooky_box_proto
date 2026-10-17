from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from spookybench import session_events, wav_retune
from spookybench.result import BenchError

HEADER = b"SPKYEVTS" + struct.pack("<HHHHI12x", 1, 0, 32, 32, 48000)


def record(sequence, frame, kind, payload=b"", uncertainty=0, flags=0):
    return struct.pack("<QIHHI", frame, uncertainty, kind, flags, sequence) + \
        payload.ljust(12, b"\x00")


def start(sequence=0):
    return record(sequence, 0, 0x0001, struct.pack("<B3xI", 0, 99100))


def end(sequence, frame):
    return record(sequence, frame, 0x0002, bytes([1]))


class Decode(unittest.TestCase):
    def test_design_example_record(self):
        # docs/design/session-format.md worked example.
        raw = bytes.fromhex("0904000000000000100e000011000000050000000001170280830100"
                            "80830100")
        header, records = session_events.decode(HEADER + raw)
        self.assertEqual(header, {"version": "1.0", "sample_rate_hz": 48000})
        self.assertEqual(records, [{"sequence": 5, "frame": 1033, "uncertainty": 3600,
                                    "kind": "tune_end", "flags": 0, "band": "FM",
                                    "outcome": "tuned", "rssi_dbuv": 23, "snr_db": 2,
                                    "target_khz": 99200, "frequency_khz": 99200}])

    def test_classic_and_unknown_kinds(self):
        classic = record(1, 0, 0x0040, struct.pack("<6B3H", 255, 4, 0, 1, 3, 30, 400, 180, 20))
        _header, records = session_events.decode(HEADER + classic +
                                                 record(2, 5, 0x8001, b"x"))
        self.assertEqual(records[0]["event"], "snapshot")
        self.assertEqual((records[0]["run_state"], records[0]["edge"],
                          records[0]["direction_up"], records[0]["rate_limited"],
                          records[0]["hold_seconds"], records[0]["rate_setting_per_min"],
                          records[0]["rate_per_min"], records[0]["distance_channels"]),
                         ("holding", "bounce", True, True, 30, 400, 180, 20))
        self.assertEqual(records[1]["kind"], "0x8001")

    def test_rejects_bad_files(self):
        for data in (b"", HEADER[:31], HEADER + b"x", b"NOTEVTS!" + HEADER[8:]):
            with self.subTest(size=len(data)):
                with self.assertRaises(BenchError):
                    session_events.decode(data)


class Summary(unittest.TestCase):
    def summarize(self, *records):
        return session_events.summarize(session_events.decode(HEADER + b"".join(records))[1])

    def test_clean_log(self):
        summary = self.summarize(start(), end(1, 999))
        self.assertEqual((summary["problems"], summary["last_frame"]), ([], 999))

    def test_reported_loss_is_consistent(self):
        lost = record(4, 50, 0x00F0, struct.pack("<I", 2))
        summary = self.summarize(start(), record(1, 10, 0x0030), lost, end(5, 999))
        self.assertEqual((summary["sequence_gaps"], summary["records_dropped"],
                          summary["problems"]), (2, 2, []))

    def test_rule_violations(self):
        summary = self.summarize(record(0, 5, 0x0030), record(2, 10, 0x0031),
                                 record(2, 2000, 0x0030), end(3, 999))
        self.assertIn("does not open with session start at frame 0", summary["problems"])
        self.assertIn("sequence numbers are not increasing", summary["problems"])
        self.assertIn("a record lies after session end", summary["problems"])
        self.assertTrue(any(p.startswith("sequence gaps") for p in summary["problems"]))
        self.assertIn("does not close with session end",
                      self.summarize(start())["problems"])

    def test_tune_pairs(self):
        tune_start = record(1, 100, 0x0010, struct.pack("<B3xI", 1, 1000))
        tune_end = record(2, 400, 0x0011, struct.pack("<4B2I", 1, 1, 30, 2, 1000, 1000), 3600)
        records = session_events.decode(HEADER + start() + tune_start + tune_end +
                                        end(3, 999))[1]
        self.assertEqual(session_events.tunes(records),
                         [{"start": 100, "end": 400, "band": "AM", "outcome": "tuned",
                           "target_khz": 1000}])


    def test_tune_open_at_session_end_runs_to_the_end(self):
        tune_start = record(1, 900, 0x0010, struct.pack("<B3xI", 1, 720))
        records = session_events.decode(HEADER + start() + tune_start + end(2, 999))[1]
        self.assertEqual(session_events.tunes(records),
                         [{"start": 900, "end": 999, "band": "AM", "outcome": "open",
                           "target_khz": 720}])


class Gaps(unittest.TestCase):
    def test_gap_pairs_and_open_gap(self):
        gap_start = record(1, 100, 0x0020, bytes([1, 0]))
        gap_end = record(2, 611, 0x0021, bytes([1, 1, 0, 0]) + struct.pack("<I", 1000))
        late = record(3, 900, 0x0020, bytes([1, 1]))
        records = session_events.decode(HEADER + start() + gap_start + gap_end + late +
                                        end(4, 999))[1]
        self.assertEqual(session_events.gaps(records), [
            {"start": 100, "end": 611, "band_before": "FM", "band_after": "AM",
             "frequency_khz": 1000, "open": False},
            {"start": 900, "end": 999, "band_before": "AM", "band_after": None,
             "frequency_khz": None, "open": True}])


class RetuneFromEvents(unittest.TestCase):
    def test_wav_retune_reads_events(self):
        with tempfile.TemporaryDirectory() as root:
            wav, events = Path(root, "AUDIO000.WAV"), Path(root, "EVENTS.BIN")
            radio = [5] * 2000 + [0] * 300 + [5] * 2000
            data = b"".join(struct.pack("<hhh", value, value, 7) for value in radio)
            wav.write_bytes(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " +
                            struct.pack("<IHHIIHH", 16, 1, 3, 48000, 288000, 6, 16) +
                            b"data" + struct.pack("<I", len(data)) + data)
            events.write_bytes(HEADER + start() +
                               record(1, 1900, 0x0010, struct.pack("<B3xI", 0, 99200)) +
                               record(2, 2250, 0x0011,
                                      struct.pack("<4B2I", 0, 1, 20, 3, 99200, 99200), 3600) +
                               end(3, len(radio) - 1))
            metrics, verdict = wav_retune.run_local({"wav": str(wav), "events": str(events)})
        self.assertEqual(verdict[0], "pass")
        self.assertEqual((metrics["source"], metrics["tunes_in_file"],
                          metrics["settle_margin_frames"]), ("events", 1, {"FM": 512}))

    def write(self, root, radio, *events):
        wav, path = Path(root, "AUDIO000.WAV"), Path(root, "EVENTS.BIN")
        data = b"".join(struct.pack("<hhh", value, value, 7) for value in radio)
        wav.write_bytes(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " +
                        struct.pack("<IHHIIHH", 16, 1, 3, 48000, 288000, 6, 16) +
                        b"data" + struct.pack("<I", len(data)) + data)
        path.write_bytes(HEADER + start() + b"".join(events) + end(9, len(radio) - 1))
        return {"wav": str(wav), "events": str(path)}

    def test_gaps_explain_their_silence_and_must_be_silent(self):
        tune = (record(1, 100, 0x0010, struct.pack("<B3xI", 0, 99200)) +
                record(2, 150, 0x0011, struct.pack("<4B2I", 0, 1, 20, 3, 99200, 99200)))
        gap = (record(3, 2048, 0x0020, bytes([1, 0])) +
               record(4, 2559, 0x0021, bytes([1, 1, 0, 0]) + struct.pack("<I", 1000)))
        with tempfile.TemporaryDirectory() as root:
            radio = [5] * 2048 + [0] * 512 + [5] * 1000
            metrics, verdict = wav_retune.run_local(self.write(root, radio, tune, gap))
            self.assertEqual(verdict[0], "pass")
            self.assertEqual((metrics["explained_by_gaps"], metrics["gaps"][0]["nonzero_frames"]),
                             (1, 0))
            radio[2300] = 3
            metrics, verdict = wav_retune.run_local(self.write(root, radio, tune, gap))
            self.assertEqual(verdict[1], "gap_not_silent")

    def test_mute_into_gap_and_per_band_counts(self):
        tune = (record(1, 100, 0x0010, struct.pack("<B3xI", 0, 99200)) +
                record(2, 300, 0x0011, struct.pack("<4B2I", 0, 1, 20, 3, 99200, 99200)))
        gap = (record(3, 301, 0x0020, bytes([1, 0])) +
               record(4, 811, 0x0021, bytes([1, 2, 0, 0]) + struct.pack("<I", 6000)))
        with tempfile.TemporaryDirectory() as root:
            # A tune mute (200..300) runs straight into the gap (301..811), then an
            # unexplained SW run at 1500.
            radio = [5] * 200 + [0] * 612 + [5] * 688 + [0] * 60 + [5] * 500
            metrics, verdict = wav_retune.run_local(self.write(root, radio, tune, gap))
        self.assertEqual(metrics["max_overhang_frames"], {"FM": 0})
        self.assertEqual(metrics["unexplained_by_band"], {"SW": 1})
        self.assertEqual(verdict[1], "zero_run_outside_retune")

    def test_needs_events_or_log_and_origin(self):
        with self.assertRaises(BenchError):
            wav_retune.run_local({"wav": __file__})


if __name__ == "__main__":
    unittest.main()
