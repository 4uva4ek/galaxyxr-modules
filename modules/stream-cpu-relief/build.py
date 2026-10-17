"""NDK r29 builder; automatic disabled package only. No tests/device access."""
import hashlib,json,os,subprocess,zipfile
from pathlib import Path
source=Path(__file__).resolve().parent
root=source.parents[1]
ndk=Path(os.environ.get('ANDROID_NDK_HOME','/home/dev/Android/Sdk/ndk/29.0.14206865'))
assert 'Pkg.Revision = 29.0.14206865' in (ndk/'source.properties').read_text()
tool=ndk/'toolchains/llvm/prebuilt/linux-x86_64'
out=root/'dist/stream-cpu-relief';out.mkdir(parents=True,exist_ok=True)
archive=out/'gxr-stream-cpu-relief-0.1.0-auto.zip'
if archive.exists(): raise SystemExit('Refusing to overwrite an existing artifact')
binary=out/'gxr-stream-cpu'
subprocess.run([str(tool/'bin/aarch64-linux-android34-clang++'),'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static-libstdc++','-Wl,-z,max-page-size=16384',str(source/'native/main.cpp'),'-o',str(binary)],check=True)
subprocess.run([str(tool/'bin/llvm-strip'),'--strip-unneeded',str(binary)],check=True)
payload={p.name:p.read_bytes() for p in (source/'module').iterdir() if p.is_file()}
payload.update({'disable':b'','cold-boot-reset.sh':(root/'tools/cold-boot-reset.sh').read_bytes(),'bin/gxr-stream-cpu':binary.read_bytes(),'NOTICE.txt':(source/'NATIVE-NOTICE.txt').read_bytes(),'NDK-NOTICE.txt':(tool/'NOTICE').read_bytes()})
payload['SHA256SUMS']=''.join(f'{hashlib.sha256(b).hexdigest()}  {n}\n' for n,b in sorted(payload.items()) if n!='disable').encode()
with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED) as z:
 for n,b in sorted(payload.items()):
  i=zipfile.ZipInfo(n,date_time=(2026,10,17,0,0,0));i.create_system=3;i.compress_type=zipfile.ZIP_DEFLATED;i.external_attr=(0o100755 if n.endswith('.sh') or n.startswith('bin/') else 0o100644)<<16;z.writestr(i,b)
archive.with_suffix('.zip.sha256').write_text(hashlib.sha256(archive.read_bytes()).hexdigest()+'  '+archive.name+'\n')
print(archive)
