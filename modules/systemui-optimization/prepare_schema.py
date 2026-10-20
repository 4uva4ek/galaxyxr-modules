"""Reproduce the pinned ARM64 payload from source; no headset access."""
import argparse
import hashlib
import struct
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
PIN = "ab04d6daf57800a42170febaf33cb128441339c06bd6d46f068c0109693fee42"


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--library", type=Path, required=True)
    p.add_argument("--ndk", type=Path, required=True)
    a = p.parse_args()
    blob = a.library.read_bytes()
    assert hashlib.sha256(blob).hexdigest() == PIN
    out = HERE.parents[1] / "dist/systemui-optimization"
    out.mkdir(parents=True, exist_ok=True)
    tools = a.ndk / "toolchains/llvm/prebuilt/windows-x86_64/bin"
    subprocess.run([str(tools / "clang.exe"), "--target=aarch64-linux-android34", "-c",
                    str(HERE / "native/schema_fastpath.S"), "-o", str(out / "schema.o")], check=True)
    subprocess.run([str(tools / "ld.lld.exe"), "-m", "aarch64elf", "-Ttext=0x15b8f10",
                    "--defsym=original_strncmp=0x159a320", "--defsym=original_parse_int=0x159a330",
                    "--entry=schema_key_fastpath", str(out / "schema.o"), "-o", str(out / "schema.elf")], check=True)
    subprocess.run([str(tools / "llvm-objcopy.exe"), "-O", "binary", "--only-section=.text",
                    str(out / "schema.elf"), str(out / "schema.bin")], check=True)
    code = (out / "schema.bin").read_bytes()
    assert len(code) == 140 and blob[0x15B8F10:0x15B8F10+len(code)] == bytes(len(code))
    words = struct.unpack("<35I", code)
    (HERE / "native/schema_code.h").write_text(
        "#include <array>\nstatic constexpr std::array<uint32_t,35> SCHEMA_CODE = {"
        + ",".join(f"0x{x:08x}" for x in words) + "};\n", encoding="ascii", newline="\n")
    expected = blob[0xA04598:0xA04968]
    (HERE / "native/schema_expected.h").write_text(
        "static constexpr unsigned char EXPECTED[] = {"
        + ",".join(f"0x{x:02x}" for x in expected) + "};\n", encoding="ascii", newline="\n")
    print("Generated pinned payload:", hashlib.sha256(code).hexdigest())


if __name__ == "__main__":
    main()
