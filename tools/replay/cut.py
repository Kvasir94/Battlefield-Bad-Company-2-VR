#!/usr/bin/env python3
"""Cut a regression tape out of a recorded FVR_BC2_INPUT tape.

    python tools/replay/cut.py session/input.fvrtape --events session/gun-events.jsonl \
        --bookmark 3 --before 20 --after 5 --expect "transfers=1 no-auto-reload" --out tapes/xm8-1005.fvrtape
    python tools/replay/cut.py session/input.fvrtape --from-tick 4000 --to-tick 6200 --out cut.fvrtape

Tape ticks and gun-event `tick` both count native gathers from the start of the
DLL session, so a bookmark (`phase: tape, reason: bookmark`) or any event tick
can choose the window. The `#! weapon/loaded/reserve` start conditions come
from the last native `sample` (or transfer) row before the cut; the original
`#!` provenance lines are kept and a `source=cut:...` line is added. Write the
`#! expect` line from what *should* have happened, before fixing the bug.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

CONDITION_KEYS = ("weapon", "loaded", "reserve", "capacity")


def read_tape(path: Path):
    lines = path.read_text(encoding="ascii").splitlines()
    head = lines[0].split()
    if len(head) != 4 or head[:2] != ["FVR_BC2_INPUT", "1"]:
        raise ValueError(f"{path} is not an FVR_BC2_INPUT v1 tape")
    meta, expect, rows = [], [], []
    for line in lines[1:]:
        if line.startswith("#!"):
            tokens = line[2:].split()
            if tokens and tokens[0] == "expect":
                expect += tokens[1:]
            else:
                meta += tokens
        elif line and not line.startswith("#"):
            ticks, rest = line.split(" ", 1)
            rows.append([int(ticks), rest])
    return int(head[2]), int(head[3]), meta, expect, rows


def start_state(events: Path | None, tick: int) -> dict:
    state = {}
    if not events:
        return state
    for line in events.read_text(encoding="utf-8").splitlines():
        try:
            row = json.loads(line)
        except json.JSONDecodeError:
            continue
        if not isinstance(row, dict) or row.get("src") != "native" or int(row.get("tick", 1 << 62)) > tick:
            continue
        if row.get("copy") not in (None, "server") and row.get("branch") not in (None, 2):
            continue
        weapon = (row.get("weapon") or {}).get("asset")
        if row.get("phase") == "sample" and isinstance(row.get("loaded"), int):
            state = {"weapon": weapon, "loaded": row["loaded"], "reserve": row.get("reserve")}
        elif row.get("phase") == "transfer" and isinstance(row.get("loaded_after"), int):
            state = {"weapon": weapon, "loaded": row["loaded_after"], "reserve": row.get("reserve_after")}
    return state


def bookmark_tick(events: Path, number: int) -> int:
    marks = []
    for line in events.read_text(encoding="utf-8").splitlines():
        try:
            row = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(row, dict) and row.get("phase") == "tape" and row.get("reason") == "bookmark":
            marks.append(int(row["tick"]))
    if not 1 <= number <= len(marks):
        raise ValueError(f"bookmark {number} not found ({len(marks)} bookmarks)")
    return sorted(marks)[number - 1]


def cut(tape: Path, first: int, last: int, events: Path | None = None, expect: str | None = None) -> str:
    step, seed, meta, old_expect, rows = read_tape(tape)
    if first < 1 or last < first:
        raise ValueError("need 1 <= from-tick <= to-tick")
    out_rows, tick = [], 0
    for count, rest in rows:
        lo, hi = tick + 1, tick + count
        tick = hi
        keep = min(hi, last) - max(lo, first) + 1
        if keep > 0:
            out_rows.append(f"{keep} {rest}")
    if not out_rows:
        raise ValueError(f"window {first}..{last} is outside the tape (1..{tick})")
    state = start_state(events, first - 1)
    kept = [t for t in meta if not any(t.startswith(k) and t[len(k):len(k) + 1] in "=<>!" for k in CONDITION_KEYS)
            and not t.startswith("source=")]
    conditions = []
    if state.get("weapon"):
        conditions.append(f"weapon={state['weapon']}")
    if isinstance(state.get("loaded"), int):
        conditions.append(f"loaded={state['loaded']}")
    if isinstance(state.get("reserve"), int):
        conditions.append(f"reserve={state['reserve']}")
    if not conditions:  # no gun events: keep the recording's own conditions
        conditions = [t for t in meta if t not in kept and not t.startswith("source=")]
    digest = hashlib.sha256(tape.read_bytes()).hexdigest()[:12]
    lines = [f"FVR_BC2_INPUT 1 {step} {seed}"]
    if conditions or kept:
        lines.append("#! " + " ".join(conditions + kept))
    lines.append(f"#! source=cut:{tape.name}:{digest}:{first}-{last}")
    expect_tokens = expect.split() if expect is not None else old_expect
    if expect_tokens:
        lines.append("#! expect " + " ".join(expect_tokens))
    return "\n".join(lines + out_rows) + "\n"


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("tape", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--events", type=Path)
    parser.add_argument("--from-tick", type=int)
    parser.add_argument("--to-tick", type=int)
    parser.add_argument("--bookmark", type=int)
    parser.add_argument("--before", type=float, default=20.0, help="seconds before the bookmark")
    parser.add_argument("--after", type=float, default=5.0, help="seconds after the bookmark")
    parser.add_argument("--expect", help="replace the #! expect tokens")
    args = parser.parse_args(argv)
    try:
        if args.out.exists():
            raise ValueError("output tape already exists")
        if args.bookmark is not None:
            if not args.events:
                raise ValueError("--bookmark needs --events")
            step = read_tape(args.tape)[0]
            mark = bookmark_tick(args.events, args.bookmark)
            first = max(1, mark - round(args.before * 1e6 / step))
            last = mark + round(args.after * 1e6 / step)
        elif args.from_tick is not None and args.to_tick is not None:
            first, last = args.from_tick, args.to_tick
        else:
            raise ValueError("give --bookmark or --from-tick and --to-tick")
        text = cut(args.tape, first, last, args.events, args.expect)
    except (OSError, ValueError) as error:
        print(f"cut: {error}", file=sys.stderr)
        return 2
    args.out.write_text(text, encoding="ascii", newline="\n")
    print(f"{args.out}: ticks {first}..{last}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
