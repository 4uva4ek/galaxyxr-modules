"""Build the standalone panel candidate payload and byte signatures."""
import argparse, hashlib, json, struct, subprocess
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument("--ndk",required=True,type=Path)
p.add_argument("--libraries",required=True,type=Path)
a=p.parse_args()
root=Path(__file__).resolve().parents[2]
src=Path(__file__).resolve().parent/"native"
out=root/"dist/systemui-optimization/panels"
out.mkdir(parents=True,exist_ok=True)
tc=a.ndk/"toolchains/llvm/prebuilt/windows-x86_64/bin"
def run(*args): subprocess.run([str(x) for x in args],check=True)
run(tc/"aarch64-linux-android34-clang.cmd","-Os","-fPIC","-ffreestanding","-fno-builtin",
    "-fno-stack-protector","-mgeneral-regs-only","-mno-outline-atomics","-mbranch-protection=bti",
    "-c",src/"panel_payload.c","-o",out/"payload.o")
run(tc/"ld.lld.exe","-Ttext=0","--entry=panel_frame",out/"payload.o","-o",out/"payload.elf")
run(tc/"llvm-objcopy.exe","-O","binary","--only-section=.text",out/"payload.elf",out/"payload.bin")
symbols={}
for line in subprocess.check_output([str(tc/"llvm-nm.exe"),"-n",str(out/"payload.elf")],text=True).splitlines():
    fields=line.split()
    if len(fields)==3: symbols[fields[2]]=int(fields[0],16)
space=""".text
.global frame_hook, mirror_hook, frame_config, frame_target, frame_return, mirror_config, mirror_target
frame_hook:
stp x0,x30,[sp,#-16]!
ldr x0,frame_config
ldr x16,frame_target
blr x16
ldp x0,x30,[sp],#16
str d8,[sp,#-0x70]!
frame_return: b .
.balign 8
frame_config: .quad 0
frame_target: .quad 0
.balign 16
mirror_hook:
ldr x2,mirror_config
ldr x16,mirror_target
br x16
.balign 8
mirror_config: .quad 0
mirror_target: .quad 0
"""
owner=[".text",".global owner_hook, owner_config, owner_target, owner_return",
       "owner_hook: sub sp,sp,#0x2b0"]
for i in range(0,20,2):
    other=30 if i==18 else i+1
    owner.append(f"stp x{i},x{other},[sp,#{i*8}]")
owner += ["mrs x16,nzcv","str x16,[sp,#0xa0]"]
for i in range(0,32,2): owner.append(f"stp q{i},q{i+1},[sp,#{0xb0+i*16}]")
owner += ["add x0,sp,#0x320","ldrb w1,[x20,#0x20]","ldr x2,owner_config",
          "ldr x16,owner_target","blr x16"]
for i in range(0,32,2): owner.append(f"ldp q{i},q{i+1},[sp,#{0xb0+i*16}]")
owner += ["ldr x16,[sp,#0xa0]","msr nzcv,x16"]
for i in range(0,20,2):
    other=30 if i==18 else i+1
    owner.append(f"ldp x{i},x{other},[sp,#{i*8}]")
owner += ["add sp,sp,#0x2b0","ldrb w8,[x20,#0x20]","owner_return: b .",
          ".balign 8","owner_config: .quad 0","owner_target: .quad 0"]
blobs={"PAYLOAD":(out/"payload.bin").read_bytes()}
for name,assembly in [("space",space),("owner","\n".join(owner))]:
    (out/f"{name}.S").write_text(assembly+"\n")
    run(tc/"aarch64-linux-android34-clang.cmd","-c",out/f"{name}.S","-o",out/f"{name}.o")
    run(tc/"ld.lld.exe","-Ttext=0",f"--entry={name if name=='owner' else 'frame'}_hook",
        out/f"{name}.o","-o",out/f"{name}.elf")
    run(tc/"llvm-objcopy.exe","-O","binary","--only-section=.text",out/f"{name}.elf",out/f"{name}.bin")
    blobs[name.upper()]=(out/f"{name}.bin").read_bytes()
    for line in subprocess.check_output([str(tc/"llvm-nm.exe"),"-n",str(out/f"{name}.elf")],text=True).splitlines():
        fields=line.split()
        if len(fields)==3: symbols[fields[2]]=int(fields[0],16)
for name,sha,sites in [
    ("libspaceflinger_jni_vulkan.so","7c72d2bd2284223877d5d5701fb69206fd2a202fa4867650b606c9245ff36fc5",[("FRAME_EXPECTED",0x4d3ee04),("MIRROR_EXPECTED",0x4d15490)]),
    ("libsysui_jni_soong_vulkan.so","d2060072c6046354d470a5e4949f762160a7d89f20310a8652e57c67805c0863",[("OWNER_EXPECTED",0x4ce92c8)])]:
    lib=(a.libraries/name).read_bytes()
    assert hashlib.sha256(lib).hexdigest()==sha
    for label,site in sites: blobs[label]=lib[site:site+32]
assert len(blobs["PAYLOAD"])+256<0xac8
assert len(blobs["OWNER"])<0x5c8
header=""
for name,blob in blobs.items():
    header+=f"static constexpr unsigned char {name}[]={{"+",".join(f"0x{b:02x}" for b in blob)+"};\n"
for name,value in symbols.items():
    if name.startswith(("panel_","frame_","mirror_","owner_")):
        header+=f"static constexpr size_t OFF_{name}=0x{value:x};\n"
(src/"panel_code.h").write_text(header,encoding="utf-8",newline="\n")
(out/"symbols.json").write_text(json.dumps(symbols,indent=2))
print("Payload/wrappers:",{k:len(v) for k,v in blobs.items()})
