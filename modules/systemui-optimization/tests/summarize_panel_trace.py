"""Summarize real enable transitions, selecting the valid libc++ string storage."""
import collections,json,re,sys
from pathlib import Path
pattern=re.compile(r"(\d+\.\d+): set_enabled: .*? enabled=(\d+) caller=(0x[0-9a-f]+) flags=(0x[0-9a-f]+) name_meta=(0x[0-9a-f]+) name_inline=\"(.*?)\" name_heap=(.*)")
for path in map(Path,sys.argv[1:]):
    counts=collections.defaultdict(collections.Counter)
    changes=collections.defaultdict(list)
    for line in path.read_text(errors="replace").splitlines():
        m=pattern.search(line)
        if not m:continue
        stamp,new,caller,flags,meta,inline,heap=m.groups()
        name=heap.strip('"') if int(meta,16)&1 else inline
        if not any(t in name for t in ("MIRROR_","NOTIFICATION","HEADS_UP","HEADSUP")):continue
        old=int(flags,16)&1; new=int(new)
        counts[name][f"{old}->{new}"]+=1
        if old!=new:changes[name].append([float(stamp),old,new])
    result={name:{"calls":dict(c),"changes":changes[name][:20]} for name,c in counts.items()}
    path.with_name("summary.json").write_text(json.dumps(result,indent=2))
    print(path.parent.name,json.dumps(result,indent=2))
