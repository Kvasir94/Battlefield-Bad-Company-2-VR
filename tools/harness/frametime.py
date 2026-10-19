import json,sys,os
# argv: pairs of trace-folder rx-folder. Native frame rate from the receiver's pairs (native_frame vs elapsed_ms).
a=sys.argv[1:]
for run,rx in zip(a[::2],a[1::2]):
    P=[json.loads(l) for l in open(os.path.join(rx,'pairs.jsonl')) if '"elapsed_ms"' in l]
    P=[p for p in P if p['elapsed_ms']>=3000]  # skip load-in
    fps=(P[-1]['native_frame']-P[0]['native_frame'])/((P[-1]['elapsed_ms']-P[0]['elapsed_ms'])/1000)
    d=json.load(open(os.path.join(run,'native-trace.json')));gp=d.get('ground_probe') or {}
    print(os.path.basename(run),'native fps %.1f (%.2f ms/frame) over %.1f s'%(fps,1000/fps,(P[-1]['elapsed_ms']-P[0]['elapsed_ms'])/1000),'probe',gp.get('enabled'),{k:gp.get(k) for k in ('published','busy','rejected','cost_calls','cost_mean_us','cost_max_us')})
