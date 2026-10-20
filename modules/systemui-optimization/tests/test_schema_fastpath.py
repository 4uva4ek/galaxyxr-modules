"""Execute the assembled ARM64 payload, verify results and fallback ABI."""
import random
import struct
import sys
from pathlib import Path
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm64_const import *

CAVE = 0x15B8F10
uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
uc.mem_map(0x159A000, 0x1000)
uc.mem_map(0x15B8000, 0x1000)
uc.mem_map(0x2000000, 0x1000)
uc.mem_write(CAVE, Path(sys.argv[1]).read_bytes())
fallback = []


def hook(machine, address, size, unused):
    if address in (0x159A320, 0x159A330):
        fallback.append(address)
        machine.emu_stop()


uc.hook_add(UC_HOOK_CODE, hook)
INPUT, KEY, OUTPUT, RETURN = 0x2000100, 0x2000200, 0x2000300, 0x2000400
uc.mem_write(KEY, b"requires_api\0")


def init(data, second, third):
    fallback.clear()
    uc.mem_write(INPUT-4, struct.pack("<I", len(data)))
    uc.mem_write(INPUT, data+b"\0"*32)
    uc.mem_write(OUTPUT, b"\xCC"*4)
    for reg, value in ((UC_ARM64_REG_X0, INPUT), (UC_ARM64_REG_X1, second),
                       (UC_ARM64_REG_X2, third), (UC_ARM64_REG_X3, 10),
                       (UC_ARM64_REG_X8, 0x88776655), (UC_ARM64_REG_X19, 0x1920),
                       (UC_ARM64_REG_LR, RETURN)):
        uc.reg_write(reg, value)


rng = random.Random(24102026)
keys = [b"", b"a", b"requires_api", b"requires_api_extra", b"requires_api\0extra"]
for i in range(12):
    for value in (0, 1, 48, 65, 127, 255):
        key = bytearray(b"requires_api\0")
        key[i] = value
        keys.append(bytes(key))
for key in keys:
    init(key, KEY, 12)
    uc.emu_start(CAVE, RETURN, count=40)
    if len(key) < 12:
        assert fallback == [0x159A320]
        assert uc.reg_read(UC_ARM64_REG_X0) == INPUT
        assert uc.reg_read(UC_ARM64_REG_X1) == KEY
        assert uc.reg_read(UC_ARM64_REG_X2) == 12
    else:
        assert not fallback
        # Original strncmp(key, "requires_api", 12) equality semantics.
        expected = (key+b"\0")[:12] == b"requires_api"
        assert (uc.reg_read(UC_ARM64_REG_W0) == 0) == expected, (key, expected, uc.reg_read(UC_ARM64_REG_W0))
    assert uc.reg_read(UC_ARM64_REG_X8) == 0x88776655
    assert uc.reg_read(UC_ARM64_REG_X19) == 0x1920
numbers = [bytes([x]) for x in range(256)]
numbers += [f"{x:02d}".encode() for x in range(100)]
numbers += [bytes(rng.randrange(256) for _ in range(2)) for _ in range(256)]
numbers += [b"", b"-1", b"+1", b" 1", b"1 ", b"100", b"2147483648", b"\x001"]
for data in numbers:
    init(data, len(data), OUTPUT)
    uc.emu_start(CAVE+64, RETURN, count=40)
    fast = 1 <= len(data) <= 2 and all(48 <= x <= 57 for x in data)
    if fast:
        assert not fallback
        assert uc.reg_read(UC_ARM64_REG_W0) == 1
        assert struct.unpack("<I", uc.mem_read(OUTPUT, 4))[0] == int(data)
    else:
        assert fallback == [0x159A330]
        assert uc.reg_read(UC_ARM64_REG_X0) == INPUT
        assert uc.reg_read(UC_ARM64_REG_X1) == len(data)
        assert uc.reg_read(UC_ARM64_REG_X2) == OUTPUT
        assert uc.reg_read(UC_ARM64_REG_X3) == 10
        assert bytes(uc.mem_read(OUTPUT, 4)) == b"\xCC"*4
    assert uc.reg_read(UC_ARM64_REG_X8) == 0x88776655
    assert uc.reg_read(UC_ARM64_REG_X19) == 0x1920
    assert uc.reg_read(UC_ARM64_REG_LR) == RETURN
print(f"PASS: {len(keys)} key comparisons; {len(numbers)} numeric fast/fallback cases; ABI preserved")
