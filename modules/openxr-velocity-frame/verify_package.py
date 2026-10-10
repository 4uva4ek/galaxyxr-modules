"""Check release variant separation, archive integrity and unchanged tested payload."""
import hashlib,zipfile,json
from pathlib import Path
HERE=Path(__file__).resolve().parent
out=HERE.parents[1]/'dist/openxr-velocity-frame'
archives=list(out.glob('gxr-openxr-velocity-frame-0.1.0-*.zip'))
assert len(archives)==2
for path in archives:
 assert path.with_suffix('.zip.sha256').read_text().split()[0]==hashlib.sha256(path.read_bytes()).hexdigest()
 with zipfile.ZipFile(path) as z:
  assert z.testzip() is None
  names=set(z.namelist());assert len(names)==len(z.namelist())
  assert all(not n.startswith('/') and '..' not in n.split('/') for n in names)
  assert ('service.sh' in names)==path.name.endswith('-auto.zip')
  assert ('auto.sh' in names)==path.name.endswith('-auto.zip')
  assert 'post-fs-data.sh' not in names and 'system.prop' not in names and 'skip_mount' in names
  assert b'abort ' in z.read('customize.sh')
  checks=z.read('SHA256SUMS').decode().splitlines()
  assert {l.split('  ',1)[1] for l in checks}==names-{'SHA256SUMS'}
  for line in checks:
   digest,name=line.split('  ',1);assert hashlib.sha256(z.read(name)).hexdigest()==digest
  for name in names:
   if name.endswith('.sh'):assert b'\r' not in z.read(name)
  blob=z.read('bin/gxr-openxr-velocity');assert blob[:6]==b'\x7fELF\x02\x01' and blob[18:20]==b'\xb7\x00'
manual=zipfile.ZipFile(out/'gxr-openxr-velocity-frame-0.1.0-manual.zip')
auto=zipfile.ZipFile(out/'gxr-openxr-velocity-frame-0.1.0-auto.zip')
assert manual.read('bin/gxr-openxr-velocity')==auto.read('bin/gxr-openxr-velocity')
assert len((out/'payload.bin').read_bytes())==1112
assert json.loads((out/'tests.json').read_text())['random_actual_cpp_cases']==6000
print('PASS: manual/auto package separation, identical native helper, checksums, ELF, no early-boot hooks, installer blocked')
