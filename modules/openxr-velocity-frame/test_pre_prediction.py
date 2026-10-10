"""Execute the actual pinned ARM64 copy/prediction path plus compiled patch.

Requires unicorn 2.1.4. No device operations. The runtime math implementation
executes too; only the sincosf PLT boundary is provided by the host.
"""
import argparse, json, math, random, struct
from pathlib import Path
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm64_const import *
from geometry_oracle import descriptor, mul, pitch, rotate

HERE=Path(__file__).resolve().parent
OUT=HERE.parents[1]/'dist/openxr-velocity-frame'
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def branch(site,target):return 0x94000000|((target-site)//4&0x3ffffff)

def run(runtime):
 blob=runtime.read_bytes();payload=(OUT/'payload.bin').read_bytes()
 plan=json.loads((OUT/'plan.json').read_text());uc=Uc(UC_ARCH_ARM64,UC_MODE_ARM)
 uc.mem_map(0,0x300000);uc.mem_write(0,blob)
 uc.mem_map(0x400000,0x100000)
 uc.reg_write(UC_ARM64_REG_CPACR_EL1,3<<20)
 def boundary(u,address,size,data):
  if address==0x26ee60:u.reg_write(UC_ARM64_REG_PC,0x1e67bc)
  elif address==0x26e170:
   a=struct.unpack('<f',struct.pack('<I',u.reg_read(UC_ARM64_REG_S0)))[0]
   u.mem_write(u.reg_read(UC_ARM64_REG_X0),struct.pack('<f',math.sin(a)))
   u.mem_write(u.reg_read(UC_ARM64_REG_X1),struct.pack('<f',math.cos(a)))
   u.reg_write(UC_ARM64_REG_PC,u.reg_read(UC_ARM64_REG_LR))
 uc.hook_add(UC_HOOK_CODE,boundary)
 rng=random.Random(20261011);cases=0;maxerr=0
 sites=[0x1b385c,0x1b38b4,0x1b38b8,0x1b38e4]
 words=[branch(sites[0],0x272ae0),0x910082a1,0x9100c3e2,branch(sites[3],0x272b20)]
 for correction in (False,True):
  for force in (False,True):
   for stock_flag in (0,1):
    uc.mem_write(0x1b359c,blob[0x1b359c:0x1b3a58])
    uc.mem_write(0x272ae0,payload)
    uc.mem_write(0x272ae0+plan['enabled_offset'],struct.pack('<I',int(correction)))
    if correction:
     for a,w in zip(sites,words):uc.mem_write(a,struct.pack('<I',w))
    if force:uc.mem_write(0x1b3860,struct.pack('<I',0xd503201f))
    # Invalidate translated blocks after changing code between combinations.
    uc.ctl_remove_cache(0,0x300000)
    for dev in (2,3,4):
     for name in range(0x130005,0x180006,0x10000):
      for sample in range(12):
       angle,offset=descriptor(name,dev if dev in (2,3) else 2)
       q=[rng.gauss(0,1) for _ in range(4)];norm=math.sqrt(sum(x*x for x in q));q=tuple(x/norm for x in q)
       input_q=mul(q,pitch(angle));pos=(1.,2.,3.)
       v=tuple(rng.uniform(-3,3) for _ in range(3));w=tuple(rng.uniform(-5,5) for _ in range(3))
       relation=struct.pack('<I13f',119,*input_q,*pos,*v,*w)
       # Re-read float inputs for a reference unaffected by packing precision.
       values=struct.unpack('<I13f',relation);input_q=values[1:5];pos=values[5:8];v=values[8:11];w=values[11:14]
       q=mul(input_q,pitch(-angle))
       cv,cw=v,w
       if correction and dev in (2,3):
        cv=rotate(q,tuple(a+b for a,b in zip(v,cross(w,offset))));cw=rotate(q,w)
       dt=0 if sample==0 else rng.uniform(.0001,.03)
       stamp=1000000000;target=stamp+round(dt*1e9);dt=(target-stamp)/1e9
       expected_q=input_q;expected_p=pos
       if (force or stock_flag) and dt:
        magnitude=math.sqrt(sum(x*x for x in cw));half=magnitude*dt/2
        delta=tuple(x/magnitude*math.sin(half) for x in cw)+(math.cos(half),)
        expected_q=mul(delta,input_q) if correction and dev in (2,3) else mul(input_q,delta)
        expected_p=tuple(a+b*dt for a,b in zip(pos,cv))
       sp=0x480000;output=0x410000;slots=0x420000;accessor=0x430000
       uc.mem_write(sp,bytes(0x100));uc.mem_write(sp+12,struct.pack('<I',dev));uc.mem_write(sp+16,struct.pack('<Q',accessor))
       uc.mem_write(accessor,bytes(32));uc.mem_write(accessor+16,bytes([stock_flag]))
       slot=struct.pack('<II',1,name)+relation+struct.pack('<QQ',stamp,123)
       uc.mem_write(slots,slot);uc.mem_write(output,bytes(56))
       for register,value in [(UC_ARM64_REG_SP,sp),(UC_ARM64_REG_X8,0),(UC_ARM64_REG_X19,name),(UC_ARM64_REG_X21,output),(UC_ARM64_REG_X24,slots),(UC_ARM64_REG_X28,target)]:uc.reg_write(register,value)
       uc.emu_start(0x1b382c,0x1b38e8,count=10000)
       assert uc.reg_read(UC_ARM64_REG_PC)==0x1b38e8
       actual=struct.unpack('<I13f',uc.mem_read(output,56))
       want=expected_q+expected_p+cv+cw
       error=max(abs(a-b) for a,b in zip(actual[1:],want));maxerr=max(maxerr,error)
       assert error<2e-5,(correction,force,stock_flag,dev,name,error,actual,want)
       assert actual[0]==119 and uc.reg_read(UC_ARM64_REG_X22)==1
       assert bytes(uc.mem_read(slots,80))==slot,'shared producer sample changed'
       # Interpolation/common exit and unsuccessful lookup: correct exactly once.
       if correction:
        for success in (0,1):
         uc.mem_write(output,relation);uc.mem_write(sp+12,struct.pack('<I',dev))
         uc.reg_write(UC_ARM64_REG_SP,sp);uc.reg_write(UC_ARM64_REG_X22,success)
         uc.emu_start(0x1b38e4,0x1b38e8,count=3000)
         a=struct.unpack('<I13f',uc.mem_read(output,56))
         want=input_q+pos+(cv+cw if success else v+w)
         assert max(abs(x-y) for x,y in zip(a[1:],want))<2e-5
         assert uc.reg_read(UC_ARM64_REG_X22)==success
       cases+=1
 result={'ARM64_runtime_prediction_cases':cases,'max_component_error':maxerr,'shared_buffer_unchanged':True,'interpolation_and_invalid_result_tested':True}
 (OUT/'preprediction-tests.json').write_text(json.dumps(result,indent=2)+'\n');print(result)

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--runtime',type=Path,required=True);run(p.parse_args().runtime)
