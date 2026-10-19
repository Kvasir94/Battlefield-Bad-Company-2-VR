import json, sys, os, hashlib
trace = sys.argv[1]
d = json.load(open(os.path.join(trace, 'native-trace.json')))
g = d['gameplay']; rf = g['reload_flow']
probe = g.get('physical_reload_probe') or {}
ev = [json.loads(l) for l in open(os.path.join(trace, 'gun-events.jsonl'))]
t0 = ev[0]['qpc_us']
t = lambda e: round((e['qpc_us'] - t0) / 1000)
dll = os.path.join(trace, 'BC2NativeProbe.dll')
samples = [e for e in ev if e['phase'] == 'sample' and e.get('copy') == 'server']
out = {
  'run': os.path.basename(trace), 'dll': hashlib.sha256(open(dll, 'rb').read()).hexdigest()[:12].upper() if os.path.exists(dll) else None,
  'probe': {k: probe.get(k) for k in ('phase', 'failure', 'submitted', 'completed', 'requested_rounds', 'loaded_before', 'forced_cancel', 'forced_at_ns', 'fire_after', 'fire_until_ns')},
  'pump_carry_waits': (g.get('physical_reload') or {}).get('pump_carry_waits'),
  'idle_source_gaps': (g.get('physical_reload') or {}).get('idle_source_gaps'),
  'supply_freeze': (lambda sa: {k: sa.get(k) for k in ('freeze_trigger','freeze_now_ns','frozen','frozen_skipped')} | {'rows': [{k: r.get(k) for k in ('now_ns','supply_reason','cancel_reason','grip_pressed','inside_primary','shell_held','supply_source','reserve_current','reload_input_ready','all_three_held','raw_valid')} for r in sa.get('rows', [])[-8:]] if sa.get('frozen') else []})((g.get('physical_reload') or {}).get('supply_availability') or {}),
  'start_retries': (g.get('physical_reload') or {}).get('start_retries'), 'stock_fallback': g.get('stock_fallback'), 'cancels': [r.get('reason_name') for r in ((g.get('physical_reload') or {}).get('cancellation_evidence') or {}).get('rows', [])],
  'pending_gap_waits': (g.get('physical_reload') or {}).get('pending_gap_waits'), 'spas_submit_failure': ((rf.get('request_cycle') or {}).get('spas_submit_failure')),
  'leaks': (rf.get('empty_gate_leaks') or {}).get('total'),
  'cancel_stop': rf.get('cancel_stop'),
  'abort': {k: (rf.get('native_abort_cleanup') or {}).get(k) for k in ('armed', 'calls', 'claimed_mask', 'completed_mask', 'failure')},
  'first_loaded': samples[0].get('loaded') if samples else None, 'last_loaded': samples[-1].get('loaded') if samples else None,
  'rows': [(t(e), e['phase'], e.get('copy'), e.get('from'), e.get('to'), e.get('loaded_before'), e.get('loaded_after'), e.get('reason'), e.get('result'))
           for e in ev if e['phase'] in ('begin', 'seat', 'credit', 'cancel', 'end', 'complete', 'transfer', 'freeze_stop', 'pouch_start')
           or (e['phase'] == 'firing_state' and e.get('copy') == 'server' and e.get('to') in (1, 2, 10, 11, 12))
           or (e['phase'] == 'fire' and e.get('copy') == 'server')][:80],
}
print(json.dumps(out))
