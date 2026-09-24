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
            answers[command] = (f"OK IPC LINK=UP VERSION=1 PEER_VERSION=1 TX={sequence} "
                f"RX={sequence + 7} ACK={sequence - 1} ROUNDTRIPS={sequence - 1} "
                "PEER_SEEN=1 ACK_SEEN=1 RX_AGE=40 ACK_AGE=40 ERROR=0 PEER_ERROR=0 BUSY=0\r\n").encode()
            if self.scenario == "ipc-disabled":
                answers[command] = b"OK IPC DISABLED; build preset IpcSmoke for bench test\r\n"
            elif self.scenario == "ipc-error":
                answers[command] = answers[command].replace(b"ERROR=0", b"ERROR=3", 1)
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
