"""Run production framebuffer hook dispatch with fake Android/native effects."""
from pathlib import Path
import tempfile,subprocess,os,json
here=Path(__file__).resolve().parent
source=here.parent/'module'
results=[]
with tempfile.TemporaryDirectory() as tmp:
 w=Path(tmp);state=w/'ram';coord=w/'coord';state.mkdir();coord.mkdir();(w/'bin').mkdir();(w/'payload').mkdir()
 (coord/'busy').touch();(coord/'generation').write_text('one\n')
 script=(source/'restart-hooks.sh').read_text().replace('/dev/gxr-framebuffer10',str(state)).replace('/dev/ksu-restart-stage',str(coord))
 mocks='''sha256sum() { return 0; }
chown() { return 0; }
chmod() { return 0; }
chcon() { return 0; }
getprop() { echo stopped; }
sleep() { command sleep 0.01; }
'''
 (w/'restart-hooks.sh').write_text(mocks+script)
 (w/'cold-boot-reset.sh').write_text('exit 0\n')
 native='''#!/bin/sh
M=${0%/*}/..
echo "$1" >> "$M/calls"
case "$1" in
 --status) cat "$M/native-status";;
 --prepare-coordinated-restart) touch "$M/handoff"; rm -f "$M/session-lease/boot-id"; rmdir "$M/session-lease";;
 --stop) rm -f "$M/session-lease/boot-id"; rmdir "$M/session-lease"; echo 'DISABLED - baseline' > "$M/native-status";;
 --preflight|--preflight-stopped) exit 0;;
 *) exit 64;;
esac
'''
 (w/'bin/gxr-fb10-gate').write_text(native);(w/'bin/gxr-fb10-gate').chmod(0o755)
 (w/'auto.sh').write_text(f'''#!/bin/sh
[ "$1" = resume-coordinated ] || exit 64
M=${{0%/*}}
mkdir "$M/session-lease"
echo boot > "$M/session-lease/boot-id"
echo "$KSU_RESTART_GENERATION" > {state}/start-ready
echo 'ENABLED - mock verified output' > "$M/native-status"
''')
 def run(phase,generation):
  env=os.environ.copy();env['KSU_RESTART_GENERATION']=generation
  return subprocess.run(['sh',str(w/'restart-hooks.sh'),phase],env=env,capture_output=True,text=True,timeout=5)
 for generation in ('one','two','three'):
  (coord/'generation').write_text(generation+'\n')
  for phase in ('prepare','stopped','started'):
   r=run(phase,generation);assert r.returncode==0,(phase,r.stdout,r.stderr)
  assert not (w/'disable').exists()
  results.append('complete transaction '+generation)
 assert (w/'handoff').exists()
 r=run('started','stale');assert r.returncode!=0 and (w/'disable').exists()
 results.append('stale generation refuses success')
 r=run('abort','three');assert r.returncode==0,(r.stdout,r.stderr)
 assert (w/'disable').exists() and not (w/'session-lease').exists()
 results.append('abort restores through native helper and preserves OFF')
print('PASS:',results)
