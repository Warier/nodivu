"""Nodivu worker protocol 0.1: Windows named mapping + bounded JSON control.

One packet in flight. stdin grants access; stdout reply returns ownership.
No PCM is serialized. This broker protocol is NOT callable from an RT callback.
Stdout is reserved for protocol; diagnostics must use stderr.
"""
import importlib.util
import json
import math
import mmap
import os
from pathlib import Path
import struct
import sys
import time

# -I isolates Python from user/PYTHONPATH; load the adjacent implementation explicitly.
spec = importlib.util.spec_from_file_location("nodivu_example_dsp", Path(__file__).with_name("dsp.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
Processor = module.Processor
MAX_CONTROL = 4096
HEADER = struct.Struct("<8s6I")
EXPECTED = (b"NDVWRK01", 1, 48000, 2, 480, 64, 3904)
MAPPING_BYTES = 7744  # 64 header + two stereo planar banks of 480*2*4 bytes


def reply(message):
    data = json.dumps(message, allow_nan=False, separators=(",", ":")).encode() + b"\n"
    if len(data) > MAX_CONTROL:
        raise ValueError("response too large")
    sys.stdout.buffer.write(data)
    sys.stdout.buffer.flush()


def reject_constant(value):
    raise ValueError("non-finite JSON number: " + value)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON key")
        result[key] = value
    return result


def run():
    mapping = None
    processor = None
    epoch, sequence = None, 0
    try:
        while True:
            raw = sys.stdin.buffer.readline(MAX_CONTROL + 1)
            if not raw:
                return
            if len(raw) > MAX_CONTROL or not raw.endswith(b"\n"):
                raise ValueError("invalid control frame")
            request = json.loads(raw, parse_constant=reject_constant, object_pairs_hook=unique_object)
            if not isinstance(request, dict) or request.get("version") != 1:
                raise ValueError("invalid protocol version")
            if type(request.get("seq")) is not int or request["seq"] != sequence + 1:
                raise ValueError("invalid sequence")
            sequence = request["seq"]
            if not isinstance(request.get("epoch"), str) or len(request["epoch"]) > 128:
                raise ValueError("invalid epoch")
            if epoch is not None and request["epoch"] != epoch:
                raise ValueError("old epoch")
            epoch = request["epoch"]
            response = {"version": 1, "epoch": epoch, "seq": sequence, "ok": True}
            operation = request.get("op")
            if operation == "hello" and mapping is None:
                if request.get("layout") != "f32le-planar-stereo-480":
                    raise ValueError("unsupported PCM format")
                mapping = mmap.mmap(-1, MAPPING_BYTES, tagname=request["mapping"], access=mmap.ACCESS_WRITE)
                if HEADER.unpack_from(mapping) != EXPECTED:
                    raise ValueError("invalid mapping header")
                # Host has assigned the process to its Job Object before sending hello.
                # Keep module imports side-effect free; initialize models/children here.
                processor = Processor()
            elif mapping is None:
                raise ValueError("hello required")
            elif operation == "close":
                reply(response)
                return
            elif operation == "reset":
                processor.reset()
            elif operation == "begin":
                processor.begin(request.get("frames"))
            elif operation == "process":
                frames = request.get("frames")
                if type(frames) is not int or not 1 <= frames <= 480:
                    raise ValueError("frames outside 1..480")
                incoming = memoryview(mapping)[64:3904].cast("f")
                outgoing = memoryview(mapping)[3904:7744].cast("f")
                try:
                    if any(not math.isfinite(incoming[c * 480 + i]) for c in range(2) for i in range(frames)):
                        raise ValueError("non-finite input")
                    started = time.perf_counter_ns()
                    produced, done = processor.process(request.get("mode"), incoming, outgoing, frames, request.get("gain", 1))
                    response.update(frames=frames, produced=produced, done=done,
                                    processing_us=(time.perf_counter_ns() - started) / 1000)
                    if any(not math.isfinite(outgoing[c * 480 + i]) for c in range(2) for i in range(frames)):
                        raise ValueError("non-finite output")
                finally:
                    incoming.release()
                    outgoing.release()
            elif operation == "test_fault" and "--allow-test-faults" in sys.argv:
                fault = request.get("fault")
                if fault == "exit":
                    os._exit(17)
                if fault == "stall":
                    time.sleep(30)
                if fault == "old_reply":
                    response["seq"] -= 1
                if fault == "wrong_epoch":
                    response["epoch"] = "previous-session"
                if fault == "oversize":
                    sys.stdout.buffer.write(b"x" * 4097)
                    sys.stdout.buffer.flush()
                    return
            else:
                raise ValueError("unsupported operation")
            reply(response)
    finally:
        if mapping is not None:
            mapping.close()


if __name__ == "__main__":
    try:
        run()
    except Exception as error:
        print(type(error).__name__ + ": " + str(error)[:512], file=sys.stderr)
        sys.exit(1)  # Fail closed; parent invalidates this worker/mapping epoch.
