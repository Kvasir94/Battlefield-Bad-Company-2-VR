"""Single-frame spikes in a receiver --flicker-capture recording.

The receiver (BC2NativeIpcProbe --flicker-capture) saves every consumed pair's
left eye at half size as flicker/f-NNNNN.rgb plus flicker.jsonl. Consecutive
pairs are what a headset shows in a row. A pixel spikes at frame t when it
differs from both neighbours by more than --delta while the neighbours agree
with each other (within --settle): something appeared or vanished for exactly
one frame. Continuous motion changes the neighbours too, so it does not count.

A frame with more than --min-pixels spiking pixels is flagged. Flagged frames
are written as before/frame/after strips to --out (PNG, needs Pillow), and the
run fails (exit 1) if any frame is flagged.

    python tools/replay/frame_spikes.py reports/slider-rx-... --out spikes
"""
import argparse
import json
import pathlib
import sys

import numpy as np


def load(folder):
    """Receiver flicker capture (flicker.jsonl) or native-trace game-window
    capture (desktop.jsonl, --desktop-capture: every present at quarter size)."""
    folder = pathlib.Path(folder)
    desktop = not (folder / "flicker.jsonl").exists() and (folder / "desktop.jsonl").exists()
    name, sub, key, pattern = ("desktop.jsonl", "desktop", "present", "d-%05d.rgb") if desktop else ("flicker.jsonl", "flicker", "pair", "f-%05d.rgb")
    meta = [json.loads(line) for line in (folder / name).read_text().splitlines() if line.strip()]
    for r in meta:
        r["pair"] = r[key]
        r.setdefault("elapsed_ms", 0)
    meta.sort(key=lambda r: r["pair"])
    frames = []
    for r in meta:
        path = folder / sub / (pattern % r["pair"])
        if path.exists():
            data = np.frombuffer(path.read_bytes(), np.uint8)
            if data.size == r["width"] * r["height"] * 3:
                frames.append((r, data.reshape(r["height"], r["width"], 3)))
    return frames


def spikes(frames, delta=40, settle=12):
    out = []
    for i in range(1, len(frames) - 1):
        (r, cur), (_, prev), (_, nxt) = frames[i], frames[i - 1], frames[i + 1]
        if r["pair"] - frames[i - 1][0]["pair"] != 1 or frames[i + 1][0]["pair"] - r["pair"] != 1:
            continue  # a dropped frame between them: not a valid triple
        c, p, n = (x.astype(np.int16).max(axis=2) for x in (cur, prev, nxt))
        mask = (np.abs(c - p) > delta) & (np.abs(c - n) > delta) & (np.abs(n - p) < settle)
        count = int(mask.sum())
        if count:
            ys, xs = np.nonzero(mask)
            out.append({"pair": r["pair"], "elapsed_ms": r["elapsed_ms"], "pixels": count,
                        "box": [int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())], "index": i})
    return out


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("receiver")
    parser.add_argument("--delta", type=int, default=40)
    parser.add_argument("--settle", type=int, default=12)
    parser.add_argument("--min-pixels", type=int, default=150)
    parser.add_argument("--out")
    args = parser.parse_args(argv)
    frames = load(args.receiver)
    found = spikes(frames, args.delta, args.settle)
    flagged = [s for s in found if s["pixels"] >= args.min_pixels]
    gaps = sum(1 for a, b in zip(frames, frames[1:]) if b[0]["pair"] - a[0]["pair"] != 1)
    span = (frames[-1][0]["elapsed_ms"] - frames[0][0]["elapsed_ms"]) if len(frames) > 1 else 0
    summary = {"frames": len(frames), "missing_between": gaps, "fps": round(1000 * (len(frames) - 1) / span, 1) if span else None,
               "flagged": [{k: s[k] for k in ("pair", "elapsed_ms", "pixels", "box")} for s in flagged],
               "passed": not flagged}
    if args.out and flagged:
        from PIL import Image, ImageDraw
        out = pathlib.Path(args.out)
        out.mkdir(parents=True, exist_ok=True)
        for s in flagged:
            i = s["index"]
            strip = np.concatenate([frames[i - 1][1], frames[i][1], frames[i + 1][1]], axis=1)
            image = Image.fromarray(strip)
            w = frames[i][1].shape[1]
            x0, y0, x1, y1 = s["box"]
            ImageDraw.Draw(image).rectangle([w + x0, y0, w + x1, y1], outline=(255, 0, 0))
            image.save(out / ("spike-%05d.png" % s["pair"]))
    print(json.dumps(summary))
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
