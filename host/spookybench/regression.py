"""Unattended baseline recording regression composed from the existing runners.

One board lock and one run directory cover the paired IpcSmoke flash and boot
health, audio and SD prerequisites, a bounded recording under IPC load, CRC
retrieval and inspection of the file the recorder reported, and target health
after the transfer. Automated gates, observations (stereo, clipping, tuning) and
human checks (listening) are reported separately. Recordings are never deleted.
"""
import math
import re
import time
from pathlib import Path
from . import boot_smoke, fake, ipc_load, sd_basic, serial_io, wav_align, wav_inspect
from .artifacts import JsonLines
from .result import BenchError

MIB = 1024 * 1024
BYTES_PER_SECOND = 48000 * 6
SD_MARGIN_MIB = 16
# WAV block rounding, transfer records, UART, diagnostics and stage files.
ARTIFACT_MARGIN_BYTES = 16 * MIB
STIMULI = ("ambient", "loopback")
# wav_align holds the whole recording in memory as floating-point arrays.
LOOPBACK_MAX_SECONDS = 120
RADIO = re.compile(r"^OK RADIO BAND=([A-Z]+) FREQ=([0-9]+) kHz \([0-9]+\.[0-9]{3} MHz\) "
                   r"RSSI=([0-9]+) SNR=([0-9]+) VALID=([01])$")
VOLUME = re.compile(r"^OK VOLUME ADC=([0-9]+) LEVEL=([0-9]+)% "
                    r"(?:ATTEN=-([0-9]+)\.([05]) dB )?MUTED=([01])$")
MONO_LIKE_CORRELATION = 0.99


def budget(seconds, stimulus):
    """Whole-operation deadline: boot, recording, transfer plus analysis, health.

    Stop-and-wait WAV transfer measured about 65 KB/s (4.4 s per recorded second);
    allow 6 s per recorded second for it."""
    return 450.0 + 7.0 * seconds + (60.0 if stimulus == "loopback" else 0.0)


def _record(raw, complete):
    record = sd_basic._record(raw, complete)
    if record["kind"] != "text" or not complete:
        return record
    text = record["text"].strip()
    match = RADIO.fullmatch(text)
    if match:
        record.update(kind="OK RADIO", fields={"band": match[1],
            "frequency_khz": int(match[2]), "rssi_dbuv": int(match[3]),
            "snr_db": int(match[4]), "valid": match[5] == "1"})
        return record
    match = VOLUME.fullmatch(text)
    if match:
        attenuation = None if match[3] is None else int(match[3]) + int(match[4]) / 10
        record.update(kind="OK VOLUME", fields={"adc": int(match[1]),
            "level_percent": int(match[2]), "attenuation_db": attenuation,
            "muted": match[5] == "1"})
    return record


def _target_error(command, record):
    if record["kind"] == "OK RECORD ACTIVE":
        raise BenchError("recording_busy", "A recording is already active")
    text = record["text"].strip()
    if "audio path is not running" in text:
        raise BenchError("audio_path_stopped", text, "fail")
    if command.startswith("VOLUME"):
        raise BenchError("codec_unavailable", text, "fail")
    if command.startswith("SD"):
        sd_basic._raise_target_error(record)
    raise BenchError("protocol_error", text, "fail")


def _requests(selected, options, clock, shared, emit, commands):
    """Issue each (command, expected kind) in order inside one quiet-settled session."""
    from spookyprobe.protocol import Lines
    serial = (fake.Serial(clock, "device", options["scenario"], shared)
              if options["simulate"] else serial_io.open_serial(selected["device"]["port"]))
    pending = Lines()
    answers = {}
    emit({"kind": "session_start", "command": " + ".join(command for command, _ in commands),
          "host_receive_ns": time.time_ns()})
    try:
        serial_io.settle(serial, emit, clock,
                         boot_smoke._remaining_deadline(clock, options["deadline"], 14.0))
        for command, kind in commands:
            wire = (command + "\n").encode("ascii")
            if serial.write(wire) != len(wire):
                raise OSError("short command write")
            end = boot_smoke._remaining_deadline(clock, options["deadline"], 8.0)
            answer = None
            while answer is None:
                if clock.now() >= end:
                    raise BenchError("request_timeout", f"{command} response deadline exceeded")
                for raw, complete in pending.feed(serial.read(4096)):
                    record = _record(raw, complete)
                    emit(record)
                    if answer is None and record["kind"] in (kind, "ERR", "OK RECORD ACTIVE"):
                        answer = record
            if answer["kind"] != kind:
                _target_error(command, answer)
            answers[command] = answer["fields"]
        return answers
    finally:
        try:
            for raw, complete in pending.finish():
                emit(_record(raw, complete))
        finally:
            serial.close()
            emit({"kind": "session_end", "host_receive_ns": time.time_ns()})


