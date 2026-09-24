"""Bound all worker activity, including Archive.close and filesystem calls."""
import json
import multiprocessing
import time
from .result import outcome, utc_now
from .runner import worker


def supervise(options, seconds, target=worker):
    started, started_at = time.monotonic(), utc_now()
    options = dict(options, deadline=started + seconds - 2.0,
                   started=started, started_at=started_at)
    context = multiprocessing.get_context("spawn")
    # No pipe/queue can block on a partial frame or feeder-thread shutdown.
    result_buffer = context.Array("B", 65536, lock=False)
    result_length = context.Value("I", 0, lock=False)
    notice_buffer = context.Array("B", 8192, lock=False)
    notice_length = context.Value("I", 0, lock=False)
    process = context.Process(target=target, args=(options, result_buffer, result_length,
                                                  notice_buffer, notice_length), daemon=True)
    reason = None
    cleanup = "not_needed"
    try:
        process.start()
        while process.is_alive() and time.monotonic() < options["deadline"]:
            process.join(min(0.05, max(0, options["deadline"] - time.monotonic())))
        if process.is_alive() or time.monotonic() >= options["deadline"]:
            reason = "operation_timeout"
    except KeyboardInterrupt:
        reason = "interrupted"
    finally:
        if process.pid is not None and process.is_alive():
            process.terminate()
            process.join(1.0)
            if process.is_alive():
                process.kill()
                process.join(1.0)
            cleanup = "failed" if process.is_alive() else "terminated"
    if reason is None and process.exitcode == 0 and result_length.value:
        value = json.loads(bytes(result_buffer[:result_length.value]))
    else:
        artifacts = {}
        if notice_length.value:
            artifacts["run_dir"] = bytes(notice_buffer[:notice_length.value]).decode("utf-8")
        value = outcome(options["command"], "simulated" if options["simulate"] else "hardware",
            started_at, started, result="error", reason=reason or "worker_failed", artifacts=artifacts,
            metrics={"cleanup": cleanup, "evidence_complete": False,
                     "final_target_state": "unknown", "human_required": cleanup == "failed"},
            detail="Operation stopped; any run without a final result must be treated as incomplete")
    if not process.is_alive():
        process.close()
    return value
