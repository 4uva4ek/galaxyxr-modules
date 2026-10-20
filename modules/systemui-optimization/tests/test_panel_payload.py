"""Execute compiled ARM64 cache code, including visible/hidden and stale ownership."""
import json, struct, sys
from pathlib import Path
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm64_const import *
p=Path(sys.argv[1])
symbols=json.loads((p/"symbols.json").read_text())
u=Uc(UC_ARCH_ARM64,UC_MODE_ARM)
u.mem_map(0,0x10000); u.mem_write(0,(p/"payload.bin").read_bytes())
u.mem_map(0x200000,0x500000)
state,node,ctrl,stack,done,setter,getter=0x200000,0x300000,0x310000,0x500000,0x600000,0x610000,0x620000
def put(addr,n): u.mem_write(addr,struct.pack("<Q",n))
def word(addr): return struct.unpack("<Q",bytes(u.mem_read(addr,8)))[0]
put(state,setter); put(state+8,getter); put(node,123); put(node+8,ctrl)
u.reg_write(UC_ARM64_REG_TPIDR_EL0,0xabcdef)
calls=[]
def hook(m,pc,size,unused):
    if pc==setter:
        n=m.reg_read(UC_ARM64_REG_X0); value=m.reg_read(UC_ARM64_REG_W1)
        c=word(n+8); old=word(c+32)&1
        put(c+32,3 if value else 0); calls.append((old,value))
        m.reg_write(UC_ARM64_REG_PC,m.reg_read(UC_ARM64_REG_LR))
    elif pc==getter:
        m.reg_write(UC_ARM64_REG_X0,m.reg_read(UC_ARM64_REG_X0))
        m.reg_write(UC_ARM64_REG_PC,m.reg_read(UC_ARM64_REG_LR))
u.hook_add(UC_HOOK_CODE,hook)
def call(name,*args):
    for j,a in enumerate(args): u.reg_write(UC_ARM64_REG_X0+j,a)
    u.reg_write(UC_ARM64_REG_SP,stack); u.reg_write(UC_ARM64_REG_LR,done)
    u.emu_start(symbols[name],done,count=30000)
    assert u.reg_read(UC_ARM64_REG_PC)==done
    assert word(state+32)==0
def frame(): call("panel_frame",state)
def mirror(value): call("panel_mirror",node,value,state)
owner_proxy=node+0x1000
def owner(value):
    # NodeHandle returns a temporary proxy, not a stable Node address.
    put(owner_proxy,word(node)); put(owner_proxy+8,word(node+8))
    call("panel_owner",owner_proxy,value,state)
    if not value:
        old=word(ctrl+32)&1
        put(ctrl+32,0); calls.append((old,0))  # original owner's hidden branch
frame(); mirror(1); owner(0) # first sight uses stock; caller's hidden branch disables
put(ctrl+32,0); calls.clear()
for _ in range(10):
    frame(); mirror(1); assert not word(ctrl+32)&1
    owner(0); assert not word(ctrl+32)&1
assert all(old==new==0 for old,new in calls),calls
assert word(state+40)==10
frame(); mirror(1); owner(1); assert word(ctrl+32)&1  # notification appears
frame(); mirror(1); owner(0); assert not word(ctrl+32)&1  # notification hides
frame(); mirror(1); owner(1); assert word(ctrl+32)&1  # reappears
frame(); mirror(0); owner(1); assert not word(ctrl+32)&1  # no content remains hidden
frame(); frame(); mirror(1); assert word(ctrl+32)&1  # stale owner falls back
owner(0); put(ctrl+32,0); put(node,124)
frame(); mirror(1); assert word(ctrl+32)&1  # entity generation changed
owner(0); put(ctrl+32,0)
u.reg_write(UC_ARM64_REG_TPIDR_EL0,0xdead)
mirror(1); assert word(ctrl+32)&1  # other threads keep stock
print("PASS ARM64: hidden churn, appearance/hide/reappearance, content gating, ownership gap, entity reuse, other thread, balanced in-flight counter")