def _prerequisites(selected, options, clock, shared, emit, detail):
    answers = _requests(selected, options, clock, shared, emit, (
        ("RECORD STATUS", "OK RECORD IDLE"), ("SD STATUS", "OK SD STATUS"),
        ("STATUS", "OK RADIO"), ("VOLUME STATUS", "OK VOLUME")))
    needed = math.ceil(options["seconds"] * BYTES_PER_SECOND / MIB) + SD_MARGIN_MIB
    detail.update(record_status=answers["RECORD STATUS"], sd=answers["SD STATUS"],
                  radio=answers["STATUS"], volume=answers["VOLUME STATUS"],
                  sd_required_mib=needed)
    if answers["SD STATUS"]["free_mib"] < needed:
        raise BenchError("sd_capacity", f"SD free space is below the {needed} MiB this "
                         "recording needs; free space manually (recordings are never deleted)")


def _alignment(path, detail):
    metrics = wav_align.analyze(path)
    summary = metrics["alignment"]
    detail.update(metrics)
    result, reason, message = wav_align.verdict(summary)
    if result != "pass":
        raise BenchError(reason, message, result)
    if not summary["drift_reliable"]:
        raise BenchError("alignment_discontinuous", "Loopback lag is not one continuous "
                         f"phase ({summary['drift_reason']}); radio/mic continuity unproven",
                         "fail")


def _deltas(before, after, keys):
    return {key: after[key] - before[key] for key in keys
            if isinstance(before.get(key), int) and isinstance(after.get(key), int)}


def _post_health(selected, options, profile, clock, shared, emit, context, detail):
    query = lambda command: boot_smoke._query(profile, selected, options, clock, shared,
                                              emit, command)
    final_diag = query("DIAG STATUS")
    detail["diag"] = final_diag["fields"]
    boot_smoke._check_diag(final_diag)
    final_log = query("LOG STATUS")
    detail["log"] = final_log["fields"]
    ipc_load._check_log(final_log)
    final_ipc = query("IPC STATUS")
    detail["ipc"] = final_ipc
    boot_smoke._check_ipc(final_ipc)
    detail["ipc_delta"] = boot_smoke._progress(context["last_ipc"], final_ipc)
    if not detail["ipc_delta"]:
        raise BenchError("ipc_no_progress", "IPC did not progress after the WAV transfer", "fail")
    answers = _requests(selected, options, clock, shared, emit,
                        (("RECORD STATUS", "OK RECORD IDLE"), ("SD STATUS", "OK SD STATUS")))
    detail.update(record_status=answers["RECORD STATUS"], sd=answers["SD STATUS"])

    # Error counters are already gated at zero before and after; deltas are evidence.
    diag_keys = (*boot_smoke.DIAG_ERRORS, "COUNT", "OVERWRITTEN")
    log_keys = (*boot_smoke.LOG_ERRORS, "TX_BYTES")
    detail["diag_deltas"] = _deltas(context["baseline_diag"], detail["diag"], diag_keys)
    detail["log_deltas"] = _deltas(context["baseline_log"], detail["log"], log_keys)

    before, after = context["sd"], detail["sd"]
    if any(before[key] != after[key] for key in ("type", "capacity_mib", "blocks")):
        raise BenchError("sd_card_changed", "SD identity changed during the regression", "fail")
    # The recorder never overwrites (FA_CREATE_NEW) and this runner never deletes,
    # so free space must drop by the one new file, within MiB reporting rounding.
    consumed = before["free_mib"] - after["free_mib"]
    size_mib = context["file_bytes"] / MIB
    detail["sd_consumed_mib"] = consumed
    if not math.floor(size_mib) - 1 <= consumed <= math.ceil(size_mib) + 1:
        raise BenchError("sd_accounting", f"SD free space changed by {consumed} MiB for a "
                         f"{size_mib:.1f} MiB recording", "fail")


