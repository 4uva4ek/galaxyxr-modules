"""Compile and exercise the exact validator used by both native modules."""
import argparse,ctypes as C,json,struct,subprocess
from pathlib import Path
HERE=Path(__file__).resolve().parent
OUT=HERE.parents[1]/'dist/openxr-velocity-frame'
def test(ndk):
 tc=ndk/'toolchains/llvm/prebuilt/windows-x86_64/bin'
 source=OUT/'test-compatibility.cpp'
 source.write_text('#include "compatibility.h"\nextern "C" __declspec(dllexport) int check(const unsigned char* f,const unsigned char* p){int s=gxr_consumer_state(f);return s<0||!gxr_payload_valid(p,s&1)?-1:s;}\n')
 subprocess.run([str(tc/'clang++.exe'),'--target=x86_64-pc-windows-msvc','-std=c++17','-O2','-ffreestanding','-fno-exceptions','-fno-rtti','-I',str(HERE/'native'),'-c',str(source),'-o',str(OUT/'compat.obj')],check=True)
 subprocess.run([str(tc/'ld.lld.exe'),'-flavor','link','/dll','/noentry','/nodefaultlib',f'/out:{OUT/"compat.dll"}',str(OUT/'compat.obj')],check=True)
 lib=C.CDLL(str(OUT/'compat.dll'));check=lib.check;check.argtypes=[C.c_char_p,C.c_char_p];check.restype=C.c_int
 # Parse generated constants, so a stale contract is caught against build output.
 import re
 header=(HERE/'native/compatibility.generated.h').read_text()
 def values(name):return [int(x) for x in re.search(name+r'\[\]=\{([^}]+)',header)[1].split(',')]
 original=bytes(values('GXR_CONSUMER'));sites=values('GXR_SITES');words=values('GXR_PATCHED')
 payload=(OUT/'payload.bin').read_bytes();assert payload==bytes(values('GXR_PAYLOAD'))
 enabled=json.loads((OUT/'plan.json').read_text())['enabled_offset']
 count=0
 for correction in (False,True):
  for extrapolation in (False,True):
   f=bytearray(original);p=bytearray(payload+bytes(0x520-len(payload)))
   if correction:
    for site,word in zip(sites,words):struct.pack_into('<I',f,site-0x1b359c,word)
    struct.pack_into('<I',p,enabled,1)
   if extrapolation:struct.pack_into('<I',f,0x1b3860-0x1b359c,0xd503201f)
   expected=int(correction)+2*int(extrapolation)
   assert check(bytes(f),bytes(p))==expected
   for data,other,first in ((f,p,True),(p,f,False)):
    for i in range(len(data)):
     bad=bytearray(data);bad[i]^=0x80
     assert check(bytes(bad) if first else bytes(other),bytes(other) if first else bytes(bad))==-1,(expected,i,first)
     count+=1
   # Every partial hook combination fails, independently of the gate.
   for mask in range(1,15):
    partial=bytearray(original)
    for i,(site,word) in enumerate(zip(sites,words)):
     if mask>>i&1:struct.pack_into('<I',partial,site-0x1b359c,word)
    assert check(bytes(partial),bytes(p))==-1
   # Each module's restoration leaves the other module's state intact.
   for site in sites:f[site-0x1b359c:site-0x1b359c+4]=original[site-0x1b359c:site-0x1b359c+4]
   struct.pack_into('<I',p,enabled,0)
   assert check(bytes(f),bytes(p))==2*int(extrapolation)
 result={'combined_states':4,'corrupted_byte_cases_rejected':count,'partial_hooks_rejected':True,'independent_restore_checked':True}
 (OUT/'compatibility-tests.json').write_text(json.dumps(result,indent=2)+'\n');print(result)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--ndk',type=Path,required=True);test(p.parse_args().ndk)
