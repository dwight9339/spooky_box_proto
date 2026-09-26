"""Measure radio-to-microphone alignment in a recorder WAV; offline, no hardware.

With an acoustic loopback stimulus (an earbud on the monitor output held against
the microphone), the microphone track is a delayed copy of the radio program.
Each analysis window reports the lag, in frames, that best aligns the two:
positive means the microphone sample at frame n matches radio frame n - lag.
Lag spread between windows exposes drift or discontinuities within a file;
lag differences between files expose start-offset variation.
"""
from array import array
import math
from operator import mul
import sys
from .result import BenchError
from .wav_inspect import read_format

RATE_HZ = 48000
DECIMATION = 16
MIN_DRIFT_PHASE_FRACTION = 0.75


def _load(path):
    """Return (radio mono, microphone) as float arrays."""
    _fmt, data_offset, data_size = read_format(path)
    samples = array("h")
    with path.open("rb") as stream:
        stream.seek(data_offset)
        samples.frombytes(stream.read(data_size))
    if sys.byteorder != "little":
        samples.byteswap()
    left, right, mic = samples[0::3], samples[1::3], samples[2::3]
    radio = array("d", map(lambda a, b: (a + b) * 0.5, left, right))
    return radio, array("d", mic)


def _difference(values):
    """First difference: a simple pre-whitening that sharpens correlation peaks."""
    out = array("d", values)
    out[1:] = array("d", map(lambda a, b: a - b, values[1:], values[:-1]))
    out[0] = 0.0
    return out


def _decimate(values, factor):
    count = len(values) // factor
    return array("d", (sum(values[i * factor:(i + 1) * factor]) / factor
                       for i in range(count)))


def _correlate(mic, radio, start, length, lags):
    """Normalized correlation of mic[start:start+length] against shifted radio."""
    target = mic[start:start + length]
    target_energy = sum(map(mul, target, target))
    scores = {}
    for lag in lags:
        begin = start - lag
        source = radio[begin:begin + length]
        energy = sum(map(mul, source, source))
        denominator = math.sqrt(target_energy * energy)
        scores[lag] = sum(map(mul, target, source)) / denominator if denominator else 0.0
    return scores


def _add_drift_summary(summary, detected):
    """Add endpoint drift only when the detected peak follows one continuous phase."""
    first, last = detected[0], detected[-1]
    elapsed = (last["start_seconds"] - first["start_seconds"]) * RATE_HZ
    endpoint_delta = last["lag_frames"] - first["lag_frames"]
    positive = [item for item in detected if item["polarity"] > 0]
    negative = [item for item in detected if item["polarity"] < 0]
    drift_polarity = 1 if len(positive) >= len(negative) else -1
    drift_windows = positive if drift_polarity > 0 else negative
    phase_fraction = len(drift_windows) / len(detected)
    lags = [item["lag_frames"] for item in drift_windows]
    deltas = [right - left for left, right in zip(lags, lags[1:])]
    polarity_consistent = len({item["polarity"] for item in detected}) == 1
    lag_monotonic = all(delta >= 0 for delta in deltas) or all(delta <= 0 for delta in deltas)
    lag_stable = max(lags) - min(lags) <= 1
    phase_dominant = phase_fraction >= MIN_DRIFT_PHASE_FRACTION
    reliable = phase_dominant and (lag_monotonic or lag_stable)

    drift_first, drift_last = drift_windows[0], drift_windows[-1]
    drift_elapsed = (drift_last["start_seconds"] - drift_first["start_seconds"]) * RATE_HZ
    drift_delta = drift_last["lag_frames"] - drift_first["lag_frames"]

    summary.update(
        first_lag_frames=first["lag_frames"], last_lag_frames=last["lag_frames"],
        endpoint_delta_frames=endpoint_delta,
        endpoint_delta_ppm=round(endpoint_delta / elapsed * 1e6, 3) if elapsed else None,
        polarity_consistent=polarity_consistent, lag_monotonic=lag_monotonic,
        lag_stable=lag_stable, drift_polarity=drift_polarity,
        drift_windows=len(drift_windows), drift_excluded_windows=len(detected) - len(drift_windows),
        drift_phase_fraction=round(phase_fraction, 4), drift_reliable=reliable)
    if reliable:
        summary.update(drift_frames=drift_delta,
                       drift_ppm=round(drift_delta / drift_elapsed * 1e6, 3)
                       if drift_elapsed else None,
                       drift_reason=None)
    else:
        reason = "phase_ambiguous" if not phase_dominant else "lag_not_continuous"
        summary.update(drift_frames=None, drift_ppm=None, drift_reason=reason)


