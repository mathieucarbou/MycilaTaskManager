#!/usr/bin/env python3
"""Build, flash and verify each example on a connected ESP32.

For every example:
  1. build it with the matching environment
  2. upload it to the board
  3. capture the serial output for the example's duration
  4. require every printed check to be PASS and the summary to say 0 failed

Exit code: 0 if every example passes, 1 otherwise.
"""
import re
import subprocess
import sys
import time

PORT = "/dev/cu.usbserial-0001"

# example -> (env, capture seconds)
EXAMPLES = [
    ("examples/TaskManager", "arduino-3", 9),
    ("examples/AsyncTaskManager", "arduino-3", 9),
    ("examples/Json", "json", 8),
    ("examples/Watchdog", "arduino-3", 15),
    ("examples/PauseResume", "arduino-3", 8),
    ("examples/TaskStates", "arduino-3", 6),
    ("examples/ConditionalTasks", "arduino-3", 8),
]


def run(cmd, **kw):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True, **kw)


def main() -> int:
    only = sys.argv[1] if len(sys.argv) > 1 else None
    failures = []

    for src_dir, env, seconds in EXAMPLES:
        name = src_dir.split("/")[-1]
        if only and only not in name:
            continue
        print(f"\n=== {name} (env={env}, capture={seconds}s) ===")

        print(f"  building {name}...")
        r = run(f"PLATFORMIO_SRC_DIR={src_dir} pio run -e {env}")
        if "SUCCESS" not in r.stdout:
            print(f"  BUILD FAILED:\n{r.stdout[-2000:]}{r.stderr[-2000:]}")
            failures.append((name, "build"))
            continue

        print(f"  uploading {name}...")
        r = run(f"PLATFORMIO_SRC_DIR={src_dir} pio run -e {env} -t upload --upload-port {PORT}")
        if "SUCCESS" not in r.stdout:
            print(f"  UPLOAD FAILED:\n{r.stdout[-2000:]}{r.stderr[-2000:]}")
            failures.append((name, "upload"))
            continue

        print(f"  capturing {seconds}s of serial output...")
        r = run(f"python3 tools/serial-capture.py {seconds}")
        output = r.stdout

        n_pass = len(re.findall(r"^PASS:", output, re.M))
        n_fail = len(re.findall(r"^FAIL:", output, re.M))
        m = re.search(r"== RESULT: (\d+) passed, (\d+) failed ==", output)
        complete = "== " in output and "complete ==" in output

        print(f"  checks: {n_pass} PASS, {n_fail} FAIL")
        for line in output.splitlines():
            if line.startswith("FAIL"):
                print(f"    {line}")

        ok = n_fail == 0 and m and int(m.group(2)) == 0 and complete
        if ok:
            print(f"  => {name}: OK")
        else:
            print(f"  => {name}: FAILED")
            if not complete:
                print("    (the summary line was not reached: capture too short or crash)")
            failures.append((name, "verify"))

        time.sleep(1)

    print("\n================ SUMMARY ================")
    if failures:
        for name, stage in failures:
            print(f"  {name}: FAILED at {stage}")
        return 1
    print("  all examples passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())