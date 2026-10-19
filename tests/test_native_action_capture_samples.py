"""Synthetic admission checks; no game file, native process or build required."""
import copy
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from bc2_native_action_capture_samples import profile
from bc2_native_action_pose_samples import header


def at(x=0):
    return [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, 0, 0, 1]


class CaptureAdmission(unittest.TestCase):
    def setUp(self):
        self.skeleton = SimpleNamespace(names=['root', 'jntWpn_1', 'handle', 'mag'],
            parents=[-1, 0, 1, 1], fingerprint='fnv1a64:0000000000000007', sha256='b'*64)
        self.spec = dict(asset='gun', mesh='mesh', begin_ms=1000, end_ms=1040,
            parts=['handle'], driven_part='handle', stationary_parts=['mag'],
            action='Rack', semantic_evidence='Synthetic complete back/return after stationary magazine')
        self.rows = []
        for n, position in enumerate([0, .04, 0]):
            action = dict(asset='gun', configured_mesh='mesh', rig_fingerprint=7,
                captured_ms=1000+n*20, equipment_generation=1, animation_object=2, pose_object=3,
                configured_mesh_binding_verified=True, source='native_evaluated_before_vr',
                matrix_convention='canonical_row_vector_metres', native_left_wrist_in_weapon=at(),
                native_left_wrist=at(), native_weapon=at(), native_left_fingers_in_wrist=[at() for _ in range(15)],
                native_action_parts=[dict(bone='handle', part_in_weapon=at(position)), dict(bone='mag', part_in_weapon=at())],
                native_firing_observation=dict(sequence=n+1, observed_ns=100+n,
                    branches_agree_on_phase=True, branches=[dict(wrapper_offset=i, address=i+100,
                    phase=7, phase_timer=.5) for i in [60, 64]]))
            self.rows.append(dict(weapon=4, owner_generation=1, space=1, generation=n+1, native_action=action))
        self.report = dict(gameplay=dict(rig_publication=dict(native_action_capture=dict(requested=True), hand_evidence=self.rows)))

    def run_profile(self):
        return profile(self.report, 'a'*64, self.skeleton, self.spec)

    def test_original_samples_and_truthful_provenance(self):
        result = self.run_profile()
        self.assertEqual(result['source_kind'], 'EffectiveNativeCapture')
        self.assertEqual(result['clip_sha256'], '')
        self.assertEqual(result['capture_sha256'], 'a'*64)
        self.assertEqual([f['progress'] for f in result['frames']], [0, 1, 0])
        self.assertEqual([f['returning'] for f in result['frames']], [False, False, True])
        self.assertEqual(result['frames'][1]['parts_in_weapon'], [at(.04)])
        self.assertIn('NativeActionPoseSource::EffectiveNativeCapture', header(dict(profiles=[result])))

    def test_rejects_owner_configuration_and_sample_gaps(self):
        for field, value in [('configured_mesh', 'other'), ('equipment_generation', 2),
                             ('rig_fingerprint', 8), ('configured_mesh_binding_verified', False)]:
            with self.subTest(field=field):
                original = self.rows[1]['native_action'][field]
                self.rows[1]['native_action'][field] = value
                with self.assertRaises(ValueError): self.run_profile()
                self.rows[1]['native_action'][field] = original
        self.rows[1]['generation'] = 1
        with self.assertRaises(ValueError): self.run_profile()

    def test_rejects_unobserved_reload_and_incomplete_geometry(self):
        for row in self.rows:
            for branch in row['native_action']['native_firing_observation']['branches']: branch['phase'] = 2
        with self.assertRaises(ValueError): self.run_profile()
        self.setUp(); self.rows[1]['native_action']['native_left_fingers_in_wrist'].pop()
        with self.assertRaises(ValueError): self.run_profile()
        self.setUp(); self.rows[1]['native_action']['native_action_parts'][1]['part_in_weapon'] = at(.02)
        with self.assertRaises(ValueError): self.run_profile()
        self.setUp(); self.rows[2]['native_action']['native_action_parts'][0]['part_in_weapon'] = at(.03)
        with self.assertRaises(ValueError): self.run_profile()


if __name__ == '__main__': unittest.main()
