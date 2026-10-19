# One-line verdict per run for the fail-closed empty gate tests.
# usage: gate_report.py TRACE [RX]
import json, sys, os, collections
trace = sys.argv[1]; rx = sys.argv[2] if len(sys.argv) > 2 else None
d = json.load(open(os.path.join(trace, 'native-trace.json')))
g = d['gameplay']; rf = g['reload_flow']
ceg = rf.get('closed_empty_gate', {}); leaks = rf.get('empty_gate_leaks', {})
p = g.get('magazine_physical_probe', {})
ev = [json.loads(l) for l in open(os.path.join(trace, 'gun-events.jsonl'))]
t0 = ev[0]['qpc_us']
def t(e): return round((e['qpc_us'] - t0) / 1000)
dry = collections.Counter(e.get('copy') for e in ev if e['phase'] == 'fire' and e.get('result') == 'dry')
autos = [(t(e), e.get('copy'), e.get('suppressed')) for e in ev if e['phase'] == 'auto_reload']
seat = next((t(e) for e in ev if e['phase'] == 'seat'), None)
transfers = [(t(e), e.get('copy'), e.get('loaded_before'), e.get('loaded_after')) for e in ev if e['phase'] == 'transfer']
early = [x for x in transfers if seat is None or x[0] < seat]
begins = [t(e) for e in ev if e['phase'] == 'begin']
complete = any(e['phase'] == 'complete' for e in ev)
rp = g.get('rig_publication', {}).get('pump_part', {})
hints = g.get('reload_hints', {})
import hashlib
_dll = os.path.join(trace, 'BC2NativeProbe.dll')
out = {
  'run': os.path.basename(trace), 'dll': hashlib.sha256(open(_dll, 'rb').read()).hexdigest()[:12].upper() if os.path.exists(_dll) else None, 'asset': p.get('asset'), 'probe_phase': p.get('phase'), 'probe_failure': p.get('failure'),
  'gate_enabled': ceg.get('enabled'), 'applied': ceg.get('applied'), 'token_passes': ceg.get('token_passes'),
  'unsupported_passes': ceg.get('unsupported_passes'), 'identity_misses': ceg.get('identity_misses'),
  'context_misses': ceg.get('context_misses'), 'restore_failures': ceg.get('restore_failures'),
  'leaks': leaks.get('total'), 'leak_masks': sorted({r.get('mask') for r in leaks.get('rows', [])}),
  'stale_owner_holds': g.get('stale_owner_holds'), 'test_hitch': g.get('test_hitch'), 'token_rows': rf.get('token_passes'), 'unsupported_rows': rf.get('unsupported_passes'),
  'native_10_without_begin': [(t(e), e.get('copy')) for e in ev if e['phase'] == 'firing_state' and e.get('to') in (10, 11) and e.get('from') == 2 and not any(b <= t(e) for b in [t(x) for x in ev if x['phase'] == 'begin'])][:6], 'dry_rows_per_copy': dict(dry), 'auto_reload': autos[:6],
  'begins_ms': begins, 'seat_ms': seat, 'transfers': transfers, 'transfer_before_seat': early, 'complete': complete,
  'pickup_eases': rp.get('pickup_eases'), 'pickup_ease_misses': rp.get('pickup_ease_misses'),
  'glow_frames': hints.get('magazine_glow_frames'), 'ghost_frames': hints.get('magwell_ghost_frames'),
  'rack_rows': [(t(e), e.get('by_grip')) for e in ev if e['phase'] == 'rack'], 'bolt_releases': sum(1 for e in ev if e['phase'] == 'rack' and e.get('by_grip') is False),
  'seat': {k: (g.get('magazine_physical') or {}).get(k) for k in ('seat_deferrals','seat_retry_submits','seat_missing','cancel_cause','cancelled','held_read_gap_rides','reserve_cancel_why')},
  'copy_authority': rf.get('copy_authority'),
  'client1_12': [(t(e), e.get('from')) for e in ev if e['phase'] == 'firing_state' and e.get('copy') == 'client1' and e.get('to') == 12][:3],
  'chamber_rows': [(t(e), e.get('event'), e.get('display_ammo')) for e in ev if e['phase'] == 'chamber'][:12],
  'abort_cleanup': rf.get('native_abort_cleanup'), 'cancel_stop': rf.get('cancel_stop'), 'cancel_sites': g.get('physical_cancel_sites'),
  'forced': {k: p.get(k) for k in ('forced_cancel','forced_at_ns','phase','failure')},
  'native_rows': [(t(e), e['phase'], e.get('copy'), e.get('from'), e.get('to'), e.get('reason')) for e in ev if (e['phase']=='firing_state' and e.get('to') in (1,2,10,11,12) and e.get('from') in (10,11,12,1)) or e['phase'] in ('freeze_start','freeze_stop','begin','cancel','unseat','transfer')][:30],
  'closed': ceg, 'stock_fallback': g.get('stock_fallback'),
  'mask_131072_rows': sum(1 for r in leaks.get('rows', []) if (r.get('mask') or 0) & 131072),
}
if rx and os.path.exists(os.path.join(rx, 'result.json')):
    r = json.load(open(os.path.join(rx, 'result.json'))); out['input_gap'] = r.get('input_gap')
    pr = [json.loads(l) for l in open(os.path.join(rx, 'pairs.jsonl')) if '"pair"' in l]
    out['rx_stalls'] = [(a['pair'], b['elapsed_ms'] - a['elapsed_ms']) for a, b in zip(pr, pr[1:]) if b['elapsed_ms'] - a['elapsed_ms'] > 400]
print(json.dumps(out))
