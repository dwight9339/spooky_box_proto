import argparse
import contextlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import pwm_mic_sweep as sweep  # noqa: E402

SPIKE = (300, 600, 200)


def floor(frames, seed=1):
    """Deterministic +-10 noise, well under the click threshold."""
    values, state = [], seed
    for _ in range(frames):
        state = (state * 1103515245 + 12345) & 0x7fffffff
        values.append(state % 21 - 10)
    return values


def add_spike(mic, at):
    for offset, value in enumerate(SPIKE):
        if at + offset < len(mic):
            mic[at + offset] += value


def burst_train(seconds, phase=1000, spikes=6, spacing=60):
    mic = floor(int(seconds * sweep.SAMPLE_RATE))
    beat = 0
    while phase + beat * sweep.BEAT_FRAMES < len(mic) - spikes * spacing:
        start = int(phase + beat * sweep.BEAT_FRAMES)
        for k in range(spikes):
            add_spike(mic, start + k * spacing)
        beat += 1
    return mic


def scattered(seconds, count):
    mic = floor(int(seconds * sweep.SAMPLE_RATE))
    state = 7
    for _ in range(count):
        state = (state * 1103515245 + 12345) & 0x7fffffff
        add_spike(mic, state % (len(mic) - 4))
    return mic


def write_wav(path, mic):
    data = b"".join(struct.pack("<hhh", 11, -11, value) for value in mic)
    header = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " + \
        struct.pack("<IHHIIHH", 16, 1, 3, 48000, 288000, 6, 16) + \
        b"data" + struct.pack("<I", len(data))
    path.write_bytes(header + data)


class BeatTest(unittest.TestCase):
    def test_beat_follows_the_pll3_fraction(self):
        # 2.03 ppm of 3.072 MHz slips one PDM cycle every 160.6 ms.
        self.assertAlmostEqual(sweep.PLL3_RATIO_ERROR * 1e6, 2.0266, places=3)
        self.assertAlmostEqual(sweep.BEAT_FRAMES / sweep.SAMPLE_RATE * 1000, 160.63, places=1)


class AnalyzeTest(unittest.TestCase):
    def test_burst_train_folds_onto_one_window(self):
        result = sweep.analyze(burst_train(4.0))
        self.assertEqual(result["verdict"], "bursts")
        self.assertGreaterEqual(result["train_fraction"], 0.9)
        self.assertEqual(result["beats_hit"], result["beats"])
        # Spikes at 1000 + 60k frames, k = 0..5: mean 1149 frames, 23.9 ms.
        self.assertAlmostEqual(result["train_phase_ms"], 1149 / 48, delta=0.5)

    def test_train_across_the_beat_wrap_keeps_its_phase(self):
        result = sweep.analyze(burst_train(4.0, phase=7600))
        self.assertEqual(result["verdict"], "bursts")
        late = (7600 + 150) % sweep.BEAT_FRAMES / 48
        self.assertAlmostEqual(result["train_phase_ms"], late, delta=0.5)

    def test_scattered_clicks_are_not_a_train(self):
        result = sweep.analyze(scattered(4.0, 150))
        self.assertEqual(result["verdict"], "clicks")
        self.assertLess(result["train_fraction"], 0.4)

    def test_quiet_floor_is_clean(self):
        result = sweep.analyze(floor(4 * sweep.SAMPLE_RATE))
        self.assertEqual(result["verdict"], "clean")
        self.assertEqual(result["clicks"], 0)

    def test_one_spike_is_one_click(self):
        mic = floor(10000)
        add_spike(mic, 5000)
        self.assertEqual(sweep.clicks(mic), [5000 - 1])


class ReadTest(unittest.TestCase):
    def test_reads_the_mic_channel_of_a_span(self):
        mic = list(range(-100, 100)) * 480  # 96,000 frames, 2 s
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "a.wav"
            write_wav(path, mic)
            self.assertEqual(list(sweep.read_mic(path)), mic)
            self.assertEqual(list(sweep.read_mic(path, 0.5, 1.0)), mic[24000:48000])


