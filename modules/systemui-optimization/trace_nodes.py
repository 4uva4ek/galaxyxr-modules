"""Bounded, isolated tracefs uprobe of node activity; no device file patching."""
import argparse
import shlex
import subprocess
import uuid
from pathlib import Path
from collect import adb

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--serial", required=True)
p.add_argument("--output", required=True, type=Path)
p.add_argument("--kind", choices=("nodes", "attributes"), default="nodes")
p.add_argument("--show-volume", action="store_true",
               help="Show volume UI without changing volume, during node tracing.")
p.add_argument("--notification", action="store_true",
               help="Run the pre-uploaded local notification helper with timed removal.")
p.add_argument("--dialog", action="store_true",
               help="Open stock contrast dialog then dismiss with Back; no selection.")
a = p.parse_args()
assert adb(a.serial, "getprop ro.product.model").strip() in ("SM-I610", "SM_I610")
pid = adb(a.serial, "pidof com.android.systemui").strip()
assert pid.isdecimal()
assert adb(a.serial, "sha256sum /system_ext/lib64/libImpress-jni-vulkan.so").startswith(
    "ab04d6daf57800a42170febaf33cb128441339c06bd6d46f068c0109693fee42")
a.output.mkdir(parents=True, exist_ok=False)
(a.output / "maps.txt").write_text(adb(a.serial, f"su -c 'cat /proc/{pid}/maps'"), encoding="utf-8")
tag = "gxr_node_" + uuid.uuid4().hex[:12]
probe = ("0x12f037c node=%x0:x64 enabled=%x1:u32 caller=%x30:x64 flags=+32(%x0):x32 "
         "name_meta=+80(%x0):x8 name_inline=+81(%x0):string name_heap=+0(+96(%x0)):string")
if a.kind == "attributes":
    probe = "0xa04598 validator=%x0:x64 attributes=%x1:x64 count=+0(%x1):u32 caller=%x30:x64"
script = f"""
set -eu
root=/sys/kernel/tracing
inst=$root/instances/{tag}
created=0
event=0
cleanup() {{
  set +e
  if [ "$created" = 1 ]; then
    echo 0 > "$inst/tracing_on"
    if [ "$event" = 1 ]; then echo 0 > "$inst/events/{tag}/set_enabled/enable"; fi
    rmdir "$inst"
  fi
  if [ "$event" = 1 ]; then echo '-:{tag}/set_enabled' >> "$root/uprobe_events"; fi
}}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
test ! -e "$inst"
mkdir "$inst"
created=1
echo 0 > "$inst/tracing_on"
echo 2048 > "$inst/buffer_size_kb"
echo 'p:{tag}/set_enabled /system_ext/lib64/libImpress-jni-vulkan.so:{probe}' >> "$root/uprobe_events"
event=1
echo {pid} > "$inst/set_event_pid"
echo 1 > "$inst/options/event-fork"
echo 1 > "$inst/events/{tag}/set_enabled/enable"
echo 1 > "$inst/tracing_on"
sleep 2
echo 0 > "$inst/tracing_on"
cat "$inst/trace"
"""
# set_event_pid filters thread ids, not process ids: enumerate the target's threads.
tids = adb(a.serial, f"su -c 'ls /proc/{pid}/task'").split()
assert tids and all(t.isdecimal() for t in tids)
script = script.replace(f"echo {pid} >", f"echo {' '.join(tids)} >")
if a.show_volume:
    assert a.kind == "nodes"
    script = script.replace("sleep 2", "sleep 1\ncmd media_session volume --stream 3 --adj same --show >&2\nsleep 2")
if a.notification or a.dialog:
    assert a.kind == "nodes" and not a.show_volume
    assert not (a.notification and a.dialog)
    script = script.replace("echo 2048 >", "echo 8192 >")
    trigger=("am start -W -a com.android.intent.action.SHOW_CONTRAST_DIALOG -p com.android.systemui >&2\nsleep 3\ninput keyevent KEYCODE_BACK"
             if a.dialog else "CLASSPATH=/data/local/tmp/gxr-panel-ui-20261020.dex app_process /system/bin PanelUiProbe >&2")
    script = script.replace("sleep 2", "sleep 1\n"+trigger+"\nsleep 1")
r = subprocess.run(["adb", "-s", a.serial, "shell", "su", "-c", shlex.quote(script)],
                   capture_output=True, timeout=20)
(a.output / "trace.txt").write_bytes(r.stdout)
(a.output / "stderr.txt").write_bytes(r.stderr)
remaining = adb(a.serial, f"su -c 'cat /sys/kernel/tracing/uprobe_events; ls /sys/kernel/tracing/instances'")
assert tag not in remaining, "Temporary trace cleanup incomplete"
assert r.returncode == 0, r.stderr.decode(errors="replace")
print(f"Captured {len(r.stdout)} bytes; isolated trace and uprobe removed.")
