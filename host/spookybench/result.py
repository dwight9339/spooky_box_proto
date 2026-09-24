"""One result schema for CLI errors, workers, and simulated operations."""
from datetime import datetime, timezone
import time


class BenchError(Exception):
    def __init__(self, reason, detail, result="error"):
        super().__init__(detail)
        self.reason, self.result = reason, result


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def outcome(command, execution, started_at, started, *, result="pass", reason=None,
            metrics=None, artifacts=None, detail=None):
    value = {"schema_version": 1, "command": command, "result": result,
             "reason": reason, "execution": execution, "metrics": metrics or {},
             "artifacts": artifacts or {}, "timestamps": {
                 "started_at": started_at, "ended_at": utc_now(),
                 "duration_ms": round((time.monotonic() - started) * 1000)}}
    if detail:
        value["detail"] = str(detail)[:2000]
    return value


def exit_code(value):
    if value["reason"] == "interrupted":
        return 130
    return {"pass": 0, "fail": 1, "error": 2, "unsupported": 3}[value["result"]]