class GroupsAnalyzeTest(unittest.TestCase):
    def test_steps_are_placed_back_from_the_save(self):
        # 12 s capture in two segments; the save came 0.5 s after the last step.
        # Steps a (0-4 s), b (4-8 s, bursts) and c (8-11.5 s) in host time.
        rate = sweep.SAMPLE_RATE
        quiet = floor(12 * rate)
        bursts = burst_train(12.0)
        mic = quiet[:4 * rate] + bursts[4 * rate:8 * rate] + quiet[8 * rate:]
        with tempfile.TemporaryDirectory() as directory:
            caps = Path(directory)
            write_wav(caps / "C009S00.WAV", mic[:7 * rate])
            write_wav(caps / "C009S01.WAV", mic[7 * rate:])
            (caps / "C009.TXT").write_text(
                "SPOOKY DEMO CAPTURE 1\nsegments=2 frames=576000\n"
                f"C009S00.WAV frames={7 * rate} sequence=1\n"
                f"C009S01.WAV frames={5 * rate} sequence=2\n")
            t0 = 1000.0
            plan = {"capture": "C009", "save_requested": t0 + 12.0, "steps": [
                {"name": "a", "start": t0 + 0.0, "end": t0 + 4.0},
                {"name": "b", "start": t0 + 4.0, "end": t0 + 8.0},
                {"name": "c", "start": t0 + 8.0, "end": t0 + 11.5}]}
            (caps / "groups.json").write_text(json.dumps(plan))
            self.assertEqual(len(sweep.read_capture(caps, "C009")), 12 * rate)
            with contextlib.redirect_stdout(io.StringIO()):
                sweep.groups_analyze(argparse.Namespace(
                    plan=str(caps / "groups.json"), caps=str(caps), capture=None))
            result = json.loads((caps / "groups-analysis.json").read_text())
        verdicts = {row["name"]: row["analysis"]["verdict"] for row in result["steps"]}
        self.assertEqual(verdicts, {"a": "clean", "b": "bursts", "c": "clean"})
        self.assertEqual(result["steps"][1]["from_s"], 5.0)
        self.assertEqual(result["steps"][1]["to_s"], 7.0)


class PlanTest(unittest.TestCase):
    def test_raw_command_is_hex_mask_duty_rest(self):
        self.assertEqual(sweep.raw_command(0x2001, 500, 1000), "DEMO LIGHTS RAW 2001 500 1000")

    def test_plan_has_controls_then_every_channel(self):
        steps = sweep.plan(500, 1000)
        self.assertEqual([s[0] for s in steps[:3]], ["steady-on", "steady-off", "all-500"])
        self.assertEqual(steps[2][1], 0x3FFF)
        singles = steps[3:]
        self.assertEqual(len(singles), 14)
        self.assertEqual(singles[0], ("BTN0-500", 1, 500, 1000))
        self.assertEqual(singles[-1], ("ENC3_B-500", 1 << 13, 500, 1000))

    def test_follow_up_retries_noisy_single_channels_only(self):
        def result(name, mask, verdict):
            return {"name": name, "mask": mask, "duty": 500,
                    "analysis": {"verdict": verdict}}
        results = [result("all-500", 0x3FFF, "bursts"), result("BTN0-500", 1, "bursts"),
                   result("BTN1-500", 2, "clean"), result("ENC0_R-500", 4, "clicks")]
        self.assertEqual(sweep.follow_up(results, [100, 500, 900], 1000),
                         [("BTN0-100", 1, 100, 1000), ("BTN0-900", 1, 900, 1000),
                          ("ENC0_R-100", 4, 100, 1000), ("ENC0_R-900", 4, 900, 1000)])


if __name__ == "__main__":
    unittest.main()
