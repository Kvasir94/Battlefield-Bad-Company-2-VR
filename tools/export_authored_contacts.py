"""Export authored hand contacts from private same-frame contact captures.

Reads the private captures produced by the floating-hands intake
(HandsIkPose-same-frame-contact.json, 1P_Reload-same-frame-contact.json) and
writes the runtime text library read by the BC2 native module
(include/fvr/interaction/AuthoredContacts.h, format "fvr-authored-contacts 1").

The output is a transformation of game animation data. It is PRIVATE: write it
under an ignored folder (reports/, private-inputs/) or beside the deployed DLL,
never into the repository. This tool contains no game data.

Contacts exported:
  support   left  wrist in the weapon root, 15 fingers  (HandsIkPose, first sample)
  gun_grip  right wrist in the weapon root, 15 fingers  (HandsIkPose, first sample)
  magazine  left  wrist in the magazine part, 15 fingers (1P_Reload, stable grasp window)
No handle contact is exported: the reload clip does not establish a hand on the
charging handle (see reports/floating-hands-intake/HANDOFF.md).

Usage:
  python -B tools/export_authored_contacts.py --intake reports/floating-hands-intake \
      --asset XM8_sp_s --rig a7f219a1426216ab --out reports/floating-hands-intake/authored-contacts-v1.txt
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import pathlib
import sys
from urllib.parse import quote

FINGERS = [f"{side}Hand{finger}{joint}" for side in ("Left",) for finger in ("Thumb", "Index", "Middle", "Ring", "Pinky") for joint in (1, 2, 3)]


def finger_names(side: str) -> list[str]:
    return [f"{side}Hand{finger}{joint}" for finger in ("Thumb", "Index", "Middle", "Ring", "Pinky") for joint in (1, 2, 3)]


def rigid(m: list[float], max_translation: float) -> bool:
    if len(m) != 16 or not all(math.isfinite(v) for v in m):
        return False
    for r in range(3):
        if abs(sum(m[r * 4 + c] ** 2 for c in range(3)) - 1) > 0.002 or abs(m[r * 4 + 3]) > 1e-4:
            return False
    return math.hypot(m[12], m[13], m[14]) <= max_translation and abs(m[15] - 1) < 1e-4


def fmt(m: list[float]) -> str:
    return " ".join(f"{v:.9g}" for v in m)


def contact_lines(kind: str, hand: str, asset: str, part: str, rig: str, clip: str, time: float, sample: dict) -> list[str]:
    side = "Left" if hand == "left" else "Right"
    contact = sample["contacts"][side]
    wrist = contact["part_from_wrist"][part]
    if not rigid(wrist, 1.5):
        raise ValueError(f"{kind}: wrist in {part} is not rigid")
    if not asset or any(ord(c) < 32 or ord(c) > 126 for c in asset):
        raise ValueError("Asset name must be printable ASCII")
    asset_token = quote(asset, safe="/_.:-")
    lines = [f"contact {kind} {hand} {asset_token} {part} {rig} {clip} {time:.6f} {fmt(wrist)}"]
    for name in finger_names(side):
        m = contact["wrist_from_fingers"][name]
        if not rigid(m, 0.2):
            raise ValueError(f"{kind}: finger {name} is not rigid")
        lines.append(f"finger {name} {fmt(m)}")
    return lines


def translation_delta(a: list[float], b: list[float]) -> float:
    return math.dist(a[12:15], b[12:15])


def magazine_window(samples: list[dict], part: str, window: int) -> int:
    """Index of the middle sample of the most magazine-motion window in which the
    left wrist stays within 1 cm / 0.15 of its start relative to the magazine
    (the same stability criteria as tools/bc2_authored_magazine_geometry.py)."""
    best, best_motion = -1, -1.0
    for start in range(0, len(samples) - window):
        rel = [s["contacts"]["Left"]["part_from_wrist"][part] for s in samples[start:start + window]]
        if any(translation_delta(r, rel[0]) > 0.01 for r in rel):
            continue
        if any(max(abs(r[i] - rel[0][i]) for i in (0, 1, 2, 4, 5, 6, 8, 9, 10)) > 0.15 for r in rel):
            continue
        mags = [s["weapon_relative"][part] for s in samples[start:start + window]]
        motion = max(translation_delta(m, mags[0]) for m in mags)
        # The magazine must be off its seat (in the hand), not still in the well.
        if motion > best_motion:
            best, best_motion = start + window // 2, motion
    if best < 0:
        raise ValueError("no stable magazine grasp window")
    return best


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--intake", type=pathlib.Path, required=True)
    parser.add_argument("--asset", required=True)
    parser.add_argument("--rig", required=True, help="skeleton fingerprint, hex")
    parser.add_argument("--weapon-part", default="jntWpn_1")
    parser.add_argument("--magazine-part", default="jntWpn_6")
    parser.add_argument("--out", type=pathlib.Path, required=True)
    args = parser.parse_args(argv)
    if not args.rig or not all(c in "0123456789abcdefABCDEF" for c in args.rig):
        parser.error("rig must be hex")
    ik_path = args.intake / "HandsIkPose-same-frame-contact.json"
    reload_path = args.intake / "1P_Reload-same-frame-contact.json"
    ik = json.loads(ik_path.read_text(encoding="utf-8"))
    lines = ["fvr-authored-contacts 1",
             f"# private: derived from {ik_path.name} sha256 {sha256(ik_path)}"]
    clip = ik["source"]["sha256"]
    first = ik["samples"][0]
    lines += contact_lines("support", "left", args.asset, args.weapon_part, args.rig.lower(), clip, first["time"], first)
    lines += contact_lines("gun_grip", "right", args.asset, args.weapon_part, args.rig.lower(), clip, first["time"], first)
    if reload_path.exists():
        reload = json.loads(reload_path.read_text(encoding="utf-8"))
        rate = int(reload.get("sample_rate_hz", 60))
        index = magazine_window(reload["samples"], args.magazine_part, max(2, round(0.2 * rate)))
        sample = reload["samples"][index]
        lines.append(f"# private: derived from {reload_path.name} sha256 {sha256(reload_path)} sample {index}")
        lines += contact_lines("magazine", "left", args.asset, args.magazine_part, args.rig.lower(),
                               reload["source"]["sha256"], sample["time"], sample)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"wrote {args.out} ({sum(1 for l in lines if l.startswith('contact'))} contacts)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