# Mid-function owner wrapper must preserve every caller register and SIMD value,
# even when the vendor callbacks clobber them. Only replayed W8 may differ.
v=Uc(UC_ARCH_ARM64,UC_MODE_ARM); v.mem_map(0,0x700000)
code=bytearray((p/"owner.bin").read_bytes())
base=0x10000; target=0x610000; finish=base+len(code)+64
struct.pack_into("<Q",code,symbols["owner_config"],state)
struct.pack_into("<Q",code,symbols["owner_target"],target)
struct.pack_into("<I",code,symbols["owner_return"],
                 0x14000000|((finish-base-symbols["owner_return"])//4 & 0x3ffffff))
v.mem_write(base,bytes(code)); v.reg_write(UC_ARM64_REG_SP,stack)
regs=[UC_ARM64_REG_X0+j for j in range(29)]+[UC_ARM64_REG_FP,UC_ARM64_REG_LR]
for j,r in enumerate(regs): v.reg_write(r,0x100+j)
v.reg_write(UC_ARM64_REG_X20,node)
v.mem_write(node+32,b"\x01")
for j in range(32): v.reg_write(UC_ARM64_REG_Q0+j,(j+1)*0x123456789abcdef0123456789)
v.reg_write(UC_ARM64_REG_NZCV,0xa0000000)
saved=[v.reg_read(r) for r in regs]
qs=[v.reg_read(UC_ARM64_REG_Q0+j) for j in range(32)]
def clobber(m,pc,size,unused):
    if pc!=target:return
    assert m.reg_read(UC_ARM64_REG_X0)==stack+0x70
    assert m.reg_read(UC_ARM64_REG_X1)==1
    assert m.reg_read(UC_ARM64_REG_X2)==state
    lr=m.reg_read(UC_ARM64_REG_LR)
    for j in range(19):m.reg_write(UC_ARM64_REG_X0+j,0xbad)
    for j in range(32):m.reg_write(UC_ARM64_REG_Q0+j,0xbad)
    m.reg_write(UC_ARM64_REG_NZCV,0)
    m.reg_write(UC_ARM64_REG_PC,lr)
v.hook_add(UC_HOOK_CODE,clobber); v.emu_start(base,finish,count=1000)
assert v.reg_read(UC_ARM64_REG_PC)==finish
for j,r in enumerate(regs): assert v.reg_read(r)==(1 if j==8 else saved[j]),j
assert [v.reg_read(UC_ARM64_REG_Q0+j) for j in range(32)]==qs
assert v.reg_read(UC_ARM64_REG_SP)==stack and v.reg_read(UC_ARM64_REG_NZCV)==0xa0000000
print("PASS ARM64: owner trampoline preserves GP/SIMD/NZCV/SP and replays original load")

# Hook after PACIASP: keep the signed return address and replay STR D8 exactly.
f=Uc(UC_ARCH_ARM64,UC_MODE_ARM); f.mem_map(0,0x700000)
frame_code=bytearray((p/"space.bin").read_bytes())
finish=base+len(frame_code)+64
struct.pack_into("<Q",frame_code,symbols["frame_config"],state)
struct.pack_into("<Q",frame_code,symbols["frame_target"],target)
struct.pack_into("<I",frame_code,symbols["frame_return"],
                 0x14000000|((finish-base-symbols["frame_return"])//4 & 0x3ffffff))
f.mem_write(base,bytes(frame_code))
f.reg_write(UC_ARM64_REG_SP,stack)
f.reg_write(UC_ARM64_REG_X0,node)
f.reg_write(UC_ARM64_REG_LR,0xabcd000000123456)
f.reg_write(UC_ARM64_REG_D8,0x123456789abcdef0)
def frame_clobber(m,pc,size,unused):
    if pc!=target:return
    assert m.reg_read(UC_ARM64_REG_X0)==state
    lr=m.reg_read(UC_ARM64_REG_LR)
    for j in range(19):m.reg_write(UC_ARM64_REG_X0+j,0xbad)
    m.reg_write(UC_ARM64_REG_PC,lr)
f.hook_add(UC_HOOK_CODE,frame_clobber)
f.emu_start(base,finish,count=1000)
assert f.reg_read(UC_ARM64_REG_X0)==node
assert f.reg_read(UC_ARM64_REG_LR)==0xabcd000000123456
assert f.reg_read(UC_ARM64_REG_SP)==stack-0x70
assert bytes(f.mem_read(stack-0x70,8))==struct.pack("<Q",0x123456789abcdef0)
print("PASS ARM64: frame hook preserves signed LR/this and replays original D8/SP prologue")
