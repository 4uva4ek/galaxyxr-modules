"""Verify the exact disabled combined module artifact before deployment."""
import hashlib,json,stat,zipfile
from pathlib import Path
module=Path(__file__).resolve().parents[1]
root=module.parents[1]
props=dict(line.split("=",1) for line in (module/"module/module.prop").read_text().splitlines() if "=" in line)
archive=root/"dist/systemui-optimization"/f'gxr-systemui-optimization-{props["version"]}-auto.zip'
digest=hashlib.sha256(archive.read_bytes()).hexdigest()
assert archive.with_suffix(".zip.sha256").read_text().split()[0]==digest
expected={"module.prop","action.sh","customize.sh","uninstall.sh","skip_mount",
          "auto.sh","service.sh","cold-boot-reset.sh","disable","bin/gxr-systemui",
          "NOTICE.txt","NDK-NOTICE.txt","SHA256SUMS"}
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    assert len(z.namelist())==len(set(z.namelist()))
    assert set(z.namelist())==expected
    for info in z.infolist():
        assert stat.S_ISREG(info.external_attr>>16)
        assert not info.filename.startswith("/") and ".." not in info.filename.split("/")
    assert z.read("disable")==z.read("skip_mount")==b""
    checks={}
    for line in z.read("SHA256SUMS").decode().splitlines():
        checksum,name=line.split("  ",1)
        assert name not in checks
        checks[name]=checksum
        assert hashlib.sha256(z.read(name)).hexdigest()==checksum,name
    assert set(checks)==expected-{"SHA256SUMS","disable"}
    packaged=dict(line.split("=",1) for line in z.read("module.prop").decode().splitlines() if "=" in line)
    assert packaged["id"]=="gxr_systemui_optimization"
    assert packaged["version"]==props["version"] and packaged["versionCode"]==props["versionCode"]
    binary=z.read("bin/gxr-systemui")
    assert binary==(archive.parent/"gxr-systemui").read_bytes()
    assert binary[:6]==b"\x7fELF\x02\x01" and binary[18:20]==b"\xb7\x00"
    assert b"single swapchain wait, selected-depth release/submit and panel coalescing" in binary
    assert b"Mixed combined patch state" in binary
    assert b"GXR_UPDATE_STATE=STOCK" in z.read("auto.sh")
    assert b'off|disable)' in z.read("action.sh")
print(json.dumps({"archive":str(archive),"sha256":digest,"files":len(expected),
                  "disabled":True,"binary_sha256":hashlib.sha256(binary).hexdigest()},indent=2))
