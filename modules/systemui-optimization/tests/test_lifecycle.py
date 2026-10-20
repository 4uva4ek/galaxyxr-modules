"""Host supervision scenarios with mocked device identity, native helper and boot state."""
import os
import re
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
BASH = r"C:\Program Files\Git\bin\bash.exe"
SOURCE = (HERE / "module/auto.sh").read_text()
for scenario in ("normal", "exit", "applyfail", "restorefail", "pending", "hashfail"):
    with tempfile.TemporaryDirectory(prefix="gxr-systemui-life-") as temp:
        root = Path(temp)
        (root / "bin").mkdir()
        state = root / "ram"
        (root / "fake-identity").write_text("123:100\n")
        (root / "cold-boot-reset.sh").write_text("exit 0\n")
        if scenario == "pending":
            (root / "autostart").mkdir()
            (root / "autostart/pending").write_text("")
        script = SOURCE.replace("STATE=/dev/gxr_systemui_optimization", f"STATE='{state.as_posix()}'")
        script = re.sub(r"identity\(\) \{.*?\n\}", 'identity() { cat "$MODDIR/fake-identity"; }', script, count=1, flags=re.S)
        script = script.replace(
            "awk '$2==\"/dev\" && $3==\"tmpfs\" {ok=1} END {exit !ok}' /proc/mounts",
            "true")
        mocks = """
id() { echo 0; }
getprop() { echo 1; }
sha256sum() {
    case "$1" in
      *libspaceflinger*) if [ "$SCENARIO" = hashfail ]; then echo 'bad fixture'; else echo "$SPACE_PIN  fixture"; fi;;
      *libsysui*) echo "$SYSUI_PIN  fixture";;
      *) echo "$PIN  fixture";;
    esac
}
sleep() {
    case "$SCENARIO" in
      exit) : > "$MODDIR/fake-identity"; rm -f "$MODDIR/active";;
      *) : > "$MODDIR/disable";;
    esac
}
"""
        (root / "auto.sh").write_text(mocks + script, newline="\n")
        (root / "action.sh").write_text((HERE/"module/action.sh").read_text(), newline="\n")
        (root / "bin/gxr-systemui").write_text("""#!/bin/sh
MODDIR=${0%/bin/*}
echo "$2" >> "$MODDIR/operations"
case "$2" in
on) [ "$SCENARIO" != applyfail ] || exit 1; : > "$MODDIR/active"; echo ENABLED;;
off) [ "$SCENARIO" != restorefail ] || exit 1; rm -f "$MODDIR/active"; echo DISABLED;;
status) [ ! -f "$MODDIR/status-broken" ] || exit 1; if [ -f "$MODDIR/active" ]; then echo 'ENABLED - fixture'; else echo 'DISABLED - fixture'; fi;;
esac
""", newline="\n")
        result = subprocess.run([BASH, (root / "auto.sh").as_posix(), "run"], capture_output=True,
                                text=True, env={**os.environ, "SCENARIO": scenario}, timeout=10)
        operations = (root / "operations").read_text() if (root / "operations").exists() else ""
        assert not (state / "worker").exists(), (scenario, result.stdout, result.stderr)
        if scenario == "normal":
            assert result.returncode == 0 and "on\n" in operations and "off\n" in operations, (result.stdout, result.stderr, operations)
            assert not (root / "active").exists()
            assert not (root / "autostart/pending").exists()
            # Installer-facing off must actually restore, and STOCK must never
            # be emitted when native verification fails.
            (root/"active").write_text("")
            (state/"target").write_text("123:100\n")
            report=subprocess.run([BASH,(root/"action.sh").as_posix(),"off"],
                                  capture_output=True,text=True,
                                  env={**os.environ,"SCENARIO":scenario},timeout=10)
            assert report.returncode==0 and "GXR_UPDATE_STATE=STOCK" in report.stdout,report
            assert not (root/"active").exists()
            (root/"status-broken").write_text("")
            report=subprocess.run([BASH,(root/"action.sh").as_posix(),"status"],
                                  capture_output=True,text=True,
                                  env={**os.environ,"SCENARIO":scenario},timeout=10)
            assert report.returncode!=0 and "GXR_UPDATE_STATE=STOCK" not in report.stdout,report
        else:
            assert result.returncode != 0, (scenario, result.stdout, result.stderr)
            assert (root / "disable").exists()
            assert (root / "autostart/failed").exists()
            if scenario in ("pending", "hashfail"):
                assert "on\n" not in operations
            if scenario == "restorefail":
                assert (root / "active").exists()
                assert "Restoration unconfirmed" in result.stdout
        print("PASS lifecycle:", scenario)
