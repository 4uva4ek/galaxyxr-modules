"""Emulate the actual AArch64 release/submit block, including caller-saved clobbers.

python modules/systemui-optimization/tests/test_depth_patch.py PATH_TO_PINNED_LIBRARY
Requires the locally installed Unicorn emulator; no headset access.
"""
import hashlib
import struct
import sys
from pathlib import Path
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm64_const import (
    UC_ARM64_REG_PC, UC_ARM64_REG_SP, UC_ARM64_REG_X0, UC_ARM64_REG_X1,
    UC_ARM64_REG_X2, UC_ARM64_REG_X8, UC_ARM64_REG_X19, UC_ARM64_REG_LR,
)

PIN = "ab04d6daf57800a42170febaf33cb128441339c06bd6d46f068c0109693fee42"
CAVE, SITE1, SITE2 = 0x15B8F10, 0x104B9C4, 0x104B9D8


def branch(src, dst):
    return 0x14000000 | (((dst-src)//4) & 0x3FFFFFF)


def run(blob, selected, patched):
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    uc.mem_map(0, 0x1800000)
    uc.mem_write(0, blob[:0x15B9000])
    if patched:
        code = (0xF9407500, 0xF9400000, branch(CAVE+8, SITE1+4),
                0xF9407502, 0xF9400042, branch(CAVE+20, SITE2+4))
        uc.mem_write(CAVE, struct.pack("<6I", *code))
        uc.mem_write(SITE1, struct.pack("<I", branch(SITE1, CAVE)))
        uc.mem_write(SITE2, struct.pack("<I", branch(SITE2, CAVE+12)))
    uc.mem_map(0x2000000, 0x10000)
    obj, layers, color, stack = 0x2000000, 0x2001000, 0x2002000, 0x200F000
    def put(addr, value):
        uc.mem_write(addr, struct.pack("<Q", value))
    put(obj+8, 0x1234)
    put(obj+0x10, layers)
    put(layers+0x70, color)
    put(color, 0xC010)
    put(layers+0x78, 0xD000)
    put(layers+0xB0, 0xD001)
    offset = 0xB0 if selected == "foveated" else 0x78
    if selected == "none":
        put(layers+offset, 0)
    put(layers+0xE8, layers+offset)
    uc.reg_write(UC_ARM64_REG_X19, obj)
    uc.reg_write(UC_ARM64_REG_SP, stack)
    released = []
    def hook(machine, address, size, unused):
        if address == 0x15A7E00:
            released.append(machine.reg_read(UC_ARM64_REG_X0))
            assert machine.reg_read(UC_ARM64_REG_X1) == stack+0x120
            machine.reg_write(UC_ARM64_REG_X8, 0xDEADBEEF)
            machine.reg_write(UC_ARM64_REG_X0, 0)
            machine.reg_write(UC_ARM64_REG_PC, machine.reg_read(UC_ARM64_REG_LR))
    uc.hook_add(UC_HOOK_CODE, hook)
    uc.emu_start(0x104B9C0, 0x104B9F4, count=80)
    assert uc.reg_read(UC_ARM64_REG_PC) == 0x104B9F4
    assert uc.reg_read(UC_ARM64_REG_X0) == 0x1234
    assert uc.reg_read(UC_ARM64_REG_X1) == 0xC010
    return released, uc.reg_read(UC_ARM64_REG_X2)


if __name__ == "__main__":
    blob = Path(sys.argv[1]).read_bytes()
    assert hashlib.sha256(blob).hexdigest() == PIN
    assert blob[CAVE:CAVE+24] == bytes(24)
    for mode, expected in (("normal", 0xD000), ("foveated", 0xD001), ("none", 0)):
        result = run(blob, mode, True)
        assert result == ([expected] if expected else [], expected), (mode, result)
    # Reproduce the original mismatch and verify exact restoration behavior.
    assert run(blob, "foveated", False) == ([0xD000], 0xD000)
    assert run(blob, "normal", False) == run(blob, "normal", True)
    assert run(blob, "none", False) == run(blob, "none", True)
    print("PASS: selected depth release/submit, null depth, color preservation, ABI clobbers, stock reproduction")
