"""Check the disabled automatic package and actual ARM64 test results."""
import hashlib,zipfile,json
from pathlib import Path
HERE=Path(__file__).resolve().parent
out=HERE.parents[1]/'dist/openxr-velocity-frame'
props=dict(l.split('=',1) for l in (HERE/'module/module.prop').read_text().splitlines() if '=' in l)
path=out/f'gxr-openxr-velocity-frame-{props["version"]}-auto.zip'
assert path.with_suffix('.zip.sha256').read_text().split()[0]==hashlib.sha256(path.read_bytes()).hexdigest()
with zipfile.ZipFile(path) as z:
 assert z.testzip() is None
 names=set(z.namelist());assert len(names)==len(z.namelist())
 assert all(not n.startswith('/') and '..' not in n.split('/') for n in names)
 assert {'service.sh','auto.sh','disable','skip_mount'}<=names
 assert not {'post-fs-data.sh','system.prop','auto-default'}&names
 assert b'abort ' in z.read('customize.sh')
 checks=z.read('SHA256SUMS').decode().splitlines()
 assert {l.split('  ',1)[1] for l in checks}==names-{'SHA256SUMS','disable'}
 for line in checks:
  digest,name=line.split('  ',1);assert hashlib.sha256(z.read(name)).hexdigest()==digest
 for name in names:
  if name.endswith('.sh'):assert b'\r' not in z.read(name)
 blob=z.read('bin/gxr-openxr-velocity');assert blob[:6]==b'\x7fELF\x02\x01' and blob[18:20]==b'\xb7\x00'
 assert blob==(out/'gxr-openxr-velocity').read_bytes()
assert len((out/'payload.bin').read_bytes())<=0x520
assert json.loads((out/'tests.json').read_text())['random_actual_cpp_cases']==6000
assert json.loads((out/'preprediction-tests.json').read_text())['ARM64_runtime_prediction_cases']>=1728
assert json.loads((out/'compatibility-tests.json').read_text())['corrupted_byte_cases_rejected']==10096
print('PASS: disabled automatic package, checksums, native provenance, math and compatibility results')
