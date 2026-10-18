"""Exercise real settings commands on a disposable local directory (Linux)."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    with tempfile.TemporaryDirectory(prefix="gxr-suite-settings-") as temp:
        root = Path(temp)
        shutil.copyfile(ROOT / "tools/suite-settings.sh", root / "settings.sh")
        (root / "suite.conf").write_text(f'SUITE_ID=gxr_settings_test_{os.getpid()}\nSETTING_KEYS="TAIL_GAIN_MILLI POSE_TIME_US"\n')
        (root / "disable").touch()
        def run(*args):
            return subprocess.run(["sh", str(root / "settings.sh"), *args], capture_output=True, text=True)
        initial = run("get")
        assert initial.returncode == 0, initial.stderr
        assert initial.stdout == "TAIL_GAIN_MILLI=1000\nPOSE_TIME_US=0\n"
        for key, value in [("TAIL_GAIN_MILLI", "500"), ("POSE_TIME_US", "12345")]:
            result = run("set", key, value)
            assert result.returncode == 0, result.stderr
        before = (root / "settings.conf").read_bytes()
        for key, value in [("TAIL_GAIN_MILLI", "1001"), ("POSE_TIME_US", "30001"), ("EXTRAPOLATION", "1"), ("POSE_TIME_US", "-1"), ("POSE_TIME_US", "1; touch injected"), ("UNKNOWN", "1")]:
            assert run("set", key, value).returncode != 0
            assert (root / "settings.conf").read_bytes() == before
        assert (root / "disable").exists()
        assert not (root / "injected").exists()
        assert not (root / "settings.tmp").exists()
        assert "POSE_TIME_US=12345" in run("get").stdout
        print("PASS: defaults, atomic settings preservation, bounds, invalid input, disabled preference")

if __name__ == "__main__":
    main()
