import pathlib,subprocess,hashlib,json,zipfile,argparse,platform,os,sys
HERE=pathlib.Path(__file__).resolve().parent
ROOT=HERE.parents[1]
R=ROOT/'dist/steamlink-pose-time';R.mkdir(parents=True,exist_ok=True)
args=argparse.ArgumentParser();args.add_argument('--scene',type=pathlib.Path,required=True);args.add_argument('--runtime',type=pathlib.Path,required=True);args.add_argument('--ndk',type=pathlib.Path,required=True);opt=args.parse_args()
assert 'Pkg.Revision = 28.2.13676358' in (opt.ndk/'source.properties').read_text()
host={'Windows':'windows-x86_64','Linux':'linux-x86_64','Darwin':'darwin-x86_64'}[platform.system()]
TC=opt.ndk/'toolchains/llvm/prebuilt'/host
subprocess.run([sys.executable,str(ROOT/'modules/openxr-velocity-frame/prepare.py'),'--ndk',str(opt.ndk),'--runtime',str(opt.runtime)],check=True)
B=TC/'bin'
def tool(n):return str(B/(n+('.exe' if os.name=='nt' else '')))
def run(args):subprocess.run(args,check=True)
cc=[tool('clang++'),'-ccc-install-dir',str(B),'--target=aarch64-linux-android29',f'--sysroot={TC/"sysroot"}','-resource-dir',str(TC/'lib/clang/19'),'-std=c++17','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function','-mno-outline-atomics']
run(cc+['-ffreestanding','-fno-builtin','-fno-exceptions','-fno-rtti','-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-fpie','-fvisibility=hidden','-mbranch-protection=bti','-c',str(HERE/'native/payload.cpp'),'-o',str(R/'payload.o')])
(R/'payload.ld').write_text('ENTRY(hook_consume)\nSECTIONS { . = 0; .payload : { KEEP(*(.consume)) KEEP(*(.state)) KEEP(*(.submit)) *(.text*) *(.rodata*) } . = 0x8000; .control (NOLOAD) : { *(.control) *(.bss*) } /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) } }\n')
run([tool('ld.lld'),'--no-undefined','--build-id=none','-T',str(R/'payload.ld'),str(R/'payload.o'),'-o',str(R/'payload.elf')])
report=subprocess.check_output([tool('llvm-readelf'),'-rs',str(R/'payload.elf')],text=True)
assert 'There are no relocations' in report
def sym(name):return int(next(l for l in report.splitlines() if l.rstrip().endswith(' '+name)).split()[1],16)
assert sym('gxr_control')==0x8000
run([tool('llvm-objcopy'),'-O','binary','--only-section=.payload',str(R/'payload.elf'),str(R/'payload.bin')])
payload=(R/'payload.bin').read_bytes();assert len(payload)<0x8000
scene=opt.scene.read_bytes();assert hashlib.sha256(scene).hexdigest()=='a1f1907ee91dc30dcdb4e3924cf3cff6e78302131fa835d9eb40f3fd4946ebde'
# This ELF's relevant executable VAs equal file offsets.
parts={'PAYLOAD':payload,'STATE_CODE':scene[0x1070cc:0x107b44],'SEND_CODE':scene[0xf4e74:0xf5388],'CLOCK_CODE':scene[0x155bf4:0x155c34]}
h=''.join('static const unsigned char '+k+'[]={'+','.join(map(str,v))+'};\n' for k,v in parts.items())
for name,s in [('CONSUME_ENTRY','hook_consume'),('STATE_ENTRY','hook_state'),('SUBMIT_ENTRY','hook_submit')]:h+='static constexpr uintptr_t '+name+'='+str(sym(s))+';\n'
(R/'generated.h').write_text(h)
run(cc+['-I',str(R),'-c',str(HERE/'native/main.cpp'),'-o',str(R/'main.o')])
run(cc+[f'--ld-path={tool("ld.lld")}','-static-libstdc++','-Wl,-z,max-page-size=16384',str(R/'main.o'),'-o',str(R/'gxr-steamlink-pose-time')])

files={p.name:p.read_bytes() for p in (HERE/'module').iterdir() if p.is_file()}
files['bin/gxr-steamlink-pose-time']=(R/'gxr-steamlink-pose-time').read_bytes()
files['webroot/index.html']=(HERE/'module/webroot/index.html').read_bytes()
files['cold-boot-reset.sh']=(ROOT/'tools/cold-boot-reset.sh').read_bytes()
files['NOTICE.txt']=b'Author 4uva4ek. App RAM pose-time forwarding; Android NDK static libc++ notice follows. Vendor code signatures retained solely for compatibility verification. No vendor library replacement.\n'
files['NDK-NOTICE.txt']=(TC/'NOTICE').read_bytes()
files['disable']=b'';files['skip_mount']=b''
files['SHA256SUMS']=''.join(f'{hashlib.sha256(v).hexdigest()}  {n}\n' for n,v in sorted(files.items()) if n!='disable').encode()
props=dict(l.split('=',1) for l in files['module.prop'].decode().splitlines() if '=' in l)
archive=R/f'gxr-steamlink-pose-time-{props["version"]}-auto.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
 for n,v in sorted(files.items()):
  if n.endswith('.sh'):assert b'\r' not in v
  info=zipfile.ZipInfo(n,(2026,10,13,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED;info.create_system=3
  info.external_attr=(0o100755 if n.endswith('.sh') or n.startswith('bin/') else 0o100644)<<16;z.writestr(info,v)
sha=hashlib.sha256(archive.read_bytes()).hexdigest();archive.with_suffix('.zip.sha256').write_text(sha+'  '+archive.name+'\n')
report={'archive':str(archive),'sha256':sha,'helper_sha256':hashlib.sha256(files['bin/gxr-steamlink-pose-time']).hexdigest(),'payload_sha256':hashlib.sha256(payload).hexdigest(),'payload_bytes':len(payload),'default':'DISABLED','scope':'Steam Link app RAM only; no HAL or original file changes','manual_validation':'0.1.7 public release of the live-update-fixed payload (slot label minus runtime compensation, default 15 ms, 0..30 ms, 28.82 ms = measured original lead); user confirmed the module works on the headset; per-value wire offsets other than the raw label not independently measured'}
(R/'build.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
