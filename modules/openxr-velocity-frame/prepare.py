"""Offline build of manual app-private runtime prototype; no device operations."""
import argparse,hashlib,json,struct,subprocess,platform,os
from pathlib import Path
HERE=Path(__file__).resolve().parent
def call(site,target):
 delta=target-site
 if delta%4 or not -(1<<27)<=delta<(1<<27):raise ValueError('Invalid BL range/alignment')
 return struct.pack('<I',0x94000000|((delta//4)&0x3ffffff))
def loads(blob):
 assert blob[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',blob,18)[0]==183
 off=struct.unpack_from('<Q',blob,32)[0];size,count=struct.unpack_from('<HH',blob,54);assert size==56
 return [h for i in range(count) if (h:=struct.unpack_from('<IIQQQQQQ',blob,off+i*size))[0]==1]

def build(ndk,runtime):
 assert 'Pkg.Revision = 28.2.13676358' in (ndk/'source.properties').read_text()
 host={'Windows':'windows-x86_64','Linux':'linux-x86_64','Darwin':'darwin-x86_64'}[platform.system()]
 tc=ndk/'toolchains/llvm/prebuilt'/host;bin=tc/'bin';ext='.exe' if os.name=='nt' else ''
 def tool(name):return str(bin/(name+ext))
 out=HERE.parents[1]/'dist/openxr-velocity-frame';out.mkdir(parents=True,exist_ok=True)
 blob=runtime.read_bytes()
 sha=hashlib.sha256(blob).hexdigest();assert sha=='a7dabaa1030cc7979efe987f212488c61b61e8afb9aa3add59f7ae4fbeff4c92'
 cave,end,site,target=0x272ae0,0x273000,0x1d9e18,0x26ee30
 rx=[s for s in loads(blob) if s[1]==5];assert len(rx)==1 and rx[0][2:4]==(0xfd000,0xfd000) and rx[0][5:7]==(0x175ad8,0x175ad8)
 assert all(not (s[3]<end and s[3]+s[6]>cave) for s in loads(blob) if s[1]!=5)
 assert blob[site:site+4]==call(site,target) and blob[cave:end]==bytes(end-cave)
 asm=''' .section .entry,"ax",%progbits
.global gxr_entry
gxr_entry:
 stp x19,x30,[sp,#-48]!
 stp x1,x2,[sp,#16]
 str x4,[sp,#32]
 bl original_call
 tbz w0,#0,1f
 mov w19,w0
 ldp x1,x2,[sp,#16]
 ldr x0,[sp,#32]
 bl gxr_correct
 mov w0,w19
1: ldp x19,x30,[sp],#48
 ret
.section .config,"a",%progbits
.balign 8
.global gxr_enabled
.hidden gxr_enabled
gxr_enabled: .word 0
.word 0
'''
 (out/'entry.S').write_text(asm)
 (out/'payload.ld').write_text(f'ENTRY(gxr_entry)\nSECTIONS {{ . = {cave}; .payload : {{ KEEP(*(.entry)) *(.text*) *(.rodata*) KEEP(*(.config)) }} /DISCARD/ : {{ *(.comment) *(.note*) *(.eh_frame*) }} }}\noriginal_call = {target};\n')
 driver=[tool('clang++'),'-ccc-install-dir',str(bin),'--target=aarch64-linux-android29',f'--sysroot={tc/"sysroot"}','-resource-dir',str(tc/'lib/clang/19')]
 subprocess.run(driver+['-std=c++17','-Oz','-Wall','-Wextra','-Werror','-ffreestanding','-fno-exceptions','-fno-rtti','-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-fno-pic','-ffp-contract=off','-c',str(HERE/'native/correction.cpp'),'-o',str(out/'correction.o')],check=True)
 subprocess.run([tool('clang++'),'-cc1as','-triple','aarch64-linux-android29','-filetype','obj',str(out/'entry.S'),'-o',str(out/'entry.o')],check=True)
 subprocess.run([tool('ld.lld'),'--no-undefined','--build-id=none','-T',str(out/'payload.ld'),str(out/'entry.o'),str(out/'correction.o'),'-o',str(out/'payload.elf')],check=True)
 report=subprocess.check_output([tool('llvm-readelf'),'-r','-s',str(out/'payload.elf')],text=True)
 assert 'There are no relocations in this file.' in report
 enabled=int(next(l for l in report.splitlines() if l.rstrip().endswith(' gxr_enabled')).split()[1],16)-cave
 subprocess.run([tool('llvm-objcopy'),'-O','binary','--only-section=.payload',str(out/'payload.elf'),str(out/'payload.bin')],check=True)
 payload=(out/'payload.bin').read_bytes();assert len(payload)<=end-cave,(len(payload),end-cave)
 assert enabled%8==0 and payload[enabled:enabled+4]==bytes(4)
 def arr(v):return '{'+','.join(str(x) for x in v)+'}'
 fields=['"runtime-a7dabaa1"','"third-party-main-apps"',json.dumps(sha),str(0x1d9d98),str(0x10c),str(cave),str(end),str(enabled),arr([site]),arr([int.from_bytes(call(site,target),'little')]),arr([int.from_bytes(call(site,cave),'little')]),arr(payload)]
 (out/'profiles.generated.h').write_text('static const std::vector<Profile> PROFILES={{'+','.join(fields)+'}};\n')
 subprocess.run(driver+['-std=c++17','-O2','-Wall','-Wextra','-Werror','-I',str(out),'-c',str(HERE/'native/main.cpp'),'-o',str(out/'main.o')],check=True)
 subprocess.run(driver+[f'--ld-path={tool("ld.lld")}','-static-libstdc++','-Wl,-z,max-page-size=16384',str(out/'main.o'),'-o',str(out/'gxr-openxr-velocity')],check=True)
 plan={'status':'OFFLINE_CANDIDATE','runtime_sha256':sha,'payload_bytes':len(payload),'capacity':end-cave,'enabled_offset':enabled,'hook':hex(site),'cave':hex(cave),'rollback':'off restores call, retains inert payload for in-flight returns; app exit clears private mapping; no autostart','scope':'Third-party main app processes using pinned runtime; package/UID independently verified; exact firmware hash required'}
 (out/'plan.json').write_text(json.dumps(plan,indent=2)+'\n');print(json.dumps(plan,indent=2))
if __name__=='__main__':
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--ndk',type=Path,required=True);parser.add_argument('--runtime',type=Path,required=True)
 args=parser.parse_args();build(args.ndk.resolve(),args.runtime.resolve())
