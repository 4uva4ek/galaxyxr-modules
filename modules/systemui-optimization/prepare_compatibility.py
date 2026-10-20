"""Generate local compatibility signatures from the user's pinned vendor image."""
import argparse,hashlib
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument("--library",type=Path,required=True)
a=p.parse_args()
blob=a.library.read_bytes()
assert hashlib.sha256(blob).hexdigest()=="ab04d6daf57800a42170febaf33cb128441339c06bd6d46f068c0109693fee42"
native=Path(__file__).resolve().parent/"native"
for name,start,end in (
    ("expected.h",0x104b974,0x104ba8c),
    ("wait_expected.h",0x104afe8,0x104b070),
    ("schema_expected.h",0xa04598,0xa04968)):
    data=blob[start:end]
    (native/name).write_text("static constexpr unsigned char EXPECTED[] = {"+
        ",".join(f"0x{b:02x}" for b in data)+"};\n",encoding="ascii",newline="\n")
print("Generated local pinned signatures; do not publish these headers.")
