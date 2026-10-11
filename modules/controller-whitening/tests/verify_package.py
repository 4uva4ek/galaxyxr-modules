"""Verify the disabled automatic ZIP without connecting to a headset."""
from pathlib import Path
import hashlib
import json
import zipfile


MODULE = Path(__file__).resolve().parents[1]
ROOT = MODULE.parents[1]
config = json.loads((MODULE / "build.json").read_text(encoding="utf-8"))
props = dict(
    line.split("=", 1)
    for line in (MODULE / "module/module.prop").read_text(encoding="utf-8").splitlines()
    if "=" in line
)
assert config["variants"] == ["auto"], "Only automatic packages are permitted"
assert config["shared_files"] == [] and config["auto_shared_files"] == ["cold-boot-reset.sh"]
scripts = {
    "action.sh", "customize.sh", "uninstall.sh",
    "auto.sh", "service.sh", "emulated-soft-reboot.sh",
}
module_files = set(config["files"]) | set(config["auto_files"])
assert module_files == scripts | {"module.prop", "skip_mount"}
assert props["id"] == "gxr_controller_whitening"
assert "gxrLiveControl" not in props and "Live control" not in props["name"]
assert not (MODULE / "module/live-control.sh").exists()
assert config["binary"] == "gxr-whitening"
archive = ROOT / "dist" / MODULE.name / (
    f'{config["artifact"]}-{props["version"]}-auto.zip'
)
expected_digest = archive.with_suffix(".zip.sha256").read_text(encoding="ascii").split()[0]
assert expected_digest == hashlib.sha256(archive.read_bytes()).hexdigest()
binary_name = "bin/" + config["binary"]
expected_names = module_files | set(config["auto_shared_files"]) | {
    binary_name, "disable", "SHA256SUMS", "NOTICE.txt", "NDK-NOTICE.txt",
}
with zipfile.ZipFile(archive) as package:
    assert package.read("cold-boot-reset.sh") == (ROOT / "tools/cold-boot-reset.sh").read_bytes()
    assert package.testzip() is None
    names = package.namelist()
    assert len(names) == len(set(names)), "Duplicate ZIP paths"
    assert set(names) == expected_names, set(names) ^ expected_names
    assert package.read("disable") == package.read("skip_mount") == b""
    packaged_props = dict(
        line.split("=", 1)
        for line in package.read("module.prop").decode("utf-8").splitlines()
        if "=" in line
    )
    for field in ("id", "version", "versionCode"):
        assert packaged_props[field] == props[field], field
    assert packaged_props["name"].endswith("(auto)")
    assert "gxrLiveControl" not in packaged_props and "Live control" not in packaged_props["name"]
    assert "live-control.sh" not in names
    for name in module_files - {"module.prop"}:
        assert package.read(name) == (MODULE / "module" / name).read_bytes(), name
    assert package.read("NOTICE.txt") == (MODULE / "NATIVE-NOTICE.txt").read_bytes()
    assert package.read("customize.sh").startswith(b"#!/system/bin/sh\nabort ")
    for name in scripts:
        data = package.read(name)
        assert data.startswith(b"#!/system/bin/sh\n") and b"\r" not in data, name
        assert package.getinfo(name).external_attr >> 16 & 0o777 == 0o755, name
    assert package.getinfo(binary_name).external_attr >> 16 & 0o777 == 0o755
    binary = package.read(binary_name)
    assert binary[:6] == b"\x7fELF\x02\x01" and binary[18:20] == b"\xb7\x00"
    assert b"4dfb10d768c5cdf14b50873057c59eeacc83a3532c78d10b21652bb9727fe01f" in binary
    assert b"/vendor/bin/hw/vendor.samsung.hardware.secxrcontroller@1.0-service" in binary
    covered = set()
    for line in package.read("SHA256SUMS").decode("ascii").splitlines():
        digest, name = line.split("  ", 1)
        assert name not in covered, name
        assert hashlib.sha256(package.read(name)).hexdigest() == digest, name
        covered.add(name)
    assert covered == expected_names - {"disable", "SHA256SUMS"}
print("controller-whitening: disabled auto ZIP, dedicated lifecycle, hashes, ARM64 and firmware guard: PASS")
