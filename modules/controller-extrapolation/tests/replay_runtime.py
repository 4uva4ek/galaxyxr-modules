"""Execute pinned consumePose and its math on retained controller records.

Offline only. OS/atomic/trace/libm import boundaries are supplied by the host;
selection, copy, velocity correction, pose extrapolation and returned time execute
as ARM64 instructions. No device commands, physical-error claim or timestamp fit.
"""
import argparse,hashlib,json,math,struct,sys
from pathlib import Path
import numpy as np
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm64_const import *
MODULE=Path(__file__).resolve().parents[1];REPO=MODULE.parents[1]
sys.path.insert(0,str(REPO/'modules/openxr-velocity-frame'))
from geometry_oracle import descriptor,mul,pitch,rotate
PIN='a7dabaa1030cc7979efe987f212488c61b61e8afb9aa3add59f7ae4fbeff4c92'
ENTRY=0x1b359c;END=0x1b3a58;CAVE=0x272ae0
SHARED=0x500000;SLOTS=SHARED+0x500;ACCESSOR=0x410000;TIME=0x410100;OUTPUT=0x410200;STACK=0x480000;TLS=0x490000;RETURN=0x400000
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def branch(a,b):return 0x94000000|(((b-a)//4)&0x3ffffff)
def stats(a):
 a=np.asarray(a);return {'n':len(a),'min':float(a.min()),'median':float(np.median(a)),'p95':float(np.percentile(a,95)),'max':float(a.max())} if len(a) else {'n':0}
class Runtime:
 def __init__(self,runtime,payload_dir):
  self.blob=runtime.read_bytes();assert hashlib.sha256(self.blob).hexdigest()==PIN
  self.payload=(payload_dir/'payload.bin').read_bytes();self.plan=json.loads((payload_dir/'plan.json').read_text());assert self.plan['runtime_sha256']==PIN
  self.u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);self.u.mem_map(0,0x300000);self.u.mem_write(0,self.blob);self.u.mem_map(0x400000,0x400000)
  self.u.reg_write(UC_ARM64_REG_CPACR_EL1,3<<20);self.u.reg_write(UC_ARM64_REG_TPIDR_EL0,TLS);self.u.hook_add(UC_HOOK_CODE,self.boundary)
  self.paths={};self.active_paths=set();self.now=0
 def boundary(self,u,a,size,data):
  if a in (0x1b3704,0x1b382c,0x1b38c0,0x1b38d0,0x1b3918,0x1b39ac):self.active_paths.add(hex(a))
  if a==0x268df0:
   at=u.reg_read(UC_ARM64_REG_X2);old=struct.unpack('<I',u.mem_read(at,4))[0]
   if old==u.reg_read(UC_ARM64_REG_W0):u.mem_write(at,struct.pack('<I',u.reg_read(UC_ARM64_REG_W1)))
   u.reg_write(UC_ARM64_REG_W0,old)
  elif a==0x26ee78:u.reg_write(UC_ARM64_REG_X0,self.now)
  elif a==0x26ee90:u.reg_write(UC_ARM64_REG_X0,0)
  elif a==0x2683a8:pass # Log-state atomic helper; no log sink in this harness.
  elif a==0x26eec0:u.reg_write(UC_ARM64_REG_X0,0)
  elif a==0x26ee60:u.reg_write(UC_ARM64_REG_PC,0x1e67bc);return
  elif a==0x26ee48:u.reg_write(UC_ARM64_REG_PC,0x1e64b8);return
  elif a==0x26e170:
   angle=struct.unpack('<f',struct.pack('<I',u.reg_read(UC_ARM64_REG_S0)))[0]
   u.mem_write(u.reg_read(UC_ARM64_REG_X0),struct.pack('<f',math.sin(angle)));u.mem_write(u.reg_read(UC_ARM64_REG_X1),struct.pack('<f',math.cos(angle)))
  else:return
  u.reg_write(UC_ARM64_REG_PC,u.reg_read(UC_ARM64_REG_LR))
 def configure(self,correction,force):
  u=self.u;u.mem_write(ENTRY,self.blob[ENTRY:END]);u.mem_write(CAVE,self.payload)
  u.mem_write(CAVE+self.plan['enabled_offset'],struct.pack('<I',int(correction)))
  if correction:
   for a,w in [(0x1b385c,branch(0x1b385c,CAVE)),(0x1b38b4,0x910082a1),(0x1b38b8,0x9100c3e2),(0x1b38e4,branch(0x1b38e4,CAVE+0x40))]:u.mem_write(a,struct.pack('<I',w))
  if force:u.mem_write(0x1b3860,struct.pack('<I',0xd503201f))
  u.ctl_remove_cache(0,0x300000)
 def call(self,dev,name,wanted,records,stock_flag=0):
  u=self.u;u.mem_write(SLOTS,bytes(16*80));u.mem_write(SHARED+0x1e39c0,bytes(4));u.mem_write(ACCESSOR,struct.pack('<QQB',0,SHARED,stock_flag)+bytes(15));self.active_paths=set()
  raw=b''.join(struct.pack('<II',1,name)+struct.pack('<I13f',flags,*values)+struct.pack('<QQ',stamp,stamp-1000000) for stamp,flags,values in records)
  u.mem_write(SLOTS,raw);u.mem_write(TIME,struct.pack('<Q',wanted));u.mem_write(OUTPUT,bytes([0xA5])*56);u.mem_write(STACK-0x1000,bytes(0x1000));self.now=wanted+10000
  sentinels={globals()[f'UC_ARM64_REG_X{i}']:0x12340000+i for i in range(19,30)}
  sentinels.update({globals()[f'UC_ARM64_REG_D{i}']:0x23450000+i for i in range(8,16)})
  for reg,val in sentinels.items():u.reg_write(reg,val)
  for reg,val in [(UC_ARM64_REG_SP,STACK),(UC_ARM64_REG_X0,ACCESSOR),(UC_ARM64_REG_X1,dev),(UC_ARM64_REG_X2,name),(UC_ARM64_REG_X3,TIME),(UC_ARM64_REG_X4,OUTPUT),(UC_ARM64_REG_LR,RETURN)]:u.reg_write(reg,val)
  u.emu_start(ENTRY,RETURN,count=10000);assert u.reg_read(UC_ARM64_REG_PC)==RETURN,hex(u.reg_read(UC_ARM64_REG_PC))
  assert u.reg_read(UC_ARM64_REG_SP)==STACK
  for reg,val in sentinels.items():assert u.reg_read(reg)==val,(reg,u.reg_read(reg),val)
  assert bytes(u.mem_read(SLOTS,len(raw)))==raw,'Producer records modified'
  return u.reg_read(UC_ARM64_REG_X0),struct.unpack('<Q',u.mem_read(TIME,8))[0],struct.unpack('<I13f',u.mem_read(OUTPUT,56)),sorted(self.active_paths)
def oracle(dev,name,values,dt,correction,extrapolate):
 q,p,v,w=values[:4],values[4:7],values[7:10],values[10:13];local=w
 if correction and dev in (2,3):
  angle,origin=descriptor(name,dev);basis=mul(q,pitch(-angle));v=rotate(basis,tuple(a+b for a,b in zip(v,cross(w,origin))));w=rotate(basis,w)
  # Native extrapolator expects local angular velocity, while API output is base-frame.
  local=rotate((-q[0],-q[1],-q[2],q[3]),w)
 if extrapolate:
  p=tuple(x+y*dt for x,y in zip(p,v));mag=math.sqrt(sum(x*x for x in local));half=mag*dt*.5
  delta=tuple(x*(math.sin(half)/mag if mag else dt*.5) for x in local)+(math.cos(half),);q=mul(q,delta)
 return (*q,*p,*v,*w)
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--runtime',type=Path,required=True);ap.add_argument('--capture',type=Path,required=True);ap.add_argument('--payload-dir',type=Path,default=REPO/'dist/openxr-velocity-frame');ap.add_argument('--output',type=Path,required=True);ap.add_argument('--limit',type=int,default=0);a=ap.parse_args()
 rt=Runtime(a.runtime,a.payload_dir);C=a.capture;raw=next(C.glob('locate-*.bin')).read_bytes();all_loc=list(struct.iter_unpack('<7Q2q2QiIII13fI',raw[64:]));manifest=json.loads((C/'manifest.json').read_text());start=float(manifest['device_start'].split()[0]);loc=[r for r in all_loc if start<=r[3]/1e9<=start+30]
 trials=[];repeats=[]
 for hand,dev in [('left',3),('right',2)]:
  z=np.load(C/(hand+'-native.npz'));n=z['a'];match=z['match'];ids=np.flatnonzero(z['full'])
  if a.limit:ids=ids[:a.limit]
  fresh={}
  for k in ids:
   r=loc[int(z['li'][k])];source=n[int(match[k])];q=source[6:10];p=source[3:6];v=source[10:13];w=source[13:16]
   values=struct.unpack('<13f',struct.pack('<13f',*q,*p,*v,*w));trial=(dev,0x150005,int(r[8]),round(source[1]*1e9),119,values);trials.append(trial);fresh[int(z['li'][k])]=trial
  active=None;signature=None
  for index in np.load(C/(hand+'-locate-matches.npz'))['locate_indices']:
   r=loc[int(index)]
   if index in fresh:active=fresh[index];signature=r[15:28];continue
   if active is not None and r[15:28]==signature:repeats.append((*active[:2],int(r[8]),*active[3:]))
   else:active=None
 result={'runtime_sha256':PIN,'payload_sha256':hashlib.sha256(rt.payload).hexdigest(),'capture':str(C),'trials':len(trials),'modes':{},'scope':'Full consumePose ARM64 execution on one reconstructed selected record from each matched fresh capture state. This does not reproduce real concurrent bank selection or rendered tracking.'}
 for correction,force in [(False,False),(True,False),(False,True),(True,True)]:
  rt.configure(correction,force);errors=[];times=[];paths={};negative=0
  for dev,name,wanted,stamp,flags,values in trials:
   dt=(wanted-stamp)/1e9;negative+=dt<0;ok,t,actual,path=rt.call(dev,name,wanted,[(stamp,flags,values)])
   assert ok==1 and actual[0]==flags;assert t==(wanted if force else stamp),(t,wanted,stamp)
   want=oracle(dev,name,values,dt,correction,force);err=max(abs(x-y) for x,y in zip(actual[1:],want));assert err<2e-5,(correction,force,err)
   errors.append(err);times.append((t-wanted)/1e6);key=','.join(path);paths[key]=paths.get(key,0)+1
  label=f'velocity_{int(correction)}_extrapolation_{int(force)}';result['modes'][label]={'cases':len(errors),'negative_dt_cases':negative,'max_component_error':max(errors),'returned_minus_requested_ms':stats(times),'executed_paths':paths};print(label,result['modes'][label],flush=True)
 rt.configure(True,True);repeat_errors=[];repeat_dt=[]
 for dev,name,wanted,stamp,flags,values in repeats:
  ok,t,actual,path=rt.call(dev,name,wanted,[(stamp,flags,values)]);assert ok and t==wanted and actual[0]==flags
  err=max(abs(x-y) for x,y in zip(actual[1:],oracle(dev,name,values,(wanted-stamp)/1e9,True,True)));assert err<2e-5
  repeat_errors.append(err);repeat_dt.append((wanted-stamp)/1e6)
 assert repeats,'No matched repeated API states were exercised'
 result['repeated_api_states']={'cases':len(repeats),'max_component_error':max(repeat_errors),'requested_minus_sample_ms':stats(repeat_dt),'returned_time_exact':True}
 # Endpoints and negative/positive directions, stock server flag,
 # missing input. These exercise the actual selection and returned-time paths.
 base=trials[len(trials)//2];dev,name,_,stamp,flags,values=base;edge=[]
 for force in (False,True):
  rt.configure(True,force)
  for flag in (0,1):
   for delta in (-40000000,-1000000,0,1000000,40000000):
    wanted=stamp+delta;ok,t,actual,path=rt.call(dev,name,wanted,[(stamp,flags,values)],flag);enabled=force or flag
    assert ok and t==(wanted if enabled else stamp)
    assert max(abs(x-y) for x,y in zip(actual[1:],oracle(dev,name,values,delta/1e9,True,enabled)))<2e-5
    edge.append({'force':force,'flag':flag,'delta_ns':delta,'returned_ns':t,'paths':path})
  ok,t,actual,path=rt.call(dev,name,stamp,[]);assert not ok and t==stamp
  assert bytes(rt.u.mem_read(OUTPUT,56))==bytes([0xA5])*56
 # Two samples with identity orientation: independently check interpolation of p/v/w,
 # including requests outside the pair. Gate must not alter this existing branch.
 pair=[];v0=(0.,0.,0.,1., 1.,2.,3., .1,.2,.3, 0.,0.,0.);v1=(0.,0.,0.,1., 2.,4.,6., .3,.4,.5, 0.,0.,0.)
 for correction in (False,True):
  for force in (False,True):
   rt.configure(correction,force)
   for delta in (-10000000,0,5000000,10000000,20000000):
    wanted=stamp+delta;ok,t,actual,path=rt.call(dev,name,wanted,[(stamp,119,v0),(stamp+10000000,119,v1)])
    expected=tuple(x+(y-x)*delta/10000000 for x,y in zip(v0,v1));expected=oracle(dev,name,expected,0,correction,False)
    assert ok and t==wanted and '0x1b3704' in path
    assert max(abs(x-y) for x,y in zip(actual[1:],expected))<2e-5,(delta,actual,expected)
    pair.append({'correction':correction,'force':force,'delta_ns':delta,'paths':path})
 result['pair_tests']=pair;result['endpoint_tests']=edge;result['missing_input_passthrough']=True;result['producer_bytes_unchanged']=True;result['callee_saved_registers_and_stack_preserved']=True
 a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n');print('PASS',a.output,flush=True)
if __name__=='__main__':main()
