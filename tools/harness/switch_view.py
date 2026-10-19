import json, sys, os, hashlib
trace = sys.argv[1]
d = json.load(open(os.path.join(trace, 'native-trace.json')))
g = d['gameplay']
ev = [json.loads(l) for l in open(os.path.join(trace, 'gun-events.jsonl'))] if os.path.exists(os.path.join(trace, 'gun-events.jsonl')) else []
t0 = ev[0]['qpc_us'] if ev else 0
dll = os.path.join(trace, 'BC2NativeProbe.dll')
print(os.path.basename(trace), hashlib.sha256(open(dll, 'rb').read()).hexdigest()[:12].upper() if os.path.exists(dll) else '-')
print('weapon_switch', g.get('weapon_switch'), 'next/prev', g.get('next_weapon_commands'), g.get('previous_weapon_commands'))
cs = (g.get('reload_flow') or {}).get('closed_empty_gate') or {}
print('gate', {k: cs.get(k) for k in ('applied', 'identity_misses', 'state_misses')}, 'leaks', ((g.get('reload_flow') or {}).get('empty_gate_leaks') or {}).get('total'))
for e in ev:
    ph = e['phase']
    if ph in ('weapon_switch', 'transfer', 'credit', 'begin') or (ph == 'firing_state' and e.get('copy') == 'server' and e.get('to') in (10, 11, 12)):
        print(round((e['qpc_us'] - t0) / 1000), ph, e.get('copy'), (e.get('weapon') or {}).get('asset'), {k: e.get(k) for k in ('from', 'to', 'loaded_before', 'loaded_after') if k in e})
last = {}
for e in ev:
    if e['phase'] == 'sample' and e.get('copy') == 'server':
        a = (e.get('weapon') or {}).get('asset'); t = round((e['qpc_us'] - t0) / 1000)
        if last.get('a') != a: print('  sample asset', t, a, e.get('loaded')); last['a'] = a
