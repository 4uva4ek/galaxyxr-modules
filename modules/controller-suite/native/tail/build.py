"""Local-only ARM64 build and explicit relocation; no adb or device writes."""
from pathlib import Path
import hashlib,json,struct,subprocess,argparse
H=Path(__file__).resolve().parent
TC=Path('C:/Users/alex/AppData/Local/Android/Sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/windows-x86_64/bin')
LIB=Path('E:/GalaxyXR/src/xrvst2-dump-20261011/vendor/lib64/libsctrlclient.so')
SHA='36120385f25c61f5c2d832d2ac2ebf2179dea1ba8142beeb1001994b7f0d22c0'
SITE=0x72c5c

def branch(src,dst):
    delta=dst-src
    if delta%4 or not -(1<<27)<=delta<(1<<27):
        raise ValueError('B target unaligned or outside signed 128 MiB reach')
    return struct.pack('<I',0x14000000|((delta//4)&0x3ffffff))

def relocate(payload,ret,base,cave):
    if base%0x1000 or cave%0x4000: raise ValueError('base must be page aligned; cave must be 16 KiB aligned')
    if not base or not cave: raise ValueError('zero address')
    if cave < base+0x100000 and cave+0x8000>base: raise ValueError('cave overlaps pinned library extent')
    code=bytearray(payload)
    code[ret:ret+4]=branch(cave+ret,base+SITE+4)
    return bytes(code),branch(base+SITE,cave)

def build():
    assert hashlib.sha256(LIB.read_bytes()).hexdigest()==SHA
    subprocess.run([str(TC/'clang.exe'),'-cc1as','-triple','aarch64-linux-android29','-filetype','obj',str(H/'tail.S'),'-o',str(H/'tail.o')],check=True)
    # Link internal ADR relocations at zero; relative offsets remain position independent.
    subprocess.run([str(TC/'ld.lld.exe'),'-Ttext=0','--entry=tail_entry',str(H/'tail.o'),'-o',str(H/'tail.elf')],check=True)
    subprocess.run([str(TC/'llvm-objcopy.exe'),'-O','binary','--only-section=.text',str(H/'tail.elf'),str(H/'tail-template.bin')],check=True)
    syms=subprocess.check_output([str(TC/'llvm-nm.exe'),str(H/'tail.elf')],text=True)
    labels={line.split()[2]:int(line.split()[0],16) for line in syms.splitlines() if len(line.split())==3}
    (H/'tail.asm').write_text(subprocess.check_output([str(TC/'llvm-objdump.exe'),'-d',str(H/'tail.elf')],text=True),encoding='utf-8')
    b=(H/'tail-template.bin').read_bytes()
    assert len(b)==0x4800 and labels['tail_entry']==0 and labels['tail_control']==0x4000
    meta=dict(status='LOCAL_TEMPLATE_ONLY_NOT_INSTALLABLE_UNTIL_RELOCATED',author='4uva4ek',library=str(LIB),library_sha256=SHA,
      hook_va=SITE,original_word='295d1f1f',return_offset=labels['tail_return'],code_page_bytes=0x4000,allocation_bytes=0x8000,control_offset=0x4000,
      template_sha256=hashlib.sha256(b).hexdigest(),mode_default=0,gain_default=1.,bounds=dict(dt_min_s=.002,dt_max_s=.1,h_max_s=.035,accel_max_mps2=40,delta_max_mps=.5),
      telemetry=dict(lock_offset=16,calls_offset=32,observe_offset=40,applied_offset=48,rejected_offset=56,h_hist=[128,37],dt_hist=[448,102],accel_hist=[1280,42],
       limitation='Best effort try-lock; positive-tail calls only. Contended calls are not counted. Last bin means overflow/invalid.'),
      provenance='Original project algorithm preserved; standalone experimental addition by 4uva4ek. Original library remains untouched.')
    (H/'manifest.json').write_text(json.dumps(meta,indent=2)+'\n')
    return meta

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--base',type=lambda x:int(x,0));p.add_argument('--cave',type=lambda x:int(x,0));a=p.parse_args()
    m=build()
    if a.base is not None or a.cave is not None:
        if a.base is None or a.cave is None: p.error('both --base and --cave required')
        b,entry=relocate((H/'tail-template.bin').read_bytes(),m['return_offset'],a.base,a.cave)
        out=H/f'relocated-{a.base:x}-{a.cave:x}';out.mkdir(exist_ok=False)
        (out/'allocation.bin').write_bytes(b);(out/'entry-word.bin').write_bytes(entry)
        (out/'addresses.json').write_text(json.dumps(dict(base=a.base,cave=a.cave,site=a.base+SITE,entry_hex=entry.hex(),allocation_sha256=hashlib.sha256(b).hexdigest()),indent=2)+'\n')
    print(json.dumps(m,indent=2))
