import json,sys,os,re
sys.path.insert(0,os.path.dirname(__file__))
for run in sys.argv[1:]:
    txt=open(os.path.join(run,'native-trace.json')).read()
    w={k:re.findall(r'"%s":(\d+)'%k,txt) for k in ('deferred_waits','held_gap_waits','pending_gap_waits')}
    ev=[json.loads(l) for l in open(os.path.join(run,'gun-events.jsonl'))];t0=ev[0]['qpc_us']
    print(' waits',w)
    end=None
    for e in ev:
        t=round((e['qpc_us']-t0)/1000)
        if e['phase'] in ('transfer','credit','end','cancel','complete') or (e['phase']=='firing_state' and t>9500 and e.get('to') in (1,2,12)):
            if e['phase']=='firing_state' and e.get('copy')!='server' and e.get('to')!=12:continue
            print('  ',t,e['phase'],e.get('copy'),{k:e.get(k) for k in ('from','to','loaded_before','loaded_after','reason') if k in e})