def analyze(path, window_seconds=2.0, hop_seconds=5.0, max_lag_ms=50.0,
            min_correlation=0.2, min_confidence=1.3):
    radio, mic = _load(path)
    frames = len(mic)
    window = int(window_seconds * RATE_HZ)
    hop = int(hop_seconds * RATE_HZ)
    max_lag = int(max_lag_ms * RATE_HZ / 1000)
    coarse_lag = max_lag // DECIMATION + 1
    if window < 4 * DECIMATION or hop <= 0 or max_lag <= 0:
        raise BenchError("invalid_invocation", "Alignment window, hop and lag must be positive")
    radio_fine, mic_fine = _difference(radio), _difference(mic)
    radio_coarse = _difference(_decimate(radio, DECIMATION))
    mic_coarse = _difference(_decimate(mic, DECIMATION))

    windows = []
    start = max_lag + DECIMATION
    while start + window + max_lag + DECIMATION <= frames:
        coarse = _correlate(mic_coarse, radio_coarse, start // DECIMATION,
                            window // DECIMATION, range(-coarse_lag, coarse_lag + 1))
        best_coarse = max(coarse, key=lambda lag: abs(coarse[lag]))
        rivals = [abs(value) for lag, value in coarse.items() if abs(lag - best_coarse) > 2]
        runner_up = max(rivals, default=0.0)
        centre = best_coarse * DECIMATION
        fine = _correlate(mic_fine, radio_fine, start, window,
                          range(max(-max_lag, centre - DECIMATION - 8),
                                min(max_lag, centre + DECIMATION + 8) + 1))
        lag = max(fine, key=lambda value: abs(fine[value]))
        correlation = fine[lag]
        confidence = abs(coarse[best_coarse]) / runner_up if runner_up else math.inf
        windows.append({"start_seconds": round(start / RATE_HZ, 3), "lag_frames": lag,
            "lag_ms": round(lag * 1000 / RATE_HZ, 4), "correlation": round(correlation, 4),
            "confidence": round(confidence, 3) if math.isfinite(confidence) else None,
            "polarity": 1 if correlation >= 0 else -1,
            "detected": abs(correlation) >= min_correlation and confidence >= min_confidence})
        start += hop

    detected = [item for item in windows if item["detected"]]
    summary = {"frames": frames, "duration_seconds": frames / RATE_HZ,
        "windows": len(windows), "detected_windows": len(detected),
        "window_seconds": window_seconds, "hop_seconds": hop_seconds,
        "max_lag_ms": max_lag_ms, "min_correlation": min_correlation,
        "min_confidence": min_confidence}
    if detected:
        lags = sorted(item["lag_frames"] for item in detected)
        summary.update(lag_median_frames=lags[len(lags) // 2], lag_min_frames=lags[0],
            lag_max_frames=lags[-1], lag_spread_frames=lags[-1] - lags[0],
            polarity=1 if sum(item["polarity"] for item in detected) >= 0 else -1)
        _add_drift_summary(summary, detected)
    return {"alignment": summary, "windows": windows}


def verdict(summary):
    """Return (result, reason, detail): pass needs at least half the windows detected."""
    needed = max(2, math.ceil(summary["windows"] / 2))
    if summary["detected_windows"] < needed:
        return ("fail", "alignment_not_detected",
            f"{summary['detected_windows']} of {summary['windows']} windows correlated; "
            "check the acoustic loopback stimulus")
    return ("pass", None, None)


def run_local(options):
    """Analyze one local WAV; raise BenchError("fail") when alignment is not measurable."""
    from pathlib import Path
    path = Path(options["wav"])
    if not path.is_file():
        raise BenchError("invalid_invocation", f"WAV file not found: {path}")
    metrics = analyze(path, options["window_seconds"], options["hop_seconds"],
                      options["max_lag_ms"])
    return metrics, verdict(metrics["alignment"])
