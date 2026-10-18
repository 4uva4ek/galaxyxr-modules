"""Execute the vendor's own accumulate_once through dt/dv writes (no Jacobians)."""
import sys,struct,json,math,hashlib
from pathlib import Path
sys.path.insert(0,'E:/GalaxyXR/diagnostics/compatibility-test-deps')
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE
import unicorn.arm64_const as ac
import build
H=Path(__file__).resolve().parent
b=build.LIB.read_bytes();assert hashlib.sha256(b).hexdigest()==build.SHA

def run(force,bias,dt):
    u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);u.mem_map(0,0x200000);u.mem_map(0x1000000,0x100000)
    ph=struct.unpack_from('<Q',b,32)[0];sz,n=struct.unpack_from('<HH',b,54)
    for i in range(n):
        t,f,o,v,_,fs,ms,a=struct.unpack_from('<IIQQQQQQ',b,ph+i*sz)
        if t==1:u.mem_write(v,b[o:o+fs])
    def reg(n,v):u.reg_write(getattr(ac,'UC_ARM64_REG_'+n),v)
    def mem(a,fmt,*v):u.mem_write(a,struct.pack(fmt,*v))
    obj=0x1000070;imu=0x1010000;ptr=0x1020000
    reg('SP',0x1080000);reg('CPACR_EL1',3<<20);reg('TPIDR_EL0',0x1030000)
    reg('X0',ptr);reg('X1',obj);reg('X2',0);reg('X3',0);reg('X4',1)
    reg('Q0',int.from_bytes(struct.pack('<f',dt),'little'))
    mem(ptr,'<Q',imu);mem(imu+8,'<3f',0,0,0);mem(imu+0x14,'<3f',*force)
    mem(obj+0x20,'<9f',1,0,0,0,1,0,0,0,1);mem(obj+0x2ac,'<3f',*bias)
    mem(obj+0x18,'<d',.02)
    u.emu_start(0x75008,0x75300,count=3000)
    assert u.reg_read(ac.UC_ARM64_REG_PC)==0x75300
    duration=struct.unpack('<d',u.mem_read(obj+0x18,8))[0]
    dv=struct.unpack('<3f',u.mem_read(obj+0x44,12));dp=struct.unpack('<3f',u.mem_read(obj+0x50,12))
    exp=[(f-bi)*dt for f,bi in zip(force,bias)]
    assert abs(duration-(.02+dt))<1e-8
    assert max(abs(x-y) for x,y in zip(dv,exp))<1e-6
    assert max(abs(x-y*.5*dt) for x,y in zip(dp,exp))<1e-7
    return dict(force=force,bias=bias,step_dt=dt,accumulated_dt=duration,dv=dv,dp=dp)

out=dict(status='PASS',cases=[run([3,5,10],[.1,.2,.3],.01),run([0,0,9.81],[0,0,0],.002)],
 confirmed=['OutPredictor passes embedded object at +0x70 to Preintegrator.','Preintegration+0x18 = predictor+0x88: double accumulated duration, incremented by float step dt.','Preintegration+0x44..0x4c = predictor+0xb4..0xbc: rotated bias-subtracted specific-force increments, gravity not included.','accel bias is subtracted at 0x750f4/0x750f8 (zero-gyro branch); dv write at 0x75244/0x75284; dt write at 0x752fc.'],
 limits='Test uses identity preintegration rotation and zero gyro; original matrix multiplication was inspected, not a full motion-quality validation.')
(H/'field-semantics.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2))
