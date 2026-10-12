"""Build the automatic-only, installed-disabled HAL cache bypass package."""
from pathlib import Path
import argparse,subprocess,sys,os
HERE=Path(__file__).resolve().parent
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--ndk',default=os.environ.get('ANDROID_NDK_HOME'))
args=parser.parse_args()
if not args.ndk:parser.error('Set ANDROID_NDK_HOME or pass --ndk')
subprocess.run([sys.executable,str(HERE.parents[1]/'tools/build.py'),'hal-cache-bypass','--ndk',args.ndk,'--variant','auto'],check=True)
