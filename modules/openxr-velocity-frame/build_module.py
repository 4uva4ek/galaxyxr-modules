"""Build a disabled automatic ZIP; never connect to a headset."""
import argparse,hashlib,json,platform,zipfile
from pathlib import Path
from prepare import build,HERE

def package(ndk,runtime,clients_only=False):
 build(ndk,runtime,clients_only)
 out=HERE.parents[1]/'dist/openxr-velocity-frame'
 host={'Windows':'windows-x86_64','Linux':'linux-x86_64','Darwin':'darwin-x86_64'}[platform.system()]
 props=dict(l.split('=',1) for l in (HERE/'module/module.prop').read_text().splitlines() if '=' in l)
 for variant in ('auto',):
  names=['module.prop','action.sh','customize.sh']
  if variant=='auto':names+=['auto.sh','service.sh']
  files={n:(HERE/'module'/n).read_bytes() for n in names}
  files['cold-boot-reset.sh']=(HERE.parents[1]/'tools/cold-boot-reset.sh').read_bytes()
  files['action-report.sh']=(HERE.parents[1]/'tools/action-report.sh').read_bytes()
  files['emulated-soft-reboot.sh']=(HERE.parents[1]/'tools/emulated-soft-reboot.sh').read_bytes()
  p=dict(props);p['name']+=f' ({variant})'
  files['module.prop']=''.join(f'{k}={v}\n' for k,v in p.items()).encode()
  files['skip_mount']=b''
  files['disable']=b''
  assert all(b'\r' not in v for v in files.values())
  files['bin/gxr-openxr-velocity']=(out/'gxr-openxr-velocity').read_bytes()
  files['NOTICE.txt']=b'Galaxy XR OpenXR velocity correction. Original freestanding math and guarded ptrace helper. Android NDK r28c; static libc++. See NDK-NOTICE.txt.\n'
  files['NDK-NOTICE.txt']=(ndk/'toolchains/llvm/prebuilt'/host/'NOTICE').read_bytes()
  files['SHA256SUMS']=''.join(f'{hashlib.sha256(b).hexdigest()}  {n}\n' for n,b in sorted(files.items()) if n != 'disable').encode()
  archive=out/f'gxr-openxr-velocity-frame-{props["version"]}-{variant}.zip'
  with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
   for n,b in sorted(files.items()):
    info=zipfile.ZipInfo(n,date_time=(2026,10,11,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED;info.create_system=3
    info.external_attr=(0o100755 if n.endswith('.sh') or n.startswith('bin/') else 0o100644)<<16
    z.writestr(info,b)
  archive.with_suffix('.zip.sha256').write_text(hashlib.sha256(archive.read_bytes()).hexdigest()+'  '+archive.name+'\n')
  stage=out/variant
  for n,b in files.items():
   dest=stage/n;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(b)
  print(archive)

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--ndk',type=Path,required=True);p.add_argument('--runtime',type=Path,required=True)
 p.add_argument('--streaming-clients-only',action='store_true',help='Limit targets to Steam Link and ChronoLink packages and their copies')
 args=p.parse_args();package(args.ndk.resolve(),args.runtime.resolve(),args.streaming_clients_only)
