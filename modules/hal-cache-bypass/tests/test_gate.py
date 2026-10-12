"""Exercise the actual read-only gate against local fake KernelSU state."""
from pathlib import Path
import os,subprocess,tempfile,unittest,json
ROOT=Path(__file__).resolve().parents[1]
SHELL=os.environ.get('GXR_TEST_SHELL','C:/Program Files/Git/bin/bash.exe' if os.name=='nt' else '/bin/sh')
GEN='11111111-2222-3333-4444-555555555555'

class Gate(unittest.TestCase):
 def check(self,case):
  with tempfile.TemporaryDirectory(dir=ROOT/'tests') as temp:
   p=Path(temp);stage=p/'stage';stage.mkdir()
   (stage/'ready').write_text('ready\n');(stage/'generation').write_text(GEN+'\n')
   info='late_load: true\nruntime_mode: late-load\n'
   boot='1';tail=''
   if case=='cold_mode':info='late_load: false\nruntime_mode: boot-lifecycle\n'
   if case=='wrong_runtime':info='late_load: true\nruntime_mode: boot-lifecycle\n'
   if case=='wrong_late':info='late_load: false\nruntime_mode: late-load\n'
   if case=='substring':info='not_late_load: true\nruntime_mode: late-load-extra\n'
   if case=='booting':boot='0'
   if case in ('missing_ready','missing_generation'):(stage/case.removeprefix('missing_')).unlink()
   if case in ('busy','error'):(stage/case).touch()
   if case=='invalid_ready':(stage/'ready').write_text('almost ready\n')
   if case=='invalid_generation':(stage/'generation').write_text('bad\n')
   if case=='changing_generation':tail="printf 'aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\\n' > ./stage/generation\n"
   if case=='busy_during_query':tail='touch ./stage/busy\n'
   if case=='query_failure':tail='exit 1\n'
   ksud="#!/bin/sh\nprintf '%s' '"+info+"'\n"+tail
   (p/'ksud').write_text(ksud,newline='\n')
   source=(ROOT/'module/gate.sh').read_text().replace('/dev/ksu-restart-stage','./stage').replace('timeout 2 /data/adb/ksud debug info','sh ./ksud debug info')
   (p/'gate.sh').write_text(source,newline='\n')
   (p/'test.sh').write_text('getprop() { echo '+boot+'; }\n. ./gate.sh\nif gate; then printf "ENABLED %s\\n" "$GATE_GEN"; exit 0; else printf "WAITING %s\\n" "$GATE_REASON"; exit 3; fi\n',newline='\n')
   r=subprocess.run([SHELL,'./test.sh'],cwd=p,text=True,capture_output=True,timeout=8)
   self.assertEqual(r.returncode,0 if case=='ready' else 3,r.stdout+r.stderr)
   self.assertIn('ENABLED '+GEN if case=='ready' else 'WAITING ',r.stdout)
   self.assertFalse((p/'disable').exists())
 def test_cases(self):
  for case in ('ready','cold_mode','wrong_runtime','wrong_late','substring','booting','missing_ready','missing_generation','busy','error','invalid_ready','invalid_generation','changing_generation','busy_during_query','query_failure'):
   with self.subTest(case=case):self.check(case)

if __name__=='__main__':unittest.main(verbosity=2)
