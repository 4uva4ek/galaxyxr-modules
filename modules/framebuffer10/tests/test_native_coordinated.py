from pathlib import Path
import subprocess,json,tempfile
here=Path(__file__).resolve().parent
source=here.parent/'native/main.cpp'
work=tempfile.TemporaryDirectory(prefix='gxr-fb10-native-')
binary=Path(work.name)/'native-host-test'
subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
 '-I'+str(here/'host-include'),str(source),'-o',str(binary)],check=True)
results=[]
for scenario,expected in [('normal',0),('coordinated-handoff',4),('coordinated-start-timeout',2),('bind-denied',2),('output-stays8',2),('unmount-denied',3),('clear-denied',3),('planned-disable',0)]:
 r=subprocess.run([str(binary),'--simulate-coordinated',scenario],capture_output=True,text=True,timeout=5)
 assert r.returncode==expected,(scenario,r.returncode,r.stdout)
 activation=r.stdout.split('DISABLED - rolling back')[0]
 assert 'COMMAND ctl.start ' not in activation and 'COMMAND ctl.stop ' not in activation and 'COMMAND ctl.restart ' not in activation,r.stdout
 if scenario=='normal':
  assert activation.count('EXTERNAL KernelSU start')==1 and 'ENABLED -' in activation,r.stdout
 if scenario=='coordinated-start-timeout':
  assert 'starts=0' in r.stdout and 'selector=0' in r.stdout and 'oneshot=0' in r.stdout,r.stdout
 results.append([scenario,expected])
work.cleanup()
print('PASS: 7 coordinated native scenarios, no independent successful-path Android restart')
