from pathlib import Path
import subprocess,sys
p=Path(__file__).resolve()
subprocess.run([sys.executable,str(p.parents[2]/"tools/build_suites.py"),p.parent.name,*sys.argv[1:]],check=True)
