import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import package_bc2vr as package


def peer(machine, control=6, **changes):
    data=bytearray(1024);data[:2]=b'MZ';struct.pack_into('<I',data,60,128)
    data[128:132]=b'PE\0\0';struct.pack_into('<HH',data,132,machine,1)
    struct.pack_into('<H',data,148,224);struct.pack_into('<H',data,152,0x10b)
    struct.pack_into('<IIII',data,384,512,4096,512,512)
    versions=dict(Control=control,Feedback=1,Menu=1,Pose=1,Texture=2);versions.update(changes)
    for channel,version in versions.items():
        if version is not None:data.extend((f'Local\\FrostbiteVR.{channel}.v{version}.\0').encode('utf-16-le'))
    return bytes(data)


def pair(native=None,host=None):
    return {'build/x86/BC2NativeProbe.dll':native or peer(0x14c),
            'build/x64/BC2XrHost.exe':host or peer(0x8664)}


class IpcCompatibility(unittest.TestCase):
    def test_same_protocol_accepts_independently_built_peers(self):
        native=peer(0x14c)+b'native commit A';host=peer(0x8664)+b'host commit B'
        self.assertEqual(package.validate_ipc_pair(pair(native,host)),
                         dict(Control=6,Feedback=1,Menu=1,Pose=1,Texture=2))
        self.assertEqual(package.validate_ipc_pair(pair(peer(0x14c,5),peer(0x8664,5)))['Control'],5)

    def test_v6_native_and_v5_host_are_rejected(self):
        with self.assertRaisesRegex(ValueError,'Control: native v6, host v5'):
            package.validate_ipc_pair(pair(host=peer(0x8664,5)))

    def test_audio_side_lane_requires_both_peers(self):
        marker='Local\\FrostbiteVR.Audio.v1.\0'.encode('utf-16-le')
        with self.assertRaisesRegex(ValueError,'Audio: native v1, host vmissing'):
            package.validate_ipc_pair(pair(native=peer(0x14c)+marker))
        versions=package.validate_ipc_pair(pair(native=peer(0x14c)+marker,host=peer(0x8664)+marker))
        self.assertEqual(versions['Audio'],1)

    def test_other_wire_contract_mismatch_is_rejected(self):
        with self.assertRaisesRegex(ValueError,'Pose: native v1, host v2'):
            package.validate_ipc_pair(pair(host=peer(0x8664,Pose=2)))

    def test_missing_ambiguous_and_ascii_only_markers_are_rejected(self):
        ambiguous=peer(0x8664)+('Local\\FrostbiteVR.Control.v5.\0').encode('utf-16-le')
        ascii_only=peer(0x8664,Control=None)+b'Local\\FrostbiteVR.Control.v6.\0'
        for host in (peer(0x8664,Control=None),ambiguous,ascii_only):
            with self.subTest(host=host[-90:]):
                with self.assertRaisesRegex(ValueError,'Missing or ambiguous Control'):
                    package.validate_ipc_pair(pair(host=host))

    def test_missing_peer_and_wrong_architecture_are_rejected(self):
        with self.assertRaisesRegex(ValueError,'Missing IPC peer'):
            package.validate_ipc_pair({'build/x86/BC2NativeProbe.dll':peer(0x14c)})
        with self.assertRaisesRegex(ValueError,'Wrong binary architecture'):
            package.validate_ipc_pair(pair(host=peer(0x14c)))


if __name__=='__main__':unittest.main()
