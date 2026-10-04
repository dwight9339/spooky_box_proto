"""Deterministic simulated devices. No hardware calls or real-time sleeps."""
from array import array
import math
import random
import struct
import sys
import zlib

MIB = 1024 * 1024
LOOPBACK_LAG_FRAMES = 300


def _container(payload, scenario):
    riff = b"RIFF" + struct.pack("<I", 36 + len(payload)) + b"WAVE"
    fmt = b"fmt " + struct.pack("<IHHIIHH", 16, 1, 3, 48000, 288000, 6, 16)
    wav = riff + fmt + b"data" + struct.pack("<I", len(payload)) + payload
    return (b"NOPE" + wav[4:]) if scenario == "wav-bad-header" else wav


def _wav_bytes(scenario):
    frames = 128
    payload = bytearray()
    for index in range(frames):
        values = (0, 0, 0) if scenario == "wav-silent" else (
            ((index % 32) - 16) * 100,
            ((index % 29) - 14) * 90,
            ((index % 13) - 6) * 50)
        payload.extend(struct.pack("<hhh", *values))
    return _container(payload, scenario)


def _recorded_wav(frames, scenario):
    """A recording matching the recorder's frame count: broadband mono radio and a
    microphone that hears it through an acoustic loopback, unless the scenario
    removes the loopback."""
    rng = random.Random(frames)
    radio = array("h", (rng.randint(-8000, 8000) for _ in range(frames)))
    if scenario == "loopback-missing":
        mic = array("h", (rng.randint(-300, 300) for _ in range(frames)))
    else:
        mic = array("h", bytes(2 * LOOPBACK_LAG_FRAMES)) + \
            array("h", (value // 2 for value in radio[:frames - LOOPBACK_LAG_FRAMES]))
    samples = array("h", bytes(6 * frames))
    if scenario != "wav-silent":
        samples[0::3], samples[1::3], samples[2::3] = radio, radio, mic
    if sys.byteorder != "little":
        samples.byteswap()
    return _container(samples.tobytes(), scenario)


class Clock:
    def __init__(self):
        self.value = 0.0

    def now(self):
        return self.value

    def sleep(self, seconds):
        self.value += seconds


def inventory(scenario):
    items = [dict(port="COM101", serial_number="SIM-PROBE", vid=0x2e8a, pid=0x000c,
                  interface="CDC", description="Simulated Pico"),
             dict(port="COM102", serial_number="SIM-TARGET", vid=0x0483, pid=0x5740,
                  interface="CDC", description="Simulated target")]
    if scenario == "missing":
        return []
    if scenario == "cdc-missing":
        return items[:1]
    if scenario == "com-renumber":
        items[1]["port"] = "COM104"
    if scenario == "ambiguous":
        items.append(dict(items[0], port="COM103"))
    return items


class Serial:
    def __init__(self, clock, role, scenario="happy", state=None):
        self.clock, self.role, self.scenario = clock, role, scenario
        self.state = state if state is not None else {}
        self.pending = b""
        self.reads = 0
        self.writes = []
        self.closed = False

    def read(self, size):
        # Bulk WAV reads model a CDC link of a few MB/s rather than line polling.
        if not self.pending and not self.state.get("wav_active"):
            self.state["wav_bulk"] = False
        self.clock.sleep(0.001 if self.state.get("wav_bulk") else 0.1)
        self.reads += 1
        if self.scenario == "disconnect" and (self.role == "probe" or self.writes):
            raise OSError("simulated cable disconnect")
        if self.role == "probe":
            if self.scenario == "uart-empty":
                return b""
            return (b"simulated UART\r\n\xffpartial" * 200)[:size] if self.reads == 1 else b""
        if self.scenario == "record-disconnect" and self.state.get("recording_active"):
            raise OSError("simulated target disconnect during recording")
        if self.scenario == "sd-disconnect" and self.state.get("sd_active"):
            raise OSError("simulated target disconnect during SD test")
        if not self.pending and self.state.get("wav_active"):
            offset = self.state["wav_offset"]
            data = self.state["wav_data"]
            if self.scenario == "wav-truncated" and offset:
                return b""
            if not self.state.get("wav_waiting_ack"):
                if offset < len(data):
                    payload = data[offset:offset + self.state["wav_chunk"]]
                    crc = zlib.crc32(payload) & 0xffffffff
                    if self.scenario == "wav-corrupt-frame" and offset == 0:
                        crc ^= 1
                    self.pending = struct.pack("<4sIHHI", b"WV01", offset,
                                               len(payload), 1, crc) + payload
                    self.state["wav_next"] = offset + len(payload)
                    self.state["wav_waiting_ack"] = True
                else:
                    crc = zlib.crc32(data) & 0xffffffff
                    self.pending = struct.pack("<4sIHHI", b"WV01", offset, 0, 2, crc)
                    self.state["wav_active"] = False
        if not self.pending and self.state.get("recording_active"):
            elapsed = self.clock.now() - self.state["recording_started"]
            if self.state.get("stop_requested"):
                self._finish_recording(False)
            elif self.scenario == "record-abort" and elapsed >= 5.0:
                self._finish_recording(True)
            elif elapsed >= self.state["recording_seconds"]:
                self._finish_recording(False)
            elif elapsed >= self.state["next_record_progress"]:
                whole = int(elapsed)
                self.pending = (f"RECORD progress={whole}.0s queues=0/8,0/8 "
                                "max-write=25ms\r\n").encode()
                self.state["next_record_progress"] += 5.0
        if not self.pending and self.state.get("sd_active"):
            if self.scenario == "sd-timeout":
                return b""
            current = self.state["sd_pass"]
            passes = self.state["sd_passes"]
            if self.state["sd_stage"] == "write":
                self.pending = f"SD STRESS pass={current}/{passes} phase=VERIFY\r\n".encode()
                self.state["sd_stage"] = "verify"
            elif self.scenario == "sd-corrupt":
                self.pending = (b"ERR SD STRESS failed result=FR_INT_ERR(2) offset=4096 "
                                b"hal=0x00000000; SDTEST.BIN retained\r\n")
                self.state["sd_active"] = False
            elif current < passes:
                self.state["sd_pass"] = current + 1
                self.state["sd_stage"] = "write"
                self.pending = (f"SD STRESS pass={current + 1}/{passes} "
                                "phase=WRITE\r\n").encode()
            else:
                size = self.state["sd_size_mib"]
                transferred = size * passes * 1024 * 1024
                if self.scenario == "sd-cleanup":
                    self.pending = (b"ERR SD test passed but cleanup failed "
                                    b"result=FR_DISK_ERR(1); use SD CLEAN\r\n")
                else:
                    self.pending = (f"OK SD STRESS PASS size={size}MiB passes={passes} "
                        f"written={transferred} verified={transferred} elapsed=42ms "
                        "write-max=7ms read-max=3ms aggregate=380.9MiB/s "
                        "file-removed=1\r\n").encode()
                self.state["sd_active"] = False
        # Short reads exercise line reassembly; recorded WAVs are too large for that.
        take = size if self.state.get("wav_bulk") else min(size, 17)
        out, self.pending = self.pending[:take], self.pending[take:]
        return out

    def write(self, data):
        self.writes.append(data)
        command = data.decode().strip()
        if command == "SD STATUS":
            if self.scenario == "sd-no-card":
                self.pending = b"ERR SD no card detected\r\n"
            else:
                count = self.state.get("sd_status_count", 0) + 1
                self.state["sd_status_count"] = count
                blocks = 61440001 if self.scenario == "sd-card-changed" and count > 1 else 61440000
                free = 1 if self.scenario == "sd-full" else \
                    4096 - math.ceil(self.state.get("sd_used_bytes", 0) / MIB)
                self.pending = (f"OK SD PRESENT=1 MOUNTED=1 TYPE=SDHC/SDXC "
                    f"CAPACITY=30000MiB FREE={free}MiB BLOCKS={blocks} "
                    "BUS=4 CLOCKDIV=2\r\n").encode()
            return len(data)
        if command.startswith("SD STRESS ") and command != "SD STRESS STOP":
            if self.scenario == "sd-existing":
                self.pending = (b"ERR SD SDTEST.BIN already exists; "
                                b"manual SD CLEAN required\r\n")
            else:
                _, _, size, passes = command.split()
                self.state.update(sd_active=True, sd_size_mib=int(size),
                                  sd_passes=int(passes), sd_pass=1, sd_stage="write")
                self.pending = (f"OK SD STRESS START size={size}MiB passes={passes} "
                    "file=SDTEST.BIN chunk=16384; do not remove card\r\n"
                    f"SD STRESS pass=1/{passes} phase=WRITE\r\n").encode()
            return len(data)
        if command == "SD STRESS STOP":
            if self.scenario == "sd-disconnect" and self.state.get("sd_active"):
                raise OSError("simulated disconnect prevented SD cleanup")
            self.state["sd_active"] = False
            self.pending = b"OK SD STRESS STOP cleaned=1 written=16384 verified=0\r\n"
            return len(data)
        if command.startswith("WAV FETCH "):
            name = command[10:]
            if self.scenario == "wav-missing":
                self.pending = b"ERR WAV open failed result=4\r\n"
            else:
                recorded = self.state.get("recordings", {}).get(name)
                wav = _wav_bytes(self.scenario) if recorded is None else \
                    _recorded_wav(recorded, self.scenario)
                chunk = 73 if recorded is None else 16384
                self.state.update(wav_active=True, wav_data=wav, wav_offset=0,
                                  wav_chunk=chunk, wav_waiting_ack=False,
                                  wav_bulk=recorded is not None)
                self.pending = (f"OK WAV START file={name} bytes={len(wav)} "
                                f"chunk={chunk} protocol=1\r\n").encode()
            return len(data)
        if command.startswith("WAV ACK "):
            acknowledged = int(command[8:])
            if self.state.get("wav_waiting_ack") and acknowledged == self.state.get("wav_next"):
                self.state["wav_offset"] = acknowledged
                self.state["wav_waiting_ack"] = False
            return len(data)
        if command == "WAV ABORT":
            self.state["wav_active"] = False
            self.pending = b"OK WAV ABORT\r\n"
            return len(data)
        if command == "DIAG IDENTITY":
            identity_count = self.state.get("identity_requests", 0) + 1
            self.state["identity_requests"] = identity_count
            identity_epoch = 2 if self.scenario == "identity-reset" and identity_count > 1 else 1
        else:
            identity_epoch = self.state.get("boot_epoch", 1)
        answers = {
            "LOG STATUS": b"OK LOG QUEUED=0 PEAK=128 DROP_WRITES=0 DROP_BYTES=0 TX_LOST=0 TX_BYTES=2048 TX_ERRORS=0 CONTEXT=0 FLIGHT=0\r\n",
            "DIAG STATUS": b"OK DIAG V=1 CORE=7 COUNT=1 OVERWRITTEN=0 SD_MAX_MS=25 LOOP_MAX_MS=0 RADIO_OVR=0 PDM_OVR=0 SD_ERR=0 AUDIO_ERR=0 HAS_FAULT=0\r\n",
            "DIAG IDENTITY": (f"OK IDENTITY V={'2' if self.scenario == 'identity-v2' else '1'} "
                f"CORE=7 BUILD={'wrong-build' if self.scenario == 'identity-mismatch' else 'test-build'} "
                f"BOOT={identity_epoch} RESET=1 CAPS=15\r\n").encode(),
            "DIAG LAST": b"OK DIAG LAST NONE\r\n",
            "DIAG DUMP": b"OK DIAG DUMP V=1 CORE=7 FIRST=1 COUNT=1\r\nDIAG EVENT SEQ=1 MS=0 EVENT=BOOT A=1 B=7\r\nOK DIAG END COUNT=1 GAPS=0\r\n"}
        if command == "DIAG IDENTITY":
            if self.scenario == "identity-numeric":
                answers[command] = (f"OK IDENTITY V=1 CORE=7 BUILD=20260929 "
                                    f"BOOT={identity_epoch} RESET=1 CAPS=15\r\n").encode()
            elif self.scenario == "identity-uninitialized":
                answers[command] = b"ERR IDENTITY not initialized\r\n"
            elif self.scenario == "identity-unsupported":
                answers[command] = b"ERR unknown command; type HELP\r\n"
        if command == "IPC STATUS":
            count = self.state.get("ipc_requests", 0) + 1
            self.state["ipc_requests"] = count
            sequence = 100 if self.scenario == "ipc-stale" else 100 + count
            if self.scenario == "record-ipc-stale" and self.state.get("recording_active"):
                sequence = self.state.setdefault("record_ipc_sequence", 100 + count)
            answers[command] = (f"OK IPC LINK=UP VERSION=1 PEER_VERSION=1 TX={sequence} "
                f"RX={sequence + 7} ACK={sequence - 1} ROUNDTRIPS={sequence - 1} "
                "PEER_SEEN=1 ACK_SEEN=1 RX_AGE=40 ACK_AGE=40 ERROR=0 PEER_ERROR=0 BUSY=0\r\n").encode()
            if self.scenario == "ipc-disabled":
                answers[command] = b"OK IPC DISABLED; build preset IpcSmoke for bench test\r\n"
            elif self.scenario == "ipc-error":
                answers[command] = answers[command].replace(b"ERROR=0", b"ERROR=3", 1)
        elif command.startswith("RECORD START "):
            seconds = int(command.split()[-1])
            self.state.update(recording_active=True, recording_seconds=seconds,
                recording_started=self.clock.now(), next_record_progress=5.0,
                stop_requested=False, record_filename="REC900.WAV")
            answers[command] = (f"OK RECORD START file=REC900.WAV duration={seconds}s "
                "format=48000Hz/16-bit/3ch [radio-L,radio-R,mic]\r\n").encode()
        elif command in ("STATUS", "VOLUME STATUS"):
            if self.scenario == "audio-stopped":
                answers[command] = b"ERR RADIO audio path is not running\r\n"
            elif command == "STATUS":
                answers[command] = (b"OK RADIO BAND=FM FREQ=98100 kHz (98.100 MHz) "
                                    b"RSSI=38 SNR=21 VALID=1\r\n")
            else:
                answers[command] = b"OK VOLUME ADC=32768 LEVEL=50% ATTEN=-12.5 dB MUTED=0\r\n"
        elif command == "RECORD STOP":
            self.state["stop_requested"] = True
            answers[command] = b"OK RECORD STOP requested; finalizing next matched block\r\n"
        elif command == "RECORD STATUS":
            if self.scenario == "record-busy" or self.state.get("recording_active"):
                answers[command] = (b"OK RECORD ACTIVE file=REC899.WAV audio=1.0s "
                                    b"queues=0/8,0/8 max-write=25ms margin=OK\r\n")
            else:
                answers[command] = b"OK RECORD IDLE last-file=none frames=0 max-write=0ms\r\n"
        self.pending = answers[command]
        if self.scenario == "diag-fault" and command == "DIAG STATUS":
            self.pending = self.pending.replace(b"HAS_FAULT=0", b"HAS_FAULT=1")
        if self.scenario == "no-response":
            self.pending = b""
        elif self.scenario == "incomplete":
            self.pending = self.pending.split(b"OK DIAG END")[0]
        elif self.scenario == "invalid-schema":
            self.pending = self.pending.replace(b"V=1", b"V=99")
        return len(data)

    def close(self):
        self.closed = True

    def _finish_recording(self, aborted):
        seconds = self.state["recording_seconds"]
        frames = ((seconds * 48000 + 4095) // 4096) * 4096
        audio_ms = frames * 1000 // 48000
        self.state["recording_active"] = False
        # The recorder finalizes aborted files too; both stay on the card.
        kept = frames // 2 if aborted else frames
        self.state.setdefault("recordings", {})[self.state["record_filename"]] = kept
        self.state["sd_used_bytes"] = self.state.get("sd_used_bytes", 0) + 44 + kept * 6
        if aborted:
            result = (f"ERR RECORD ABORT file={self.state['record_filename']} frames={frames // 2} "
                f"bytes={frames * 3} reason=simulated failure finalized=1\r\n")
        else:
            result = (f"OK RECORD PASS file={self.state['record_filename']} frames={frames} "
                f"bytes={frames * 6} audio={audio_ms // 1000}.{audio_ms % 1000:03d}s "
                f"elapsed={audio_ms + 40}ms\r\n")
        radio_high = 8 if self.scenario == "record-overrun" else 1
        self.pending = (result + f"RECORD DIAG queues radio={radio_high}/8 pdm=1/8 "
            "max-write=25ms peaks=1800,1790,500 "
            f"margin={'LOW' if radio_high >= 4 else 'OK'}\r\n").encode()
