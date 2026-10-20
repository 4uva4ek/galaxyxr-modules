"""Read-only SystemUI baseline collection; writes evidence only on the PC."""
import argparse
import json
import subprocess
import time
from pathlib import Path


def adb(serial, command, timeout=40):
    result = subprocess.run(
        ["adb", "-s", serial, "shell", command],
        capture_output=True, timeout=timeout,
    )
    if result.returncode:
        raise RuntimeError(result.stderr.decode("utf-8", "replace"))
    return result.stdout.decode("utf-8", "replace")


def stat(text):
    # comm can contain spaces and parentheses; fields after its closing ')' start at 3.
    fields = text[text.rindex(")") + 2:].split()
    return {"ticks": int(fields[11]) + int(fields[12]), "start": int(fields[19])}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seconds", type=int, default=15, choices=range(5, 61))
    args = parser.parse_args()
    model = adb(args.serial, "getprop ro.product.model").strip()
    if model != "SM-I610" and model != "SM_I610":
        raise RuntimeError(f"Not the expected Galaxy XR: {model!r}")
    pid = adb(args.serial, "pidof com.android.systemui").strip()
    if not pid.isdecimal():
        raise RuntimeError(f"Expected exactly one SystemUI PID, got {pid!r}")
    args.output.mkdir(parents=True, exist_ok=False)
    commands = {
        "identity": "getprop ro.build.fingerprint; cat /proc/sys/kernel/random/boot_id; date; uptime",
        "meminfo": "dumpsys meminfo com.android.systemui",
        "systemui": "dumpsys activity service com.android.systemui/.SystemUIService",
        "logcat": f"logcat -d -v threadtime --pid={pid} -t 6000",
        "crash": "logcat -b crash -d -v threadtime -t 1500",
        "surfaces": "dumpsys SurfaceFlinger --list",
        "threads": f"top -H -b -n 2 -d 2 -p {pid}",
    }
    errors = {}
    for name, command in commands.items():
        try:
            (args.output / f"{name}.txt").write_text(adb(args.serial, command), encoding="utf-8")
        except (RuntimeError, subprocess.TimeoutExpired) as exc:
            errors[name] = str(exc)
    hz = int(adb(args.serial, "getconf CLK_TCK").strip())
    before = stat(adb(args.serial, f"cat /proc/{pid}/stat"))
    start = time.monotonic()
    time.sleep(args.seconds)
    after = stat(adb(args.serial, f"cat /proc/{pid}/stat"))
    elapsed = time.monotonic() - start
    current_pid = adb(args.serial, "pidof com.android.systemui").strip()
    valid = before["start"] == after["start"] and current_pid == pid
    summary = {
        "pid": pid, "model": model, "seconds": elapsed, "clock_ticks": hz,
        "same_process": valid,
        "cpu_percent_one_core": (after["ticks"] - before["ticks"]) / hz / elapsed * 100 if valid else None,
        "capture_errors": errors,
        "limits": "Sequential captures, no call stacks; CPU includes user+kernel. Log files may contain private data.",
    }
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary, indent=2))
    if errors or not valid:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
