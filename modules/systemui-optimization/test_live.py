"""Run a bounded RAM-only SystemUI experiment; never enables a module."""
import argparse
import json
import re
import subprocess
import time
import uuid
from pathlib import Path
from collect import adb, stat

REMOTE = "/dev/gxr-systemui-depth-test-20261020"
TRANSFER = "/data/local/tmp/gxr-systemui-depth-transfer-20261020"


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--serial", required=True)
    p.add_argument("--binary", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--profile", action="store_true",
                   help="Collect an 8-second user-CPU call graph in each phase (adds profiling overhead).")
    a = p.parse_args()
    assert adb(a.serial, "getprop ro.product.model").strip() in ("SM-I610", "SM_I610")
    pid = adb(a.serial, "pidof com.android.systemui").strip()
    assert pid.isdecimal()
    assert "ab04d6daf57800a42170febaf33cb128441339c06bd6d46f068c0109693fee42" in adb(
        a.serial, "sha256sum /system_ext/lib64/libImpress-jni-vulkan.so")
    for library,digest in (
        ("libspaceflinger_jni_vulkan.so","7c72d2bd2284223877d5d5701fb69206fd2a202fa4867650b606c9245ff36fc5"),
        ("libsysui_jni_soong_vulkan.so","d2060072c6046354d470a5e4949f762160a7d89f20310a8652e57c67805c0863")):
        assert adb(a.serial,f"sha256sum /system_ext/lib64/{library}").startswith(digest)
    assert " /dev tmpfs " in adb(a.serial, "cat /proc/mounts")
    a.output.mkdir(parents=True, exist_ok=False)
    hz = int(adb(a.serial, "getconf CLK_TCK").strip())
    measurements = []
    profiles = []
    def sample(label, duration):
        stamp = adb(a.serial, "date '+%m-%d %H:%M:%S.000'").strip()
        assert re.fullmatch(r"\d\d-\d\d \d\d:\d\d:\d\d\.000", stamp)
        start = stat(adb(a.serial, f"cat /proc/{pid}/stat"))
        begin = time.monotonic()
        profiler = None
        if a.profile:
            remote = f"/dev/gxr-systemui-profile-{uuid.uuid4().hex}.data"
            command = (f"/system/bin/simpleperf record -p {pid} -e cpu-clock:u -f 99 "
                       f"--call-graph dwarf,16384 --no-callchain-joiner --duration 8 "
                       f"--size-limit 32M -o {remote}")
            profiler = subprocess.Popen(["adb", "-s", a.serial, "shell", "su", "-c",
                                         f"'{command}'"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            profiles.append((label, remote, profiler))
        time.sleep(duration)
        end = stat(adb(a.serial, f"cat /proc/{pid}/stat"))
        elapsed = time.monotonic()-begin
        assert start["start"] == end["start"]
        measurements.append({"phase": label, "seconds": elapsed, "log_start_device": stamp,
                             "cpu_percent_one_core": (end["ticks"]-start["ticks"])/hz/elapsed*100})
        (a.output / f"{label}-log.txt").write_text(
            adb(a.serial, f"logcat -d -v threadtime --pid={pid} -T '{stamp}'").replace("\r\n", "\n"),
            encoding="utf-8")
        if profiler:
            out, err = profiler.communicate(timeout=5)
            (a.output / f"{label}-profile-record.txt").write_bytes(out+err)
            assert profiler.returncode == 0, (out+err).decode("utf-8", "replace")
    # Only create our own unique, previously absent inert executable in tmpfs.
    subprocess.run(["adb", "-s", a.serial, "shell", "su", "-c",
                    f"'test ! -e {REMOTE} && test ! -e {TRANSFER}'"],
                   check=True, capture_output=True, timeout=15)
    subprocess.run(["adb", "-s", a.serial, "push", str(a.binary), TRANSFER],
                   check=True, capture_output=True, timeout=30)
    adb(a.serial, f"su -c 'cp {TRANSFER} {REMOTE} && chmod 700 {REMOTE} && rm {TRANSFER}'")
    worker = None
    try:
        adb(a.serial, f"su -c 'chmod 700 {REMOTE}'")
        initial = adb(a.serial, f"su -c '{REMOTE} {pid} status'")
        assert initial.startswith("DISABLED"), initial
        sample("before", 10)
        assert measurements[-1]["cpu_percent_one_core"] >= 5, "Headset is idle; no patch applied"
        worker = subprocess.Popen(["adb", "-s", a.serial, "shell", "su", "-c",
                                   f"'{REMOTE} {pid} test'"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        # Native 'test' prints and flushes this only after patch readback and detach.
        first = worker.stdout.readline().decode("utf-8", "replace")
        assert first.startswith("ENABLED"), first + worker.stderr.read().decode("utf-8", "replace")
        sample("during", 12)
        out, err = worker.communicate(timeout=35)
        (a.output / "test.txt").write_text(first+out.decode("utf-8", "replace")+err.decode("utf-8", "replace"),
                                         encoding="utf-8")
        assert worker.returncode == 0
        assert "DISABLED - original" in out.decode("utf-8", "replace")
        sample("after", 10)
    finally:
        # The native timer also restores independently if the host disconnects.
        if worker is not None and worker.poll() is None:
            worker.communicate(timeout=35)
        restoration = adb(a.serial, f"su -c '{REMOTE} {pid} off'")
        (a.output / "restoration.txt").write_text(restoration, encoding="utf-8")
        assert restoration.startswith("DISABLED"), restoration
        adb(a.serial, f"su -c 'rm {REMOTE}'")
        (a.output / "cpu.json").write_text(json.dumps(measurements, indent=2), encoding="utf-8")
        # Analysis runs only after restoration, outside the bounded patch window.
        for label, remote, profiler in profiles:
            if profiler.poll() is None:
                profiler.communicate(timeout=15)
            try:
                data = subprocess.run(["adb", "-s", a.serial, "exec-out", "su", "-c", f"cat {remote}"],
                                      capture_output=True, check=True, timeout=20).stdout
                (a.output / f"{label}.perf.data").write_bytes(data)
                (a.output / f"{label}-callgraph.txt").write_text(adb(
                    a.serial, f"su -c '/system/bin/simpleperf report -i {remote} --children'"),
                    encoding="utf-8")
            finally:
                adb(a.serial, f"su -c 'rm -f {remote}'")
    print(json.dumps(measurements, indent=2))


if __name__ == "__main__":
    main()
