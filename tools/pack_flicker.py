"""Per-pack rig pose time series from gun-events.jsonl (phase "pack").

Each native palette pack row says whether the posed rig went in (posed) and,
for a first pack that did not, why (miss):
  1 pose lock busy, 2 tracking stale, 3 nothing published, 4 published for
  another weapon/space/owner/equipment, 5 shot guard/pose deadline/identity,
  6 native source unreadable, 7 native palette moved since the pose (with
  posed true: rebased onto the current native palette), 8/9 the last good
  pose held through a busy lock / stale tracking (posed true).
magazine: 1 posed magazine, 2 magazine fell back to the ordinary palette.

Prints totals, the miss histogram, posed<->native toggles, and the same inside
reload windows (begin .. complete/cancel/end rows), plus the longest runs.
"""
from __future__ import annotations
import argparse,collections,json
from pathlib import Path

def load(path):
    rows=[]
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        if line.strip():rows.append(json.loads(line))
    return rows

def windows(rows):
    out=[];start=None
    for r in rows:
        p=r.get('phase')
        if p=='begin' and start is None:start=r['tick']
        elif p in ('complete','cancel','end') and start is not None:out.append((start,r['tick']));start=None
    return out

def summary(packs,label):
    firsts=[r for r in packs if r.get('pack')==1]
    posed=sum(1 for r in packs if r.get('posed'))
    miss=collections.Counter(r.get('miss',0) for r in firsts if not r.get('posed'))
    kept=collections.Counter(r.get('miss',0) for r in firsts if r.get('posed') and r.get('miss',0))
    mag=collections.Counter(r.get('magazine',0) for r in packs)
    toggles=sum(1 for a,b in zip(packs,packs[1:]) if a.get('posed')!=b.get('posed'))
    pair_split=0;by_draw=collections.defaultdict(list)
    for r in packs:by_draw[r.get('draw')].append(r)
    for rs in by_draw.values():
        if len({bool(r.get('posed')) for r in rs})>1 or len({r.get('magazine') for r in rs})>1:pair_split+=1
    print(f'{label}: packs {len(packs)} posed {posed} native {len(packs)-posed} toggles {toggles} draws_split {pair_split}')
    print(f'  first-pack miss reasons {dict(sorted(miss.items()))}  kept posed (rebased/held) {dict(sorted(kept.items()))}  magazine {dict(sorted(mag.items()))}')
    run=best=0;best_at=None
    for r in packs:
        if r.get('posed'):run=0
        else:
            run+=1
            if run>best:best=run;best_at=r['tick']
    if best:print(f'  longest native run {best} packs ending tick {best_at}')

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('events',type=Path)
    a=p.parse_args(argv);rows=load(a.events)
    packs=[r for r in rows if r.get('phase')=='pack']
    if not packs:print('no pack rows (DLL older than the pack audit?)');return 1
    summary(packs,'all')
    for n,(s,e) in enumerate(windows(rows)):
        summary([r for r in packs if s<=r['tick']<=e],f'reload {n} ticks {s}-{e}')
    return 0

if __name__=='__main__':raise SystemExit(main())
