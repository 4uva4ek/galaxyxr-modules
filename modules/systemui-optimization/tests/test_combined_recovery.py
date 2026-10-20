"""Live known-partial-state recovery; every fixture has its own native timer."""
import argparse,hashlib,subprocess,sys,zipfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from collect import adb
p=argparse.ArgumentParser()
p.add_argument("--serial",required=True)
p.add_argument("--output",required=True,type=Path)
a=p.parse_args()
root=Path(__file__).resolve().parents[3]
dist=root/"dist/systemui-optimization"
a.output.mkdir(parents=True,exist_ok=False)
old=dist/"gxr-systemui-optimization-0.3.0-experimental-auto.zip"
assert hashlib.sha256(old.read_bytes()).hexdigest()=="2ce756f15191e945af828a498dd712258f81328bce7310fdca9a6bd90e7469cd"
fixture=a.output/"baseline-0.3.0"
with zipfile.ZipFile(old) as z: fixture.write_bytes(z.read("bin/gxr-systemui"))
pid=adb(a.serial,"pidof com.android.systemui").strip()
assert pid.isdecimal()
paths={}
def invoke(name,mode,ok=True):
    r=subprocess.run(["adb","-s",a.serial,"shell","su","-c",f"'{paths[name]} {pid} {mode}'"],
                     capture_output=True,text=True,timeout=15)
    with (a.output/"commands.txt").open("a") as f:f.write(f"{name} {mode}: rc={r.returncode}\n{r.stdout}{r.stderr}\n")
    if ok: assert r.returncode==0,r.stdout+r.stderr
    return r
try:
    for name,local in (("all",dist/"gxr-systemui"),("base",fixture),("panel",dist/"gxr-panels-candidate")):
        remote=f"/dev/gxr-recovery-{name}-20261020"
        transfer=f"/data/local/tmp/gxr-recovery-{name}-20261020"
        adb(a.serial,f"su -c 'test ! -e {remote} && test ! -e {transfer}'")
        subprocess.run(["adb","-s",a.serial,"push",str(local),transfer],check=True,capture_output=True)
        adb(a.serial,f"su -c 'cp {transfer} {remote} && chmod 700 {remote} && rm {transfer}'")
        paths[name]=remote
    assert invoke("all","status").stdout.startswith("DISABLED")
    for fixture_name in ("base","panel"):
        worker=None
        try:
            adb(a.serial,"input keyevent KEYCODE_WAKEUP")
            worker=subprocess.Popen(["adb","-s",a.serial,"shell","su","-c",f"'{paths[fixture_name]} {pid} test'"],
                                    stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            first=worker.stdout.readline().decode()
            assert first.startswith("ENABLED"),first
            for mode in ("status","on"):
                r=invoke("all",mode,ok=False)
                assert r.returncode!=0 and "Mixed combined patch state" in r.stderr
            assert invoke("all","off").stdout.startswith("DISABLED")
            assert invoke("all","off").stdout.startswith("DISABLED")
            assert invoke("all","status").stdout.startswith("DISABLED")
            print("PASS known mixed state:",fixture_name,flush=True)
        finally:
            adb(a.serial,"input keyevent KEYCODE_SLEEP")
            if worker:
                out,err=worker.communicate(timeout=30)
                (a.output/f"{fixture_name}-timer.txt").write_text(first+out.decode()+err.decode())
                assert worker.returncode==0
finally:
    try:
        if "all" in paths:
            assert invoke("all","off").stdout.startswith("DISABLED")
        for remote in paths.values():adb(a.serial,f"su -c 'rm {remote}'")
    finally:adb(a.serial,"input keyevent KEYCODE_SLEEP")
