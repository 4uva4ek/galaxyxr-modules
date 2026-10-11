"""Run actual payload C++ on host against independent quaternion algebra."""
import ctypes as C, math, random, subprocess, sys, json
from pathlib import Path
HERE=Path(__file__).resolve().parent
from geometry_oracle import descriptor,pitch,mul,rotate
class V(C.Structure): _fields_=[(x,C.c_float) for x in 'xyz']
class Q(C.Structure): _fields_=[(x,C.c_float) for x in 'xyzw']
class R(C.Structure): _fields_=[('flags',C.c_uint32),('q',Q),('p',V),('v',V),('w',V)]
def xyz(v):return v.x,v.y,v.z
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def verify(ndk):
 out=HERE.parents[1]/'dist/openxr-velocity-frame';tc=ndk/'toolchains/llvm/prebuilt/windows-x86_64/bin'
 subprocess.run([str(tc/'clang++.exe'),'--target=x86_64-pc-windows-msvc','-std=c++17','-Oz','-ffreestanding','-fno-exceptions','-fno-rtti','-ffp-contract=off','-DGXR_HOST_TEST','-c',str(HERE/'native/correction.cpp'),'-o',str(out/'test.obj')],check=True)
 subprocess.run([str(tc/'ld.lld.exe'),'-flavor','link','/dll','/noentry','/nodefaultlib',f'/out:{out/"test.dll"}',str(out/'test.obj')],check=True)
 core=C.CDLL(str(out/'test.dll'));fn=core.gxr_correct;fn.argtypes=[C.POINTER(R),C.c_uint32,C.c_uint32,C.c_void_p];fn.restype=None
 enabled=C.c_uint32.in_dll(core,'gxr_enabled');rng=random.Random(20261011);n=0;err=0
 for dev in (2,3):
  for name in range(0x130005,0x180006,0x10000):
   a,p=descriptor(name,dev)
   for _ in range(500):
    qs=[rng.gauss(0,1) for i in range(4)];norm=math.sqrt(sum(x*x for x in qs));qs=tuple(x/norm for x in qs)
    r=R(119,Q(*mul(qs,pitch(a))),V(1,2,3),V(*(rng.uniform(-10,10) for i in range(3))),V(*(rng.uniform(-20,20) for i in range(3))))
    prefix=bytes(r)[:32];w=xyz(r.w);v=xyz(r.v)
    want=rotate(qs,tuple(x+y for x,y in zip(v,cross(w,p))))+rotate(qs,w)
    fn(C.byref(r),dev,name,C.byref(V()))
    assert bytes(r)[:32]==prefix
    err=max(err,max(abs(x-y) for x,y in zip(xyz(r.v)+xyz(r.w),want)));assert err<3e-5,err;n+=1
 for dev,name,flags in ((1,0x140005,119),(11,0x140005,119),(4,0x140005,119),(2,0x190005,119),(2,0x140006,119),(2,0x140005,118)):
  r=R(flags,Q(0,0,0,1),V(),V(1,2,3),V(4,5,6));before=bytes(r);fn(C.byref(r),dev,name,C.byref(V()));assert bytes(r)==before
 for q in (Q(0,0,0,2),Q(math.nan,0,0,1),Q(math.inf,0,0,1)):
  r=R(119,q,V(),V(1,2,3),V(4,5,6));before=bytes(r);fn(C.byref(r),2,0x140005,C.byref(V()));assert bytes(r)==before
 for name in range(0x130005,0x180006,0x10000):
  r=R(0x23,Q(0,0,0,1),V(),V(1,2,3),V(4,5,6));prefix=bytes(r)[4:32];fn(C.byref(r),2,name,C.byref(V()))
  assert bool(r.flags&0x20)==(name==0x140005) and bytes(r)[4:32]==prefix and xyz(r.w)==(4,5,6)
  r=R(119,Q(0,0,0,1),V(),V(math.nan,2,3),V(math.nan,5,6));before=bytes(r);fn(C.byref(r),2,name,C.byref(V()))
  assert bytes(r)[4:]==before[4:]
 enabled.value=0;r=R(119,Q(0,0,0,1),V(),V(1,2,3),V(4,5,6));before=bytes(r);fn(C.byref(r),2,0x140005,C.byref(V()));assert bytes(r)==before;fn(None,2,0x140005,None)
 # Independent rigid-body identity: R(v + omega cross p) == R(v) + R(omega) cross R(p).
 for _ in range(100):
  q=pitch(rng.uniform(-3,3));v=(1,2,3);w=(4,5,6);p=(.01,-.04,.03)
  lhs=rotate(q,tuple(x+y for x,y in zip(v,cross(w,p))));rhs=tuple(x+y for x,y in zip(rotate(q,v),cross(rotate(q,w),rotate(q,p))))
  assert max(abs(x-y) for x,y in zip(lhs,rhs))<1e-12
 result={'random_actual_cpp_cases':n,'max_vector_component_error':err,'guards':'disabled, null, wrong device/input, invalid orientation/quaternion, nonfinite vectors, offset without omega','pose_unchanged':True,'origin_shift_identity_cases':100}
 (out/'tests.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':
 import argparse
 p=argparse.ArgumentParser();p.add_argument('--ndk',type=Path,required=True);verify(p.parse_args().ndk)