def _cross_check(recording, transfer, wav, previous_file):
    result = recording["result"]
    if transfer["file"] != result["file"]:
        raise BenchError("regression_identity", "Retrieved file is not the recorded file", "fail")
    if previous_file == result["file"]:
        raise BenchError("regression_identity", "Recorder reported the pre-existing file", "fail")
    if wav["frames"] != result["frames"] or wav["data_bytes"] != result["bytes"]:
        raise BenchError("wav_accounting", "WAV frames or bytes differ from the recorder's "
                         "PASS accounting", "fail")


def _summary(details, stimulus):
    """Bounded result: full stage metrics live in stage-<name>.json."""
    summary = {}
    boot = details.get("boot_smoke", {})
    if "firmware" in boot:
        summary["firmware"] = boot["firmware"]
    if "ipc" in boot:
        summary.setdefault("ipc", {})["boot_delta"] = boot["ipc"].get("deltas")
    if "prerequisites" in details:
        summary["prerequisites"] = details["prerequisites"]
    recording = details.get("recording", {}).get("recording")
    if recording:
        summary["recording"] = recording
    ipc = details.get("recording", {}).get("ipc")
    if ipc:
        summary.setdefault("ipc", {})["recording"] = [
            {"phase": item["phase"], "delta": item.get("delta")} for item in ipc["snapshots"]]
    wav_stage = details.get("wav_inspect", {})
    if "transfer" in wav_stage:
        summary["transfer"] = wav_stage["transfer"]
    wav = wav_stage.get("wav")
    if wav:
        summary["wav"] = {key: wav[key] for key in ("frames", "data_bytes",
                                                     "duration_seconds", "signal")}
        lr = wav["correlations"]["radio_left_radio_right"]
        summary["observations"] = {
            "radio_left_right_correlation": lr,
            "radio_channels": None if lr is None else
                "mono_like" if lr >= MONO_LIKE_CORRELATION else "distinct",
            "radio_microphone_correlations": {
                key: value for key, value in wav["correlations"].items()
                if key.endswith("microphone")},
            "clipped_samples": {name: channel["clipped_samples"]
                                for name, channel in wav["signal"].items()}}
    if "alignment" in details.get("alignment", {}):
        summary["alignment"] = details["alignment"]["alignment"]
    health = details.get("post_transfer_health")
    if health:
        summary["health"] = {key: health[key] for key in (
            "diag_deltas", "log_deltas", "sd_consumed_mib", "diag", "log")
            if key in health}
        if "ipc_delta" in health:
            summary.setdefault("ipc", {})["after_transfer"] = health["ipc_delta"]
    summary["human_checks"] = {"listening": "pending", "detail":
        "Listen to the retrieved WAV; automated channel statistics are not an audio-"
        "quality verdict" + ("" if stimulus == "loopback" else
        "; stereo separation is reported as an observation only")}
    return summary


