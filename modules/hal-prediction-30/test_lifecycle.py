"""Run the real worker state machine with fake processes/time; never access a headset."""
from pathlib import Path
import hashlib
import subprocess
import tempfile
import unittest
import zipfile
import ctypes
import os, shutil
import struct

HERE = Path(__file__).resolve().parent
SHELL = os.environ.get('GXR_TEST_SHELL') or ('C:/Program Files/Git/bin/bash.exe' if os.name=='nt' else shutil.which('bash'))
WORKSPACE = Path(os.environ.get('GXR_WORKSPACE', str(HERE.parents[3])))

class Lifecycle(unittest.TestCase):
    def worker(self, scenario):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            (p/'state/worker').mkdir(parents=True)
            (p/'state/run').touch()
            (p/'proc/123').mkdir(parents=True)
            (p/'proc/123/maps').write_text('test.so\n')
            (p/'proc/uptime').write_text('100.00\n')
            # common.sh is the only replaced dependency; auto.sh itself runs.
            (p/'common.sh').write_text('''STATE=state
LIBRARY=test.so
MODDIR=.
id_value=123:99
identity() { TARGET=123; IDENTITY=$id_value; }
fail() { echo "ERROR: $*"; touch disable; echo "$*" > last-error; exit 1; }
checksums() { return 0; }
getprop() { echo 1; }
sync() { :; }
sh() {
  case "$2" in
    _apply) echo "$3" >> applied; APPLY_RESULT;;
    _restore) echo restored >> restores; RESTORE_RESULT;;
    status) HEALTH_RESULT;;
    *) return 98;;
  esac
}
tick=0
sleep() {
  tick=$((tick+1))
  STEP
  [ "$tick" -lt 5 ] || rm -f state/run
}
'''.replace('APPLY_RESULT', 'return 1' if scenario == 'apply_fail' else 'return 0')
                .replace('RESTORE_RESULT', 'return 1' if scenario == 'restore_fail' else 'return 0')
                .replace('HEALTH_RESULT', 'echo DISABLED; return 0' if scenario == 'health_fail' else 'echo "ENABLED pid=123"; return 0')
                .replace('STEP', {
                    'success': '[ "$tick" -ne 2 ] || echo 140.00 > proc/uptime',
                    'restart': '[ "$tick" -ne 2 ] || id_value=123:100',
                    'exit': '[ "$tick" -ne 2 ] || id_value=',
                    'apply_fail': ':',
                    'health_fail': '[ "$tick" -ne 2 ] || echo 140.00 > proc/uptime',
                    'restore_fail': ':',
                    'disable': '[ "$tick" -ne 2 ] || touch disable',
                    'remove': '[ "$tick" -ne 2 ] || touch remove',
                    'waiting': ':',
                    'busy': ':',
                    'boot_wait': ':',
                }[scenario]), newline='\n')
            common = (p/'common.sh').read_text()
            if scenario == 'waiting': common = common.replace('id_value=123:99', 'id_value=')
            if scenario == 'boot_wait': common = common.replace('getprop() { echo 1; }', 'getprop() { echo 0; }')
            if scenario == 'busy': common = common.replace('echo "$3" >> applied; return 0', 'echo "$3" >> applied; return 2')
            (p/'common.sh').write_text(common, newline='\n')
            (p/'auto.sh').write_text((HERE/'module/auto.sh').read_text().replace('/proc/', 'proc/'), newline='\n')
            result = subprocess.run([SHELL, './auto.sh', '_run'], cwd=p, capture_output=True, text=True, timeout=10)
            calls = (p/'applied').read_text().splitlines() if (p/'applied').exists() else []
            self.assertTrue((p/'restores').exists(), result.stdout+result.stderr)
            self.assertFalse((p/'state/worker').exists(), result.stderr)
            self.assertFalse((p/'state/run').exists())
            if scenario in ('apply_fail','health_fail','restart','exit','restore_fail'):
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertTrue((p/'disable').exists())
                self.assertTrue((p/'last-error').exists())
            else: self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            if scenario == 'success': self.assertFalse((p/'activation.pending').exists())
            if scenario in ('waiting','boot_wait'): self.assertEqual(calls, [])
            elif scenario != 'busy': self.assertEqual(calls, ['123:99'])
            if scenario in ('apply_fail','restart','exit','health_fail'):
                self.assertTrue((p/'activation.pending').exists())

    def test_worker_scenarios(self):
        for scenario in ('success','restart','exit','apply_fail','health_fail','restore_fail','disable','remove','waiting','busy','boot_wait'):
            with self.subTest(scenario=scenario): self.worker(scenario)

    def test_resume_guards(self):
        for marker in ('disable','remove','activation.pending'):
            with self.subTest(marker=marker), tempfile.TemporaryDirectory() as tmp:
                p=Path(tmp)
                (p/'state').mkdir()
                (p/marker).touch()
                (p/'common.sh').write_text('STATE=state\nMODDIR=.\nfail() { touch disable; echo "$*" > last-error; exit 1; }\n', newline='\n')
                (p/'auto.sh').write_text((HERE/'module/auto.sh').read_text(), newline='\n')
                result=subprocess.run([SHELL,'./auto.sh','resume'],cwd=p,capture_output=True,text=True,timeout=5)
                self.assertFalse((p/'state/worker').exists())
                self.assertFalse((p/'state/run').exists())
                self.assertEqual(result.returncode, 1 if marker=='activation.pending' else 0)
                if marker=='activation.pending': self.assertTrue((p/'disable').exists())

    def test_archives_and_native_provenance(self):
        fixture=WORKSPACE/'diagnostics/hal-prediction-no-shizuku-20261011/native-test'
        for name in ('patch.h',):
            self.assertEqual((HERE/'native'/name).read_bytes(),(fixture/name).read_bytes())
        old=(fixture/'main.cpp').read_text()
        new=(HERE/'native/main.cpp').read_text()
        # Mutation, freeze, identity, file hash and rollback paths stay unchanged.
        self.assertEqual(old.split('#include "patch.h"')[0],new.split('#include "patch.h"')[0])
        self.assertEqual(old.split('int main(')[1],new.split('int main(')[1])
        for variant in ('auto',):
            path=HERE.parents[1]/'dist/hal-prediction-30'/f'gxr-hal-prediction-30-0.1.1-{variant}.zip'
            with zipfile.ZipFile(path) as z:
                self.assertIn('disable', z.namelist())
                self.assertEqual('service.sh' in z.namelist(), variant=='auto')
                self.assertEqual('auto.sh' in z.namelist(), variant=='auto')
                self.assertIn(b'gxrLiveControl=1',z.read('module.prop'))
                for line in z.read('SHA256SUMS').decode().splitlines():
                    digest,name=line.split('  ',1)
                    self.assertNotEqual(name,'disable')
                    self.assertEqual(hashlib.sha256(z.read(name)).hexdigest(),digest)
        for script in (HERE/'module').glob('*.sh'):
            subprocess.run([SHELL,'-n',str(script)],check=True)

    def test_actual_native_validator(self):
        tc=Path(os.environ['ANDROID_NDK_HOME'])/'toolchains/llvm/prebuilt/windows-x86_64/bin'
        out=HERE.parents[1]/'dist/hal-prediction-30'
        subprocess.run([str(tc/'clang++.exe'),'--target=x86_64-pc-windows-msvc','-std=c++17','-Oz','-ffreestanding','-fno-exceptions','-fno-rtti','-c',str(HERE/'native/validation_host.cpp'),'-o',str(out/'validator.obj')],check=True)
        subprocess.run([str(tc/'ld.lld.exe'),'-flavor','link','/dll','/noentry','/nodefaultlib',f'/out:{out/"validator.dll"}',str(out/'validator.obj')],check=True)
        dll=ctypes.CDLL(str(out/'validator.dll'))
        fn=dll.check_rx
        fn.argtypes=[ctypes.c_void_p,ctypes.c_void_p]
        fn.restype=ctypes.c_int
        fixture=WORKSPACE/'diagnostics/hal-prediction-no-shizuku-20261011'
        stock=(fixture.parent/'openxr-velocity-runtime-20261011/controller-service.live.so').read_bytes()[0x10000:0x22000]
        payload=(fixture/'native-test/payload.bin').read_bytes()
        def check(data): return fn(ctypes.create_string_buffer(bytes(data)),ctypes.create_string_buffer(stock))
        for haptic in (False,True):
            for on in (False,True):
                data=bytearray(stock)
                if haptic: struct.pack_into('<I',data,0x238c,0x52800020)
                if on:
                    struct.pack_into('<I',data,0x7758,0x14000000|((0x21210-0x17758)//4))
                    data[0x11210:0x11220]=payload
                self.assertEqual(check(data),int(on))
                # Unknown changes anywhere in RX, including the recognized hook,
                # are rejected; recognized changes are separately covered above.
                for offset in range(0,len(data),4):
                    corrupt=data.copy(); corrupt[offset]^=0x80
                    if 0x22ac <= offset < 0x2914:
                        self.assertEqual(check(corrupt),int(on))
                    else:
                        self.assertLess(check(corrupt),0,hex(offset+0x10000))
        # Unknown future Haptic implementations accepted without a version table.
        import random
        rng=random.Random(100)
        for on in (False,True):
            for pattern in (bytes(1640),bytes([255])*1640,rng.randbytes(1640)):
                data=bytearray(stock);data[0x22ac:0x2914]=pattern
                if on:
                    struct.pack_into('<I',data,0x7758,0x14000000|((0x21210-0x17758)//4))
                    data[0x11210:0x11220]=payload
                before=data[0x22ac:0x2914]
                self.assertEqual(check(data),int(on))
                self.assertEqual(data[0x22ac:0x2914],before)
                for boundary in (0x22ab,0x2914):
                    corrupt=data.copy();corrupt[boundary]^=1
                    self.assertLess(check(corrupt),0)
        # Current tuned Haptic, with and without Prediction.
        import re
        header=(HERE.parent/'haptic-main/native/tuning.h').read_text()
        tune=bytes(int(x,16) for x in re.findall(r'0x[0-9a-f]+',header))
        for on in (False,True):
            data=bytearray(stock);struct.pack_into('<I',data,0x238c,0x52800020)
            data[0x2870:0x2870+len(tune)]=tune
            if on:
                struct.pack_into('<I',data,0x7758,0x14000000|((0x21210-0x17758)//4))
                data[0x11210:0x11220]=payload
            self.assertEqual(check(data),int(on))
        data=bytearray(stock); data[0x11210:0x11220]=payload
        self.assertEqual(check(data),0) # inert tail after off
        data=bytearray(stock); struct.pack_into('<I',data,0x7758,0x14000000|((0x21210-0x17758)//4))
        self.assertLess(check(data),0) # branch without payload
        self.assertEqual(check((fixture/'native-test/current-rx.bin').read_bytes()),0)

if __name__=='__main__': unittest.main(verbosity=2)
