"""Run the real action script against a worker timeout and a fake native helper."""
from pathlib import Path
import subprocess,tempfile
HERE=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as temp:
 p=Path(temp);(p/'bin').mkdir();(p/'state').mkdir();(p/'proc').mkdir()
 (p/'proc/mounts').write_text('tmpfs /dev tmpfs rw 0 0\n')
 (p/'auto.sh').write_text('echo "Worker did not exit"\nexit 1\n')
 (p/'bin/gxr-extrapolation').write_text('#!/bin/sh\necho "$2" > native-called\necho "STOCK pid=123"\n')
 script=(HERE/'module/action.sh').read_text().replace('STATE=/dev/gxr_controller_extrapolation','STATE=state').replace('/proc/mounts','proc/mounts').replace('/dev/gxr_openxr_transaction','global-lock')
 prefix='''id() { echo 0; }
pidof() { echo 123; }
sha256sum() { [ "$1" = -c ] || echo 'a7dabaa1030cc7979efe987f212488c61b61e8afb9aa3add59f7ae4fbeff4c92  runtime'; }
'''
 (p/'action.sh').write_text(prefix+script,newline='\n')
 result=subprocess.run(['C:/Program Files/Git/bin/bash.exe','./action.sh','off'],cwd=p,capture_output=True,text=True,timeout=10)
 assert result.returncode==1,(result.stdout,result.stderr)
 assert (p/'native-called').read_text().strip()=='off',(result.stdout,result.stderr)
 assert (p/'disable').exists()
 assert 'STOCK pid=123' in result.stdout and 'worker exit is unconfirmed' in result.stdout
 print('PASS: worker timeout does not skip native restoration; failure remains explicit and disabled')
