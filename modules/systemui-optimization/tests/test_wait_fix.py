"""Execute actual outer+inner acquire ARM64 code against a swapchain state model."""
import hashlib
import struct
import sys
from pathlib import Path
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm64_const import *

blob = Path(sys.argv[1]).read_bytes()
assert hashlib.sha256(blob).hexdigest() == "ab04d6daf57800a42170febaf33cb128441339c06bd6d46f068c0109693fee42"


def run(patched, mode, begin_ok=True):
    u = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    u.mem_map(0, 0x1800000)
    u.mem_write(0, blob[:0x15B9000])
    if patched:
        for address in (0x104B044, 0x104B058):
            u.mem_write(address, struct.pack("<I", 0xD503201F))
    u.mem_map(0x2000000, 0x10000)
    obj, layers, sync, stack, done = 0x2000000, 0x2001000, 0x2002000, 0x200F000, 0x2003000
    def put(addr, value):
        u.mem_write(addr, struct.pack("<Q", value))
    put(obj+0x30, 0x1234)  # handler.host
    put(obj+0x38, layers)  # handler.layers
    put(layers, 0xC010)
    put(layers+0x70, layers)
    put(layers+0x78, 0 if mode == "none" else 0xD000)
    put(layers+0xB0, 0xD001)
    put(layers+0xE8, layers+(0xB0 if mode == "foveated" else 0x78))
    u.reg_write(UC_ARM64_REG_X1, obj)
    u.reg_write(UC_ARM64_REG_X2, sync)
    u.reg_write(UC_ARM64_REG_SP, stack)
    u.reg_write(UC_ARM64_REG_LR, done)
    states, calls, errors = {}, [], []
    def hook(m, address, size, unused):
        if address == 0x15A7D80:  # inner acquire's PLT entry
            m.reg_write(UC_ARM64_REG_PC, 0x104B87C)
        elif address == 0x15A7AB0:  # BeginFrame sret
            put(m.reg_read(UC_ARM64_REG_X8), 1 if begin_ok else 3)
            m.reg_write(UC_ARM64_REG_PC, m.reg_read(UC_ARM64_REG_LR))
        elif address in (0x15A7DF0, 0x15A7D90):
            handle = m.reg_read(UC_ARM64_REG_X0)
            operation = "acquire" if address == 0x15A7DF0 else "wait"
            calls.append((operation, handle))
            result = 0
            if operation == "acquire":
                assert handle not in states
                states[handle] = "acquired"
                m.mem_write(m.reg_read(UC_ARM64_REG_X2), struct.pack("<I", 2))
            elif states.get(handle) != "acquired":
                errors.append(handle)
                result = 0xFFFFFFDB  # XR_ERROR_CALL_ORDER_INVALID
            else:
                states[handle] = "waited"
            m.reg_write(UC_ARM64_REG_W0, result)
            m.reg_write(UC_ARM64_REG_PC, m.reg_read(UC_ARM64_REG_LR))
    u.hook_add(UC_HOOK_CODE, hook)
    u.emu_start(0x104AFE8, done, count=300)
    assert u.reg_read(UC_ARM64_REG_PC) == done
    assert u.reg_read(UC_ARM64_REG_W0) == (0 if begin_ok else 5)
    if begin_ok:
        assert bytes(u.mem_read(sync, 4)) == struct.pack("<I", 2)
    return calls, errors


for mode in ("none", "normal", "foveated"):
    old, errors = run(False, mode)
    new, fixed = run(True, mode)
    assert errors and not fixed, (mode, old, new, errors, fixed)
    handles = [0xC010] + ([] if mode == "none" else [0xD001 if mode == "foveated" else 0xD000])
    assert new == [item for h in handles for item in (("acquire", h), ("wait", h))]
    assert run(True, mode, False) == run(False, mode, False) == ([], [])
    print("PASS:", mode, "primary acquire/wait preserved; duplicates removed; BeginFrame failure unchanged")
