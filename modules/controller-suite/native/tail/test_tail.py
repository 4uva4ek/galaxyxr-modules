"""Execute pinned vendor instructions + assembled payload in ARM64 Unicorn.
Synthetic boundary states, not a headset test or tracking quality benchmark.
"""
import sys,struct,math,json,random,hashlib
from pathlib import Path
sys.path.insert(0,'E:/GalaxyXR/diagnostics/compatibility-test-deps')
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE,UC_PROT_READ,UC_PROT_WRITE,UC_PROT_EXEC
import unicorn.arm64_const as ac
import build
H=Path(__file__).resolve().parent
M=json.loads((H/'manifest.json').read_text()); ELF=build.LIB.read_bytes()
assert hashlib.sha256(ELF).hexdigest()==build.SHA
CAVE=0x400000; DATA=CAVE+0x4000
# Relocator requires nonzero page-aligned base. ELF mapped at BASE in both runs.
BASE=0x200000; SP=0x1080000; PRED=0x1000000; SRC=0x1010000; ROT=0x1020000
PAYLOAD,ENTRY=build.relocate((H/'tail-template.bin').read_bytes(),M['return_offset'],BASE,CAVE)

def run(c,patched=False,mode=0,k=1.,locked=False,badmagic=False):
    u=Uc(UC_ARCH_ARM64,UC_MODE_ARM); u.mem_map(BASE,0x200000);u.mem_map(0x1000000,0x100000)
    ph=struct.unpack_from('<Q',ELF,32)[0];sz,n=struct.unpack_from('<HH',ELF,54)
    for i in range(n):
        typ,flags,off,va,_,fs,ms,align=struct.unpack_from('<IIQQQQQQ',ELF,ph+i*sz)
        if typ==1:u.mem_write(BASE+va,ELF[off:off+fs])
    assert bytes(u.mem_read(BASE+build.SITE,4)).hex()==M['original_word']
    def reg(n,v): u.reg_write(getattr(ac,'UC_ARM64_REG_'+n),v)
    def fp(n,fmt,*v):reg('Q'+str(n),int.from_bytes(struct.pack(fmt,*v).ljust(16,b'\0'),'little'))
    def mem(a,fmt,*v):u.mem_write(a,struct.pack(fmt,*v))
    def f(n):return struct.unpack('<4f',u.reg_read(getattr(ac,'UC_ARM64_REG_Q'+str(n))).to_bytes(16,'little'))
    rng=random.Random(1826)
    for i in range(31):reg('X'+str(i),rng.getrandbits(48))
    for i in range(32):fp(i,'<4f',*[rng.uniform(-3,3) for _ in range(4)])
    reg('SP',SP);reg('CPACR_EL1',3<<20);reg('X20',PRED);reg('X21',SRC);reg('X23',SRC);reg('X25',ROT);reg('X27',SP+0x220);reg('X24',PRED+0x800)
    reg('FPSR',0x08000000);reg('FPCR',c.get('fpcr',0));reg('NZCV',0xa0000000)
    R=c.get('R',[1,0,0,0,1,0,0,0,1]);g=c.get('g',[0,0,-9.81]);dt=c.get('dt',.01);h=c.get('h',.014)
    v=c.get('v',[1.,2.,3.]);a=c.get('a',[2.,-3.,4.]);p=c.get('p',[.2,-.4,.6]);omega=c.get('w',[.1,.2,.3])
    # R is column-major; dv is source-frame specific force integrated over dt.
    dv=c.get('dv',[sum(R[3*j+i]*(a[i]-g[i]) for i in range(3))*dt for j in range(3)])
    mem(ROT+0xa0,'<9f',*R);mem(PRED+0x90,'<9f',1,0,0,0,1,0,0,0,1)
    mem(PRED+0x88,'<d',dt);mem(PRED+0xb4,'<3f',*dv);mem(PRED+0xc0,'<3f',*[x*dt*.5 for x in dv])
    mem(PRED+0x48,'<3f',*g);mem(PRED+0x2d0,'<3f',*omega);mem(PRED+0x300,'<4f',0,0,0,1)
    mem(SRC+0xec,'<3f',*v);mem(SRC+0xe0,'<3f',*p);mem(SP+0x130,'<4f',*v,0)
    fp(9,'<d',1.);fp(10,'<d',1.+dt+h)
    if patched:
        u.mem_map(CAVE,0x8000);u.mem_write(CAVE,PAYLOAD);u.mem_write(BASE+build.SITE,ENTRY)
        mem(DATA+8,'<If',mode,k);mem(DATA+16,'<I',int(locked))
        if badmagic:mem(DATA,'<Q',0)
        u.mem_protect(CAVE,0x4000,UC_PROT_READ|UC_PROT_EXEC)
        u.mem_protect(DATA,0x4000,UC_PROT_READ|UC_PROT_WRITE)
    # All executable library pages are read/execute during execution.
    u.mem_protect(BASE,0x200000,UC_PROT_READ|UC_PROT_EXEC)
    hit=[]
    def hook(uc,pc,size,user):
        if pc==BASE+0xe91e0:
            angle=f(0)[0]
            mem(uc.reg_read(ac.UC_ARM64_REG_X0),'<f',math.sin(angle))
            mem(uc.reg_read(ac.UC_ARM64_REG_X1),'<f',math.cos(angle))
            reg('PC',uc.reg_read(ac.UC_ARM64_REG_X30))
        if pc==BASE+build.SITE:hit.append(f(31)[0])
    u.hook_add(UC_HOOK_CODE,hook)
    u.emu_start(BASE+0x727e8,BASE+0x72d58,count=5000)
    assert u.reg_read(ac.UC_ARM64_REG_PC)==BASE+0x72d58
    regs={n:u.reg_read(getattr(ac,'UC_ARM64_REG_'+n)) for n in [*[f'X{i}' for i in range(31)],*[f'Q{i}' for i in range(32)],'SP','NZCV','FPSR','FPCR']}
    return dict(regs=regs,stack=bytes(u.mem_read(SP,0x300)),pred=bytes(u.mem_read(PRED,0x900)),p=[*f(10)[:2],f(14)[0]],v=[*f(15)[:2],f(9)[0]],
                telemetry=bytes(u.mem_read(DATA,2048)) if patched else b'',h=hit)

