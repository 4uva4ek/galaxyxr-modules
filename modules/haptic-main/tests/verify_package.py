from pathlib import Path
import sys

module = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(module.parents[1]/'tools'))
from verify_variants import verify

verify(module)
