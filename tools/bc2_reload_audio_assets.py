"""Extract original XM8/SPAS audio from the user's BC2 install into a local cache.

Only the observed mono 48 kHz EA AudioCore v1/XAS1 resources are accepted.
FFmpeg performs codec decoding; no game audio is included in this repository.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import wave
from inspect_bc2_mesh_asset import Archive

RESOURCES = {
    'xm8_reload': 'Sound/Weapons/Handheld/US_rif_XM8/US_rif_XM8_reload_1p_Wave.res',
    'spas_pump': 'Sound/Weapons/Handheld/UL_shg_SPAS12/Spas-12_Pump_1p_Wave.res',
    **{f'spas_shell_{i}': f'Sound/Weapons/Handheld/UL_shg_SPAS12/Spas-12_reload_{i}_1p_Wave.res' for i in (1, 2, 3)},
}

# Calibration cuts in the original waveform, in seconds. These are explicit
# and reviewable; waveform/animation alignment is not an audible gameplay test.
CUTS = {
    'xm8_mag_out': ('xm8_reload', .390, .820),
    'xm8_mag_in': ('xm8_reload', 1.400, 1.900),
    'xm8_handle_back': ('xm8_reload', 2.005, 2.120),
    'xm8_handle_forward': ('xm8_reload', 2.120, 2.360),
    # The .355-.360 valley is below -56 dBFS RMS in the decoded source.
    # Split there to retain the rear stroke's tail and the small forward onset
    # at .360, before the forward stroke's main burst around .370.
    'spas_pump_back': ('spas_pump', .145, .355),
    'spas_pump_forward': ('spas_pump', .355, .601),
    **{f'spas_shell_{i}_cue': (f'spas_shell_{i}', .275, .600) for i in (1, 2, 3)},
}

def write_wave(path, raw):
    with wave.open(str(path), 'wb') as writer:
        writer.setparams((1, 2, 48000, len(raw)//2, 'NONE', 'not compressed'))
        writer.writeframes(raw)

def prepare_cues(output):
    records = []
    for name, (source, begin, end) in CUTS.items():
        with wave.open(str(output/(source+'.wav')), 'rb') as reader:
            if reader.getparams()[:3] != (1, 2, 48000):
                raise ValueError('Unexpected source PCM format')
            first, last = round(begin*48000), round(end*48000)
            if not 0 <= first < last <= reader.getnframes():
                raise ValueError('Cue cut exceeds source')
            reader.setpos(first)
            raw = bytearray(reader.readframes(last-first))
        # One millisecond endpoint fade avoids a cut-induced impulse. Interior
        # samples retain the decoded original amplitude and timing.
        fade = min(48, (last-first)//2)
        for i in range(fade):
            for sample in (i, last-first-1-i):
                value = struct.unpack_from('<h', raw, sample*2)[0]
                struct.pack_into('<h', raw, sample*2, round(value*i/fade))
        write_wave(output/(name+'.wav'), raw)
        records.append(dict(cue=name, source=source, resource=RESOURCES[source],
                            first_sample=first, last_sample=last,
                            duration_seconds=(last-first)/48000,
                            pcm_sha256=hashlib.sha256(raw).hexdigest()))
    (output/'cue-manifest.json').write_text(json.dumps(dict(
        calibration='waveform and source-animation alignment; audible event attribution unverified',
        sample_rate=48000, fade_samples=48, amplitude_gain=1.0,
        sample_bounds='first inclusive, last exclusive', records=records), indent=2)+'\n')

def xas_frames(data):
    if len(data) < 48 or data[:4] != bytes.fromhex('00000300'):
        raise ValueError('Unsupported BC2 Wave resource')
    count = struct.unpack_from('<I', data, 4)[0]
    if struct.unpack_from('<I', data, 28)[0] != len(data)-40 or data[-8:] != bytes(8):
        raise ValueError('Wave payload size mismatch')
    data = data[:-8]
    if data[32:36] != bytes.fromhex('4800000c'):
        raise ValueError('Missing AudioCore v1 header')
    h1, h2 = struct.unpack_from('>II', data, 36)
    if h1 != 0x1400bb80 or h2 >> 29 != 2 or h2 & 0x1fffffff != count:
        raise ValueError('Expected non-looped mono 48 kHz XAS1 stream')
    if not 0 < count <= 48000*10:
        raise ValueError('Excessive sample count')
    cursor, frames, total, partial = 44, [], 0, False
    while cursor < len(data):
        size = int.from_bytes(data[cursor:cursor+4], 'big') & 0xffffff
        kind = data[cursor]
        if size < 4 or cursor+size > len(data):
            raise ValueError('Audio block bounds')
        if kind == 0x45:
            if size != 4 or cursor+size != len(data):
                raise ValueError('Invalid stream end')
        elif kind == 0x44:
            if partial:
                raise ValueError('Only the final XAS block may contain padding')
            if size < 8 or (size-8) % 76:
                raise ValueError('Invalid XAS frame alignment')
            samples = int.from_bytes(data[cursor+4:cursor+8], 'big')
            capacity = (size-8)//76*128
            if not 0 < samples <= capacity or capacity-samples >= 128:
                raise ValueError('Invalid XAS block sample count')
            frames.append(data[cursor+8:cursor+size])
            total += samples
            partial = samples != capacity
        else:
            raise ValueError(f'Unsupported audio block {kind:#x}')
        cursor += size
    if total != count:
        raise ValueError('Decoded sample count disagrees with resource')
    return count, b''.join(frames)

def extract(archive_path, output, ffmpeg):
    archive = Archive(archive_path)
    resources = archive.read_selected(RESOURCES.values())
    output.mkdir(parents=True, exist_ok=True)
    records = []
    with tempfile.TemporaryDirectory(prefix='bc2-audio-') as temporary:
        for name, resource in RESOURCES.items():
            source = resources[resource]
            count, frames = xas_frames(source)
            cdata = Path(temporary)/'source.cdata'
            cdata.write_bytes(bytes.fromhex('0400bb80') + bytes(12) + frames)
            raw = subprocess.run([ffmpeg, '-v', 'error', '-f', 'ea_cdata', '-i', str(cdata),
                                  '-f', 's16le', '-acodec', 'pcm_s16le', '-'], check=True, capture_output=True).stdout
            if len(raw) < count*2 or len(raw)-count*2 >= 256:
                raise ValueError('FFmpeg sample count mismatch')
            raw = raw[:count*2]
            write_wave(output/(name+'.wav'), raw)
            records.append(dict(name=name, resource=resource, samples=count,
                                source_sha256=hashlib.sha256(source).hexdigest(),
                                pcm_sha256=hashlib.sha256(raw).hexdigest()))
    manifest = dict(archive=str(archive_path), index_sha256=archive.index_sha256,
                    sample_rate=48000, channels=1, records=records)
    (output/'source-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    prepare_cues(output)
    return manifest

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--ffmpeg', default='ffmpeg')
    args = parser.parse_args()
    print(json.dumps(extract(args.archive, args.output, args.ffmpeg), indent=2))

if __name__ == '__main__':
    main()
