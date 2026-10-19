import json,sys,os,re,hashlib,subprocess
S=os.path.dirname(os.path.abspath(__file__))
for line in open(sys.argv[1]):
    n,t,rx=line.split();t=os.path.join(sys.argv[2],t);rx=os.path.join(sys.argv[2],rx)
    txt=open(os.path.join(t,'native-trace.json')).read();d=json.loads(txt);g=d['gameplay'];a=g['reload_flow']['native_abort_cleanup']
    r=json.loads(subprocess.run([sys.executable,os.path.join(S,'spas_report.py'),t],capture_output=True,text=True).stdout)
    dll=hashlib.sha256(open(os.path.join(t,'BC2NativeProbe.dll'),'rb').read()).hexdigest()[:12].upper()
    w={k:(re.findall(r'"%s":(\d+)'%k,txt) or ['-'])[0] for k in ('spas_submit_failure','submit_retries','submit_retried','submit_retry_expired','deferred_waits','held_gap_waits','evaluate_contention_skips')}
    ev=[json.loads(l) for l in open(os.path.join(t,'gun-events.jsonl'))];t0=ev[0]['qpc_us']
    cr=[round((e['qpc_us']-t0)/1000) for e in ev if e['phase']=='credit'];ends=[e for e in ev if e['phase']=='end']
    en=[(round((e['qpc_us']-t0)/1000),e.get('reason')) for e in ends]
    tr_after=[(round((e['qpc_us']-t0)/1000),e.get('copy')) for e in ev if e['phase']=='transfer' and ends and e['qpc_us']>ends[0]['qpc_us']]
    ip=json.load(open(os.path.join(rx,'result.json'))).get('input_publish') or {}
    print(n.ljust(15),os.path.basename(t)[-19:],dll,'phase',r['probe']['phase'],'fail',r['probe']['failure'],'cancels',r['cancels'],'credits',cr,'end',en,'loaded',r['last_loaded'],
          'after',tr_after,'abort',a.get('claimed_mask'),a.get('completed_mask'),a.get('failure'),'rebases',a.get('stall_rebases'),w,'inmax',ip.get('max_gap_ms'))