def unchanged(a,b,velocity=False):
    exclude={'Q9','Q15','Q22'} if velocity else set()
    diff=[n for n in a['regs'] if n not in exclude and a['regs'][n]!=b['regs'][n]]
    assert not diff,('register mismatch',diff)
    assert a['stack']==b['stack'],'Original stack outputs changed'
    assert a['pred']==b['pred'],'Predictor object/orientation changed'

def counters(r):return struct.unpack_from('<4Q',r['telemetry'],32)
def clamp_expected(c):
    R=c.get('R',[1,0,0,0,1,0,0,0,1]);g=c.get('g',[0,0,-9.81]);dt=c.get('dt',.01);a=c.get('a',[2,-3,4])
    if 'dv' in c:a=[sum(R[j*3+i]*c['dv'][j] for j in range(3))/dt+g[i] for i in range(3)]
    norm=math.sqrt(sum(x*x for x in a));a=[x*min(1.,40/norm) for x in a] if norm else a
    d=[x*min(c.get('h',.014),.035)*c.get('k',1) for x in a]
    norm=math.sqrt(sum(x*x for x in d));return [x*min(1.,.5/norm) for x in d] if norm else d

def main():
    rng=random.Random(42);cases=[];rows=[]
    for i in range(36):
        t=rng.uniform(-math.pi,math.pi);co,si=math.cos(t),math.sin(t)
        cases.append(dict(name=f'random-{i}',R=[co,si,0,-si,co,0,0,0,1],v=[rng.uniform(-4,4) for _ in range(3)],
            a=[rng.uniform(-50,50) for _ in range(3)],w=[rng.uniform(-8,8) for _ in range(3)],h=rng.uniform(.0001,.08),dt=rng.uniform(.002,.09),k=rng.choice([0,.5,1.])))
    cases += [dict(name='rest',v=[0,0,0],a=[0,0,0]),dict(name='near-zero-v',v=[1e-8,0,0]),
      dict(name='small-omega',w=[0,0,0]),dict(name='dt-min',dt=.002),dict(name='dt-max',dt=.1),
      dict(name='h-zero',h=0),dict(name='h-negative',h=-.001),dict(name='h-cap',h=.1),
      dict(name='dt-small',dt=.001),dict(name='dt-zero',dt=0),dict(name='dt-negative',dt=-.01),dict(name='dt-large',dt=.101),
      dict(name='dv-nan',dv=[math.nan,0,0],w=[0,0,0]),dict(name='dv-inf',dv=[math.inf,0,0],w=[0,0,0]),
      dict(name='gain-nan',k=math.nan),dict(name='gain-inf',k=math.inf),dict(name='gain-negative',k=-1),dict(name='gain-large',k=2),
      dict(name='fpcr-fz',fpcr=1<<24),dict(name='fpcr-dn',fpcr=1<<25)]
    for c in cases:
        stock=run(c);observe=run(c,True,0);unchanged(stock,observe)
        k=c.get('k',1.);active=run(c,True,1,k);unchanged(stock,active,True)
        dt=c.get('dt',.01)
        valid=bool(stock['h']) and .002<=dt<=.1 and math.isfinite(k) and 0<=k<=1 and all(math.isfinite(x) for x in c.get('dv',[0,0,0]))
        if valid:
            expected=clamp_expected(c)
            err=max(abs(active['v'][i]-stock['v'][i]-expected[i]) for i in range(3))
            assert err<4e-6,(c['name'],err,active['v'],stock['v'],expected)
            assert counters(active)[2]==1
        else:
            unchanged(stock,active);err=0
            assert counters(active)[2]==0
        if stock['h']:
            assert counters(observe)[0]==1
            for off,n in [(128,37),(448,102),(1280,42)]:assert sum(struct.unpack_from('<'+'Q'*n,observe['telemetry'],off))==1
        rows.append(dict(name=c['name'],entered_tail=bool(stock['h']),applied=valid,max_delta_error=err))
    for kwargs in [dict(mode=2),dict(badmagic=True),dict(locked=True)]:
        s=run({});r=run({},True,**kwargs);unchanged(s,r)
        if kwargs.get('locked'):assert counters(r)==(0,0,0,0)
    # Contended telemetry must not disable an otherwise valid correction.
    a=run({},True,1);b=run({},True,1,locked=True);assert a['regs']==b['regs'];assert counters(b)==(0,0,0,0)
    for src,dst in [(0x100000,0x100004),(0x8000000,0),(0,0x7fffffc)]:assert len(build.branch(src,dst))==4
    for src,dst in [(0,0x8000000),(0x8000004,0),(0,3)]:
        try:build.branch(src,dst)
        except ValueError:pass
        else:raise AssertionError('range guard failed')
    out=dict(status='PASS',cases=rows,case_count=len(rows),extra_checks=['unknown mode','bad control magic','contended telemetry','branch range'],
      slice='0x727e8..0x72d58: source integration, near-zero gate, tail, quaternion composition',
      unchanged='mode0 all GPR/SIMD/NZCV/FPSR/FPCR and original stack/object bytes; mode1 allows only Q9/Q15/Q22 velocity changes',
      limitations=['Synthetic boundary states; sincosf host stub; no live timing, latency, quality, native throws or injector lifecycle test.','Histogram counts are best effort and positive-tail only.'])
    (H/'test-results.json').write_text(json.dumps(out,indent=2)+'\n');print('PASS',len(rows),'vendor-slice cases + guards')
if __name__=='__main__':main()
