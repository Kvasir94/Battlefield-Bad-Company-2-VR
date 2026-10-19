"""Convert an explicit reviewed native-action capture window to PRIVATE samples.

Requires the new exact configured-mesh/rig/phase evidence. Legacy unbound raw
weapon captures remain research inputs and cannot silently gain admission here.
No interpolation, native calls, game writes, or active-clip identity claims.
"""
import argparse, hashlib, json, math
from pathlib import Path
from bc2_native_action_pose_samples import header
from bc2_weapon_animation_pipeline import Skeleton, inverse_rigid, multiply


def need(ok, message):
    if not ok:
        raise ValueError(message)


def matrix(value, translation_limit=2):
    need(isinstance(value, list) and len(value) == 16, 'Matrix shape')
    need(all(isinstance(v, (int, float)) and math.isfinite(v) for v in value), 'Finite matrix')
    inverse_rigid(value)
    need(math.dist(value[12:15], [0, 0, 0]) <= translation_limit, 'Local translation bound')
    return value


def profile(report, report_sha, skeleton, spec):
    asset, mesh = spec['asset'], spec['mesh']
    begin, end = spec['begin_ms'], spec['end_ms']
    parts, driven = spec['parts'], spec['driven_part']
    stationary = spec['stationary_parts']
    need(spec['action'] in ('Rack', 'Pump'), 'Only explicitly reviewed mechanism windows')
    need(isinstance(spec.get('semantic_evidence'), str) and spec['semantic_evidence'].strip(), 'Reviewed semantic window evidence required')
    need(0 <= begin < end and end - begin <= 10000, 'Bounded explicit capture window')
    need(0 < len(parts) <= 32 and len(set(parts)) == len(parts) and driven in parts, 'Unique driven parts')
    need(0 < len(stationary) <= 8 and not set(parts).intersection(stationary), 'Explicit stationary reference parts')
    for bone in parts + stationary:
        need(bone in skeleton.names, 'Part absent from exact skeleton')
        parent = skeleton.parents[skeleton.names.index(bone)]
        while parent >= 0 and skeleton.names[parent] != 'jntWpn_1':
            parent = skeleton.parents[parent]
        need(parent >= 0, 'Part outside native weapon subtree')
    rig = int(skeleton.fingerprint.split(':')[1], 16)
    publication = report['gameplay']['rig_publication']
    need(publication['native_action_capture']['requested'] is True, 'Capture was not requested')
    rows = [r for r in publication['hand_evidence'] if r.get('native_action') and
            begin <= r['native_action']['captured_ms'] <= end]
    need(3 <= len(rows) <= 512, 'No complete bounded action window')
    frames, identity, last_time, last_generation, firing_identity = [], None, None, None, None
    stationary_positions = {name: [] for name in stationary}
    phase_set = set()
    for row in rows:
        action = row['native_action']; time = action['captured_ms']
        need(action['asset'] == asset and action['configured_mesh'] == mesh and action['rig_fingerprint'] == rig, 'Window crosses asset/configuration/rig')
        need(action['configured_mesh_binding_verified'] is True and
             action['source'] == 'native_evaluated_before_vr' and
             action['matrix_convention'] == 'canonical_row_vector_metres', 'Unverified source convention or configuration')
        current = (row['weapon'], row['owner_generation'], row['space'], action['equipment_generation'], action['animation_object'], action['pose_object'])
        need(all(current), 'Incomplete owner/pose identity')
        if identity is None:
            identity = current
        need(current == identity, 'Window crosses owner/equipment/pose incarnation')
        if last_time is not None:
            need(0 < time - last_time <= 150 and row['generation'] > last_generation, 'Noncontiguous or reordered original samples')
        last_time, last_generation = time, row['generation']
        observed = action['native_firing_observation']
        need(observed and observed['sequence'] <= row['generation'] and observed['observed_ns'] > 0, 'Missing native phase observation')
        branches = observed['branches']
        need(len(branches) == 2 and observed['branches_agree_on_phase'] is True, 'Unresolved native phase branches')
        phases = {b['phase'] for b in branches}
        need(len(phases) == 1 and phases.issubset({2, 6, 7, 8}), 'Window crosses unsupported native action phase')
        phase_set.update(phases)
        branch_ids = tuple((b['wrapper_offset'], b['address']) for b in branches)
        if firing_identity is None:
            firing_identity = branch_ids
        need(branch_ids == firing_identity and all(x[1] for x in branch_ids), 'Changed native firing copies')
        need(all(math.isfinite(b['phase_timer']) for b in branches), 'Native phase timer is not finite')
        fingers = action['native_left_fingers_in_wrist']
        need(len(fingers) == 15, 'Incomplete native fingers')
        wrist = matrix(action['native_left_wrist_in_weapon'])
        fingers = [matrix(f, .5) for f in fingers]
        # Verify the retained relative wrist against the same original world pose.
        actual = multiply(action['native_left_wrist'], inverse_rigid(action['native_weapon']))
        need(max(abs(a-b) for a, b in zip(actual, wrist)) < .0002, 'Native wrist reference mismatch')
        captured = action['native_action_parts']
        mapping = {p['bone']: p['part_in_weapon'] for p in captured}
        need(len(mapping) == len(captured) and all(b in mapping for b in parts + stationary), 'Missing/ambiguous native part')
        for name in stationary:
            stationary_positions[name].append(matrix(mapping[name])[12:15])
        frames.append(dict(time_seconds=(time-rows[0]['native_action']['captured_ms'])/1000,
                           captured_ms=time, input_generation=row['generation'], native_phase=next(iter(phases)),
                           native_observed_ns=observed['observed_ns'], wrist_in_weapon=wrist,
                           fingers_in_wrist=fingers, parts_in_weapon=[matrix(mapping[b]) for b in parts]))
    need(phase_set.intersection({6, 7, 8}), 'No actual native reload phase in reviewed window')
    stationary_motion = {name: max(math.dist(p, positions[0]) for p in positions) for name, positions in stationary_positions.items()}
    need(all(v <= .002 for v in stationary_motion.values()), 'Reference part still moving: not an isolated rack window')
    index = parts.index(driven); origin = frames[0]['parts_in_weapon'][index][12:15]
    distances = [math.dist(f['parts_in_weapon'][index][12:15], origin) for f in frames]
    peak = max(range(len(frames)), key=distances.__getitem__); stroke = distances[peak]
    need(.005 <= stroke <= .5 and 0 < peak < len(frames)-1, 'No bounded complete original back/return stroke')
    need(distances[-1] <= max(.002, stroke*.1), 'Native return does not reach observed closed reference')
    for n, frame in enumerate(frames):
        frame.update(progress=distances[n]/stroke, returning=n > peak)
    return dict(asset_name=asset, mesh_path=mesh, rig_fingerprint=skeleton.fingerprint,
                clip_sha256='', skeleton_sha256=skeleton.sha256, source_kind='EffectiveNativeCapture',
                capture_sha256=report_sha, action=spec['action'], part_bones=parts, frames=frames,
                frame_count=len(frames), native_part_stroke_m=stroke, peak_frame=peak,
                stationary_motion_m=stationary_motion, semantic_window_ms=[begin, end],
                semantic_evidence=spec['semantic_evidence'], capture_identity=list(identity),
                native_firing_identity=firing_identity, active_clip_known=False,
                runtime_frame_policy='nearest_original_sample; no interpolation')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('report', 'skeleton', 'spec', 'output', 'header'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--base', type=Path, help='Existing private sample catalog; duplicate action identities reject')
    args = parser.parse_args()
    need(args.report.stat().st_size <= 256*1024*1024, 'Report size bound')
    raw = args.report.read_bytes(); report = json.loads(raw); sha = hashlib.sha256(raw).hexdigest()
    skeleton = Skeleton(args.skeleton.read_bytes()); specs = json.loads(args.spec.read_text())
    need(0 < len(specs) <= 16, 'Spec count bound')
    profiles = json.loads(args.base.read_text())['profiles'] if args.base else []
    added = [profile(report, sha, skeleton, spec) for spec in specs]
    profiles += added
    identities = [(p['asset_name'], p['mesh_path'], p['rig_fingerprint'], p['action']) for p in profiles]
    need(len(identities) <= 256 and len(set(identities)) == len(identities), 'Duplicate action identity; no silent replacement')
    result = dict(schema='fvr.bc2.native_action_pose_samples', profiles=profiles, capture_report_sha256=sha)
    need(not args.output.exists() and not args.header.exists(), 'Preserve existing private outputs')
    args.output.parent.mkdir(parents=True, exist_ok=True); args.header.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, separators=(',', ':')), encoding='utf-8')
    args.header.write_text(header(result), encoding='utf-8')
    print(json.dumps(dict(added_profiles=len(added), added_frames=sum(p['frame_count'] for p in added),
                         total_profiles=len(profiles), report_sha256=sha, game_calls=False)))


if __name__ == '__main__':
    main()
