"""Optional Windows acceptance tool: Python + psutil, never a build/runtime dependency."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys
import time

repository = Path(__file__).resolve().parents[2]
local_test_libraries = repository / ".tools" / "aqt"
if local_test_libraries.is_dir():
    sys.path.insert(0, str(local_test_libraries))
import psutil

parser = argparse.ArgumentParser()
parser.add_argument("--package", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--minutes", type=int, default=60, choices=range(1, 61))
args = parser.parse_args()
if sys.platform != "win32":
    parser.error("Este ensaio exige Windows.")
package = args.package.resolve(strict=True)
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=False)
executable = package / "nodivu.exe"
summary = {
    "schema_version": 1,
    "status": "failed",
    "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "windows": platform.version(),
    "psutil": psutil.__version__,
    "requested_minutes": args.minutes,
    "frontend_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
    "backend_sha256": hashlib.sha256((package / "bin/nodivu-app-backend.exe").read_bytes()).hexdigest(),
    "scope": "One process lifetime; idle 3-node graph, no audio endpoints; private committed bytes, handles and threads per process.",
}
samples = []
process = None
try:
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    ready = output / "ready.png"
    started = time.monotonic()
    with (output / "application.log").open("w", encoding="utf-8") as log:
        process = subprocess.Popen(
            [str(executable), "--portable", "--smoke-test", "--benchmark-ms",
             str(args.minutes * 60000), "--screenshot", str(ready)],
            stdout=log, stderr=log, startupinfo=startup,
            creationflags=subprocess.CREATE_NO_WINDOW,
        )
        while not ready.exists() and time.monotonic() - started < 30:
            if process.poll() is not None:
                raise RuntimeError(f"GUI exited before readiness: {process.returncode}")
            time.sleep(0.1)
        if not ready.exists():
            raise RuntimeError("No ready screenshot in 30 seconds")
        ready_time = time.monotonic()
        next_sample = ready_time
        deadline = ready_time + args.minutes * 60 + 30
        with (output / "samples.jsonl").open("w", encoding="utf-8") as stream:
            while process.poll() is None and time.monotonic() < deadline:
                now = time.monotonic()
                if now >= next_sample:
                    owner = psutil.Process(process.pid)
                    records = []
                    for child in [owner] + owner.children(recursive=True):
                        try:
                            memory = child.memory_info()
                            cpu = child.cpu_times()
                            records.append({"pid": child.pid, "name": child.name(),
                                "private_bytes": memory.private, "working_set_bytes": memory.rss,
                                "handles": child.num_handles(), "threads": child.num_threads(),
                                "cpu_seconds": cpu.user + cpu.system})
                        except psutil.NoSuchProcess:
                            continue
                    sample = {"elapsed_seconds": now - ready_time, "processes": records,
                              "tree_private_bytes": sum(r["private_bytes"] for r in records),
                              "tree_handles": sum(r["handles"] for r in records)}
                    samples.append(sample)
                    stream.write(json.dumps(sample) + "\n")
                    stream.flush()
                    if len(samples) % 6 == 0:
                        print(f"Soak {sample['elapsed_seconds'] / 60:.1f}/{args.minutes} min: "
                              f"{sample['tree_private_bytes'] / 1048576:.2f} MiB, "
                              f"{sample['tree_handles']} handles", flush=True)
                    next_sample = now + 10
                time.sleep(0.25)
        if process.poll() is None:
            raise RuntimeError("GUI did not finish the bounded soak")
        summary["exit_code"] = process.returncode
        summary["elapsed_seconds"] = time.monotonic() - ready_time
        if process.returncode != 0 or summary["elapsed_seconds"] < args.minutes * 60 - 2:
            raise RuntimeError("GUI exited early or with failure")
        warm = [s for s in samples if 300 <= s["elapsed_seconds"] < 900]
        final = [s for s in samples if s["elapsed_seconds"] >= args.minutes * 60 - 600]
        if args.minutes >= 30:
            first_memory = statistics.median(s["tree_private_bytes"] for s in warm)
            final_memory = statistics.median(s["tree_private_bytes"] for s in final)
            first_handles = statistics.median(s["tree_handles"] for s in warm)
            final_handles = statistics.median(s["tree_handles"] for s in final)
            summary.update(warm_private_bytes=first_memory, final_private_bytes=final_memory,
                           warm_handles=first_handles, final_handles=final_handles)
            if final_memory - first_memory > max(5 * 1048576, first_memory * 0.10):
                raise RuntimeError("Memory grew beyond the acceptance budget after warmup")
            if final_handles - first_handles > 10:
                raise RuntimeError("Handles grew beyond the acceptance budget after warmup")
        summary["sample_count"] = len(samples)
        summary["status"] = "passed"
except Exception as error:
    summary["error"] = str(error)
finally:
    if process is not None and process.poll() is None:
        # Only the explicitly owned test process, never an executable name.
        process.terminate()
        process.wait(timeout=10)
    summary["finished_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    (output / "result.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
print(json.dumps(summary, indent=2), flush=True)
sys.exit(0 if summary["status"] == "passed" else 1)
