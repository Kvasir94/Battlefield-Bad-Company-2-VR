"""Flicker rule for host-composed props in a BC2NativeIpcProbe receiver folder.

The receiver (run with --body-props-cache) writes props.jsonl: one row per
pair that carries a host prop, with the per-eye instance counts and how many
pixels the composition changed in each eye. pairs.jsonl lists every consumed
pair.

Inside the span from the first to the last pair carrying a prop, every
consumed pair must carry the prop in BOTH eyes and must change pixels in
both eyes. A pair that fails is a dropout. Any dropout, or a span shorter
than --min-span-ms, fails the run (exit 1).

    python tools/replay/prop_flicker.py reports/slider-rx-... --min-span-ms 11500
"""
import argparse
import json
import pathlib
import sys


def rows(path):
    out = []
    for line in pathlib.Path(path).read_text().splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            out.append(json.loads(line))
        except ValueError:
            continue
    return out


def evaluate(folder, min_span_ms=0, min_pixels=1, from_ms=0):
    folder = pathlib.Path(folder)
    props = {r["pair"]: r for r in rows(folder / "props.jsonl") if "pair" in r}
    consumed = {}
    for r in rows(folder / "pairs.jsonl"):
        if "pair" in r and "elapsed_ms" in r:
            consumed[r["pair"]] = r["elapsed_ms"]
    # --from-ms: only pairs from this receiver time on (e.g. after the head
    # pitches down so the prop is in view; earlier it is legitimately off-screen).
    consumed = {p: ms for p, ms in consumed.items() if ms >= from_ms}
    carrying = sorted(p for p, r in props.items() if p in consumed and max(r.get("props", [0, 0])) > 0)
    result = {"pairs_with_props": len(carrying), "dropouts": [], "span_ms": 0, "passed": False}
    if not carrying:
        result["reason"] = "no pair carried a prop"
        return result
    first, last = carrying[0], carrying[-1]
    result["first_pair"], result["last_pair"] = first, last
    first_ms = props[first].get("elapsed_ms", consumed.get(first, 0))
    last_ms = props[last].get("elapsed_ms", consumed.get(last, 0))
    result["span_ms"] = last_ms - first_ms
    for pair in sorted(p for p in consumed if first <= p <= last):
        r = props.get(pair)
        counts = r.get("props", [0, 0]) if r else [0, 0]
        pixels = r.get("prop_pixels", [0, 0]) if r else [0, 0]
        # prop_pixels -1: the receiver did not measure this pair (outside --save-pairs).
        measured = [v for v in pixels if v >= 0]
        if min(counts) < 1 or (measured and min(measured) < min_pixels):
            result["dropouts"].append({"pair": pair, "elapsed_ms": consumed[pair], "props": counts, "prop_pixels": pixels})
    result["one_eye_only"] = sum(1 for d in result["dropouts"] if max(d["props"]) > 0 and min(d["props"]) == 0)
    result["passed"] = not result["dropouts"] and result["span_ms"] >= min_span_ms
    if result["span_ms"] < min_span_ms:
        result["reason"] = "span %d ms shorter than %d ms" % (result["span_ms"], min_span_ms)
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("receiver")
    parser.add_argument("--min-span-ms", type=int, default=0)
    parser.add_argument("--min-pixels", type=int, default=1)
    parser.add_argument("--from-ms", type=int, default=0)
    args = parser.parse_args(argv)
    result = evaluate(args.receiver, args.min_span_ms, args.min_pixels, args.from_ms)
    print(json.dumps(result))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