def run(options, profile, run, metrics, artifacts):
    stimulus = options["stimulus"]
    metrics["test"] = {"name": "recording-regression", "stimulus": stimulus,
        "requested_seconds": options["seconds"],
        "criterion": "paired IpcSmoke boot, prerequisites, recording under IPC load, "
                     "CRC-verified WAV matching the recorder, post-transfer health"}
    metrics["checks"] = {}
    metrics["stages"] = []
    # Refuse before flashing if the run budget cannot hold the retrieved WAV.
    needed = options["seconds"] * BYTES_PER_SECOND + ARTIFACT_MARGIN_BYTES
    if profile["limits"]["run_bytes"] < needed:
        raise BenchError("artifact_limit", f"Profile run_bytes must be at least {needed} "
                         "for this recording and its evidence")
    options = dict(options, sim_state={})  # One simulated target across every stage.
    shared = options["sim_state"]
    clock = fake.Clock() if options["simulate"] else serial_io.Clock()
    details = {}
    lines = None

    def stage(name, action, base=None):
        started = time.monotonic()
        entry = {"name": name, "result": "running"}
        metrics["stages"].append(entry)
        detail = details.setdefault(name, dict(base or {}))
        stage_artifacts = {}
        try:
            action(detail, stage_artifacts)
            entry["result"] = "pass"
            metrics["checks"][name] = "pass"
        except BenchError as exc:
            entry.update(result=exc.result, reason=exc.reason)
            metrics["checks"][name] = exc.result
            raise
        except BaseException as exc:
            entry.update(result="error", reason=type(exc).__name__)
            metrics["checks"][name] = "error"
            raise
        finally:
            entry["duration_ms"] = max(0, round((time.monotonic() - started) * 1000))
            for key, value in stage_artifacts.items():
                if key != "run_dir":
                    artifacts[f"{name}_{key}"] = value
            try:
                run.write_json(f"stage-{name}.json", detail)
            except BaseException:
                if entry["result"] == "pass":
                    raise  # Otherwise keep the stage's own failure as the verdict.
        return detail

    failure = None
    try:
        boot = stage("boot_smoke", lambda detail, found: boot_smoke.run(
            options, profile, run, detail, found, evidence="boot-diagnostics.jsonl"),
            base={"selected": metrics["selected"], "tools": metrics["tools"]})
        selected = boot["selected_after_reset"]
        metrics["selected_after_reset"] = selected
        artifacts["diagnostics"] = str(run.path / "regression-diagnostics.jsonl")
        lines = JsonLines(run, "regression-diagnostics.jsonl", profile["limits"]["diag_bytes"])

        prerequisites = stage("prerequisites", lambda detail, found: _prerequisites(
            selected, options, clock, shared, lines.emit, detail))

        recording = stage("recording", lambda detail, found: ipc_load.run(
            options, profile, run, detail, found, evidence="recording-diagnostics.jsonl"),
            base={"selected": selected})
        name = recording["recording"]["result"]["file"]

        inspected = stage("wav_inspect", lambda detail, found: wav_inspect.run(
            dict(options, filename=name), profile, run, detail, found),
            base={"selected": selected})

        def cross_check(detail, found):
            _cross_check(recording["recording"], inspected["transfer"], inspected["wav"],
                         prerequisites["record_status"]["last_file"])
            detail.update(file=name, frames=inspected["wav"]["frames"])
        stage("accounting", cross_check)

        if stimulus == "loopback":
            stage("alignment", lambda detail, found: _alignment(
                Path(artifacts["wav_inspect_wav"]), detail))

        context = {"baseline_diag": boot["diagnostics"]["status"]["fields"],
                   "baseline_log": boot["diagnostics"]["log_status"]["fields"],
                   "last_ipc": recording["ipc"]["snapshots"][-1]["response"],
                   "sd": prerequisites["sd"], "file_bytes": inspected["transfer"]["bytes"]}
        stage("post_transfer_health", lambda detail, found: _post_health(
            selected, options, profile, clock, shared, lines.emit, context, detail))
    except BaseException as exc:
        failure = exc
    finally:
        if lines:
            try:
                lines.close()
            except BaseException as exc:
                if failure is None:
                    failure = exc
    metrics.update(_summary(details, stimulus))
    if failure:
        failed = next((details.get(entry["name"], {}) for entry in metrics["stages"]
                       if entry["result"] != "pass"), {})
        booted = metrics["checks"].get("boot_smoke") == "pass"
        metrics["target_health"] = "failed"
        metrics["human_required"] = bool(failed.get("human_required", False))
        metrics["final_target_state"] = failed.get("final_target_state",
            "running" if booted and not metrics["human_required"] else "unknown")
        started = recording_file(details)
        if started:
            metrics["retained_recording"] = started
        raise failure
    metrics.update(target_health="healthy", final_target_state="running",
                   human_required=False)


def recording_file(details):
    """The file a started recording left on the card; it is never deleted."""
    recording = details.get("recording", {}).get("recording", {})
    return (recording.get("result") or recording.get("start") or {}).get("file")
