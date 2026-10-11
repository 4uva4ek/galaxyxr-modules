"""Verify generated tuning.h and build a standalone ARM64 math test; does not patch anything.
Run with --ndk PATH. Push dist/haptic-main/test-tuning and execute without root.
"""
import argparse, re, subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--ndk',required=True);args=p.parse_args()
m=Path(__file__).resolve().parents[1];d=m.parents[1]/'dist/haptic-main';d.mkdir(parents=True,exist_ok=True)
t=Path(args.ndk)/'toolchains/llvm/prebuilt/windows-x86_64'
def assemble(source,output):
 subprocess.run([str(t/'bin/clang.exe'),'-cc1as','-triple','aarch64-linux-android','-filetype','obj','-o',str(output),str(source)],check=True)
assemble(m/'native/tuning.S',d/'tuning.o')
subprocess.run([str(t/'bin/llvm-objcopy.exe'),'-O','binary','--only-section=.text',str(d/'tuning.o'),str(d/'tuning.bin')],check=True)
blob=(d/'tuning.bin').read_bytes()
header=bytes(int(x,16) for x in re.findall(r'0x[0-9a-f]+',(m/'native/tuning.h').read_text()))
assert blob==header and len(blob)==68
wrapper=""".text
.global run_tuning
run_tuning:
 stp x24, x30, [sp, #-16]!
 sub sp, sp, #96
 mov w24, w1
 str s0, [sp, #0x3c]
 str s1, [sp, #0x38]
"""+(m/'native/tuning.S').read_text()+"""
 ldr s0, [sp, #0x3c]
 ldr s1, [sp, #0x38]
 stp s0, s1, [x0]
 add sp, sp, #96
 ldp x24, x30, [sp], #16
 ret
"""
(d/'test.S').write_text(wrapper);assemble(d/'test.S',d/'test-asm.o')
a=[str(t/'bin/clang++.exe'),'-ccc-install-dir',str(t/'bin'),'--target=aarch64-linux-android34',f'--sysroot={t/"sysroot"}','-resource-dir',str(t/'lib/clang/19')]
subprocess.run(a+['-O2','-Wall','-Wextra','-Werror','-c',str(m/'tests/tuning_test.cpp'),'-o',str(d/'test.o')],check=True)
subprocess.run(a+[f'--ld-path={t/"bin/ld.lld.exe"}','-static-libstdc++',str(d/'test.o'),str(d/'test-asm.o'),'-o',str(d/'test-tuning')],check=True)
print('Assembler/header match; ARM64 test built')
