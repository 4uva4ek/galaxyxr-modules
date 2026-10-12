"""Narrow Manager control-contract checks; no device or tracking simulation."""
import os,pathlib,subprocess,tempfile,shutil
HERE=pathlib.Path(__file__).resolve().parent
bash=shutil.which('bash') if os.name!='nt' else 'C:/Program Files/Git/bin/bash.exe'
temp_root=HERE.parents[1]/'dist/control-tests';temp_root.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(dir=temp_root) as tmp:
 assert pathlib.Path(tmp).resolve().is_relative_to(temp_root.resolve())
 r=pathlib.Path(tmp);(r/'bin').mkdir();(r/'state').mkdir();(r/'disable').touch()
 for n in ('action.sh','auto.sh'):
  text=(HERE/'module'/n).read_text().replace('STATE=/dev/gxr_steamlink_pose_time','STATE="$MODDIR/state"')
  if n=='auto.sh':
   text=text.replace('[ "$(id -u)" = 0 ] || exit 1',': # host fixture has no Android root')
   text=text.replace("awk '$2==\"/dev\" && $3==\"tmpfs\" {ok=1} END {exit !ok}' /proc/mounts",': # host fixture uses temporary state')
  (r/n).write_text(text,newline='\n')
 (r/'bin/gxr-steamlink-pose-time').write_text("#!/bin/sh\nr=${0%/bin/*}\ncase $1 in\nstatus) cat \"$r/fixture-status\"; test ! -f \"$r/fixture-error\" ;;\nrestore) rm -f \"$r/state/target\"; echo restored ;;\nesac\n",newline='\n')
 (r/'bin/gxr-steamlink-pose-time').chmod(0o755)
 def run(mode):
  return subprocess.run([bash,(r/'action.sh').as_posix(),mode],capture_output=True,text=True)
 (r/'fixture-status').write_text('DISABLED - Waiting: no app patch is resident\n')
 (r/'state/auto.log').write_text('ENABLED - old log entry\n')
 a=run('status');assert a.returncode==0 and 'GXR_UPDATE_STATE=STOCK' in a.stdout and 'old log' not in a.stdout,(a.returncode,a.stdout,a.stderr)
 (r/'state/target').touch();a=run('status');assert 'GXR_UPDATE_STATE=STOCK' not in a.stdout
 a=run('off');assert a.returncode==0,a.stderr;assert not (r/'state/target').exists()
 (r/'state/worker').mkdir();a=run('status');assert 'GXR_UPDATE_STATE=STOCK' not in a.stdout;(r/'state/worker').rmdir()
 (r/'fixture-error').touch();a=run('status');assert a.returncode!=0 and 'GXR_UPDATE_STATE=STOCK' not in a.stdout
 assert (r/'disable').exists()
print('PASS Manager off/status: restoration, no false stock for live state/worker/error, no stale-log contamination')
