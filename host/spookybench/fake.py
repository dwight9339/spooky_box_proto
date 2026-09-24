"""Deterministic simulated devices. No hardware calls or real-time sleeps."""
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
        self.clock.sleep(0.1)
        self.reads += 1
        if self.scenario == "disconnect" and (self.role == "probe" or self.writes):
            raise OSError("simulated cable disconnect")
        if self.role == "probe":
            if self.scenario == "uart-empty":
                return b""
            return (b"simulated UART\r\n\xffpartial" * 200)[:size] if self.reads == 1 else b""
        if self.scenario == "record-disconnect" and self.state.get("recording_active"):
            raise OSError("simulated target disconnect during recording")
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
        out, self.pending = self.pending[:17], self.pending[17:]
        return out

    def write(self, data):
        self.writes.append(data)
        command = data.decode().strip()
        answers = {
            "LOG STATUS": b"OK LOG QUEUED=0 PEAK=128 DROP_WRITES=0 DROP_BYTES=0 TX_LOST=0 TX_BYTES=2048 TX_ERRORS=0 CONTEXT=0 FLIGHT=0\r\n",
            "DIAG STATUS": b"OK DIAG V=1 CORE=7 COUNT=1 OVERWRITTEN=0 SD_MAX_MS=25 LOOP_MAX_MS=0 RADIO_OVR=0 PDM_OVR=0 SD_ERR=0 AUDIO_ERR=0 HAS_FAULT=0\r\n",
            "DIAG LAST": b"OK DIAG LAST NONE\r\n",
            "DIAG DUMP": b"OK DIAG DUMP V=1 CORE=7 FIRST=1 COUNT=1\r\nDIAG EVENT SEQ=1 MS=0 EVENT=BOOT A=1 B=7\r\nOK DIAG END COUNT=1 GAPS=0\r\n"}
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
        elif command == "RECORD STOP":
            self.state["stop_requested"] = True
            answers[command] = b"OK RECORD STOP requested; finalizing next matched block\r\n"
        elif command == "RECORD STATUS":
            if self.scenario == "record-busy" or self.state.get("recording_active"):
                answers[command] = (b"OK RECORD ACTIVE file=REC899.WAV audio=1.0s "
                                    b"queues=0/8,0/8 max-write=25ms\r\n")
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
        if aborted:
            result = (f"ERR RECORD ABORT file={self.state['record_filename']} frames={frames // 2} "
                f"bytes={frames * 3} reason=simulated failure finalized=1\r\n")
        else:
            result = (f"OK RECORD PASS file={self.state['record_filename']} frames={frames} "
                f"bytes={frames * 6} audio={audio_ms // 1000}.{audio_ms % 1000:03d}s "
                f"elapsed={audio_ms + 40}ms\r\n")
        radio_high = 8 if self.scenario == "record-overrun" else 1
        self.pending = (result + f"RECORD DIAG queues radio={radio_high}/8 pdm=1/8 "
            "max-write=25ms peaks=1800,1790,500\r\n").encode()
