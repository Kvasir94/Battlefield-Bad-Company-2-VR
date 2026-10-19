import json,sys,os,hashlib
for run in sys.argv[1:]:
    d=json.load(open(os.path.join(run,'native-trace.json')));g=d['gameplay'];a=g['reload_flow']['native_abort_cleanup']
    dll=os.path.join(run,'BC2NativeProbe.dll')
    print('==',os.path.basename(run),hashlib.sha256(open(dll,'rb').read()).hexdigest()[:12].upper() if os.path.exists(dll) else '-')
    h=g.get('test_hitch');print(' hitch',h)
    print(' abort',{k:a.get(k) for k in ('armed','calls','claimed_mask','completed_mask','failure','stall_rebases','deadline_ns')})
    c=a['cycle_history'][0];print(' cycle',{k:c.get(k) for k in ('armed_ns','deadline_ns','terminal_ns','claims','claimed_mask','completed_mask','failure','last_admission')})
    for r in a['records']:print('  rec branch',r['branch'],'begin',r['begin_ns'],'rel_arm_ms',round((r['begin_ns']-c['armed_ns'])/1e6,2),'rel_hitch_end_ms',round((r['begin_ns']-(h or {}).get('end_qpc_ns',0))/1e6,2),'completed',r['completed'],r['before']['loaded'],'->',r['after']['loaded'],r['before']['current'],'->',r['after']['current'])
    if h and h.get('begin_qpc_ns'):print('  hitch begin rel_arm_ms',round((h['begin_qpc_ns']-c['armed_ns'])/1e6,2),'end rel_arm_ms',round((h['end_qpc_ns']-c['armed_ns'])/1e6,2))
    ev=[json.loads(l) for l in open(os.path.join(run,'gun-events.jsonl'))];t0=ev[0]['qpc_us']
    ends=[e for e in ev if e['phase'] in ('end',)];te=ends[0]['qpc_us'] if ends else t0
    print('  end',round((te-t0)/1000),ends[0].get('reason') if ends else None)
    for e in ev:
        if e['qpc_us']>=te-100000 and e['qpc_us']<te+3000000 and (e['phase']=='transfer' or e['phase']=='firing_state'):
            print('   ',round((e['qpc_us']-t0)/1000),e['phase'],e.get('copy'),{k:e.get(k) for k in ('from','to','loaded_before','loaded_after') if k in e})
