"""Bounded panel-only UI regression probe; always restore and sleep."""
import argparse, subprocess, sys
from pathlib import Path
from collect import adb

p=argparse.ArgumentParser()
p.add_argument("--serial",required=True)
p.add_argument("--binary",required=True,type=Path)
p.add_argument("--output",required=True,type=Path)
p.add_argument("--only",choices=("idle","volume","notification","dialog"))
a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=False)
remote="/dev/gxr-panel-ui-native-20261020"
transfer="/data/local/tmp/gxr-panel-ui-native-transfer-20261020"
pid=adb(a.serial,"pidof com.android.systemui").strip()
assert pid.isdecimal()
for library,digest in (
    ("libImpress-jni-vulkan.so","ab04d6daf57800a42170febaf33cb128441339c06bd6d46f068c0109693fee42"),
    ("libspaceflinger_jni_vulkan.so","7c72d2bd2284223877d5d5701fb69206fd2a202fa4867650b606c9245ff36fc5"),
    ("libsysui_jni_soong_vulkan.so","d2060072c6046354d470a5e4949f762160a7d89f20310a8652e57c67805c0863")):
    assert adb(a.serial,f"sha256sum /system_ext/lib64/{library}").startswith(digest)
assert " /dev tmpfs " in adb(a.serial,"cat /proc/mounts")
adb(a.serial,f"su -c 'test ! -e {remote} && test ! -e {transfer}'")
subprocess.run(["adb","-s",a.serial,"push",str(a.binary),transfer],check=True)
adb(a.serial,f"su -c 'cp {transfer} {remote} && chmod 700 {remote} && rm {transfer}'")
worker=None
try:
    adb(a.serial,"input keyevent KEYCODE_WAKEUP")
    assert adb(a.serial,f"su -c '{remote} {pid} status'").startswith("DISABLED")
    worker=subprocess.Popen(["adb","-s",a.serial,"shell","su","-c",f"'{remote} {pid} test-ui'"],
                            stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    first=worker.stdout.readline().decode()
    assert first.startswith("ENABLED"),first
    for mode,flag in [("idle",None),("volume","--show-volume"),("notification","--notification"),("dialog","--dialog")]:
        if a.only and a.only!=mode: continue
        command=[sys.executable,str(Path(__file__).with_name("trace_nodes.py")),
                 "--serial",a.serial,"--output",str(a.output/mode)]
        if flag: command.append(flag)
        subprocess.run(command,check=True,timeout=25)
    assert adb(a.serial,"pidof com.android.systemui").strip()==pid
finally:
    try:
        if worker:
            # Native test owns restoration, including if host is interrupted.
            adb(a.serial,"input keyevent KEYCODE_SLEEP")
            out,err=worker.communicate(timeout=65)
            (a.output/"test.txt").write_text(first+out.decode()+err.decode())
        restored=adb(a.serial,f"su -c '{remote} {pid} off'")
        (a.output/"restoration.txt").write_text(restored)
        assert restored.startswith("DISABLED"),restored
        adb(a.serial,f"su -c 'rm {remote}'")
    finally:
        adb(a.serial,"input keyevent KEYCODE_SLEEP")
        (a.output/"power.txt").write_text(adb(a.serial,"dumpsys power"))
