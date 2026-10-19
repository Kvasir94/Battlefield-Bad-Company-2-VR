#!/usr/bin/env python3
"""Check a replay run's gun events against one scenario's rules.

Input is gun-events JSONL in the docs/GUN-EVENT-LOG.md v1 schema, from the live
DLL or from tools/sim (fvr_replay_sim). The same rules run on both. Rules come
from a scenario file (tools/replay/scenarios/<name>.json) and, optionally, from
the tape's own `#! expect` line (--tape).

Rule types (each may carry "id" and "label"):
  sequence    ordered sub-sequence of matchers               {"steps": [...]}
  count       number of matching rows                        {"match": m, "eq"|"min"|"max": n}
  ammo_delta  native loaded/reserve change on one copy       {"copy": "server", "loaded": "+fill"|N|">0", "reserve": "-fill", "conserve": true}
  within      from-row to next to-row timing                 {"from": m, "to": m, "max_ms": n, "min_ms": n, "max_ticks": n}
  never       forbidden row, optionally scoped               {"match": m, "before": m, "after": m, "while": "mag_out"}

A matcher is a phase name, "phase[key=value,key=value]", or an object
{"phase": ..., "src": ..., "copy": ..., "where": {...}}. Ammunition deltas use
native rows only; fvr rows (credit, seat) never prove ammunition.

Exit status: 0 PASS, 1 FAIL, 2 unreadable input or bad rules.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

SCHEMA = "fvr.bc2.replay_assert.v1"
ALIASES = {"state": "firing_state", "switch": "weapon_switch", "pouch": "pouch_start",
           "pouch-start": "pouch_start", "freeze-start": "freeze_start",
           "freeze-stop": "freeze_stop", "auto-reload": "auto_reload"}
BRANCH = {0: "client0", 1: "client1", 2: "server"}
MAG_OUT = "unseat"
MAG_IN = {"seat", "cancel", "complete", "end"}
AMMO_KEYS = (("loaded_before", "reserve_before"), ("loaded", "reserve"))


class RuleError(ValueError):
    pass


# ----------------------------------------------------------------------------- input

def load_events(paths: list[Path]) -> tuple[list[dict[str, Any]], list[str]]:
    rows, invalid = [], []
    for path in paths:
        with path.open(encoding="utf-8") as handle:
            for number, line in enumerate(handle, 1):
                line = line.strip()
                if not line:
                    continue
                try:
                    row = json.loads(line)
                except json.JSONDecodeError:
                    invalid.append(f"{path.name}:{number}: not JSON")
                    continue
                tick = row.get("tick", row.get("frame")) if isinstance(row, dict) else None
                if (not isinstance(row, dict) or row.get("v") != 1 or tick is None
                        or "qpc_us" not in row or row.get("src") not in ("native", "fvr")
                        or not isinstance(row.get("phase"), str)):
                    invalid.append(f"{path.name}:{number}: not a v1 gun event")
                    continue
                row["tick"] = int(tick)
                row["phase"] = ALIASES.get(row["phase"], row["phase"])
                if row.get("copy") is None and isinstance(row.get("branch"), int):
                    row["copy"] = BRANCH.get(row["branch"])
                row["_order"] = len(rows)
                rows.append(row)
    rows.sort(key=lambda r: (r["qpc_us"], r["tick"], r["_order"]))
    return rows, invalid


# ----------------------------------------------------------------------------- matchers

_MATCH = re.compile(r"^([a-z_\-]+)(?:\[(.*)\])?$")


def _scalar(text: str) -> Any:
    lowered = text.lower()
    if lowered in ("true", "false"):
        return lowered == "true"
    if lowered in ("null", "none"):
        return None
    try:
        return int(text)
    except ValueError:
        try:
            return float(text)
        except ValueError:
            return text


def matcher(spec: Any) -> dict[str, Any]:
    if isinstance(spec, dict):
        if "phase" not in spec:
            raise RuleError(f"matcher without phase: {spec!r}")
        where = dict(spec.get("where", {}))
        for key in ("src", "copy"):
            if key in spec:
                where[key] = spec[key]
        return {"phase": ALIASES.get(spec["phase"], spec["phase"]), "where": where, "text": json.dumps(spec)}
    if not isinstance(spec, str):
        raise RuleError(f"bad matcher: {spec!r}")
    found = _MATCH.match(spec.strip())
    if not found:
        raise RuleError(f"bad matcher: {spec!r}")
    where = {}
    if found[2]:
        for part in found[2].split(","):
            if "=" not in part:
                raise RuleError(f"bad matcher condition {part!r} in {spec!r}")
            key, value = part.split("=", 1)
            where[key.strip()] = _scalar(value.strip())
    return {"phase": ALIASES.get(found[1], found[1]), "where": where, "text": spec}


def matches(m: dict[str, Any], row: dict[str, Any]) -> bool:
    if row["phase"] != m["phase"]:
        return False
    return all(row.get(key) == value for key, value in m["where"].items())


def where(row: dict[str, Any] | None) -> dict[str, Any]:
    return {} if row is None else {"tick": row["tick"], "qpc_us": row["qpc_us"], "phase": row["phase"]}


# ----------------------------------------------------------------------------- rules

def rule_sequence(rule, rows):
    steps = [matcher(s) for s in rule["steps"]]
    if not steps:
        raise RuleError("sequence without steps")
    at, last = 0, None
    for row in rows:
        if at < len(steps) and matches(steps[at], row):
            at, last = at + 1, row
    if at == len(steps):
        return True, f"{len(steps)} steps in order", where(last)
    return False, f"missing step {at + 1} '{steps[at]['text']}' after {at} matched", where(last)


def rule_count(rule, rows):
    m = matcher(rule["match"])
    n = sum(1 for row in rows if matches(m, row))
    bounds = {k: rule[k] for k in ("eq", "min", "max") if k in rule}
    if not bounds:
        raise RuleError("count needs eq, min or max")
    ok = (("eq" not in bounds or n == bounds["eq"]) and ("min" not in bounds or n >= bounds["min"])
          and ("max" not in bounds or n <= bounds["max"]))
    return ok, f"{m['text']} x{n} (want {bounds})", {}


def _ammo(row):
    if row["src"] != "native":
        return None
    for loaded, reserve in AMMO_KEYS:
        if isinstance(row.get(loaded), int) and isinstance(row.get(reserve), int):
            return row[loaded], row[reserve]
    return None


def _final_ammo(row):
    if isinstance(row.get("loaded_after"), int) and isinstance(row.get("reserve_after"), int):
        return row["loaded_after"], row["reserve_after"]
    return _ammo(row)


def _expected(spec, loaded0, reserve0, capacity, which):
    if isinstance(spec, int):
        return spec
    if not isinstance(spec, str):
        raise RuleError(f"bad ammo spec {spec!r}")
    text = spec.replace(" ", "")
    if text in ("+fill", "-fill", "fill"):
        if capacity is None:
            raise RuleError("fill needs capacity (rule or scenario)")
        fill = max(0, min(capacity - loaded0, reserve0))
        return -fill if text == "-fill" else fill
    if text == "same":
        return 0
    try:
        return int(text)
    except ValueError:
        raise RuleError(f"bad {which} spec {spec!r}") from None


def rule_ammo_delta(rule, rows, capacity=None):
    copy = rule.get("copy", "server")
    capacity = rule.get("capacity", capacity)
    native = [r for r in rows if r.get("copy") == copy and _ammo(r) is not None]
    if not native:
        return False, f"no native ammunition rows for copy {copy}", {}
    loaded0, reserve0 = _ammo(native[0])
    loaded1, reserve1 = _final_ammo(native[-1])
    failures = []
    for which, actual in (("loaded", loaded1 - loaded0), ("reserve", reserve1 - reserve0)):
        if which not in rule:
            continue
        spec = rule[which]
        compare = re.match(r"^(<=|>=|<|>|!=)\s*(-?\d+)$", spec) if isinstance(spec, str) else None
        if compare:
            op, bound = compare[1], int(compare[2])
            ok = {"<=": actual <= bound, ">=": actual >= bound, "<": actual < bound,
                  ">": actual > bound, "!=": actual != bound}[op]
            if not ok:
                failures.append(f"{which} {actual:+d} not {op} {bound}")
            continue
        want = _expected(spec, loaded0, reserve0, capacity, which)
        if actual != want:
            failures.append(f"{which} {actual:+d} != {want:+d}")
    if rule.get("conserve"):
        shots = sum(int(r.get("consumed", 1 if r.get("result") == "shot" else 0))
                    for r in rows if r["phase"] == "fire" and r.get("copy") == copy)
        if (loaded1 + reserve1) != (loaded0 + reserve0 - shots):
            failures.append(f"loaded+reserve {loaded0 + reserve0}->{loaded1 + reserve1} with {shots} shots")
    detail = f"{copy}: {loaded0}/{reserve0} -> {loaded1}/{reserve1}"
    return not failures, detail + ("; " + "; ".join(failures) if failures else ""), where(native[-1])


def rule_within(rule, rows):
    start, stop = matcher(rule["from"]), matcher(rule["to"])
    limits = {k: rule[k] for k in ("max_ms", "min_ms", "max_ticks") if k in rule}
    if not limits:
        raise RuleError("within needs max_ms, min_ms or max_ticks")
    pairs, opened = [], None
    for row in rows:
        if opened is None and matches(start, row):
            opened = row
        elif opened is not None and matches(stop, row):
            pairs.append((opened, row))
            opened = None
            if not rule.get("every", False):
                break
    if not pairs:
        return False, f"no {start['text']} -> {stop['text']} pair", where(opened)
    worst = None
    for a, b in pairs:
        ms, ticks = (b["qpc_us"] - a["qpc_us"]) / 1000.0, b["tick"] - a["tick"]
        bad = (("max_ms" in limits and ms > limits["max_ms"]) or ("min_ms" in limits and ms < limits["min_ms"])
               or ("max_ticks" in limits and ticks > limits["max_ticks"]))
        if bad or worst is None:
            worst = (bad, ms, ticks, b)
        if bad:
            break
    bad, ms, ticks, row = worst
    return not bad, f"{start['text']} -> {stop['text']}: {ms:.1f} ms, {ticks} ticks (limits {limits})", where(row)


def rule_never(rule, rows):
    m = matcher(rule["match"])
    before = matcher(rule["before"]) if "before" in rule else None
    after = matcher(rule["after"]) if "after" in rule else None
    scope = rule.get("while")
    if scope not in (None, "mag_out", "mag_in"):
        raise RuleError(f"unknown while scope {scope!r}")
    armed, mag_out = after is None, False
    for row in rows:
        if before is not None and matches(before, row):
            break
        if after is not None and not armed and matches(after, row):
            armed = True
            continue
        if row["src"] == "fvr":
            if isinstance(row.get("mag_out"), bool):
                mag_out = row["mag_out"]
            elif row["phase"] == MAG_OUT:
                mag_out = True
            elif row["phase"] in MAG_IN:
                mag_out = False
        if not armed or not matches(m, row):
            continue
        if scope == "mag_out" and not mag_out:
            continue
        if scope == "mag_in" and mag_out:
            continue
        return False, f"forbidden {m['text']} at tick {row['tick']}", where(row)
    return True, f"no {m['text']}" + (f" while {scope}" if scope else ""), {}


RULES = {"sequence": rule_sequence, "count": rule_count, "ammo_delta": rule_ammo_delta,
         "within": rule_within, "never": rule_never}


# ----------------------------------------------------------------------------- tape expectations

def expect_rules(tokens: list[str]) -> list[dict[str, Any]]:
    """Translate a tape's `#! expect` tokens into rules (unknown tokens are refused)."""
    rules, delta = [], {}
    for token in tokens:
        key, op, value = re.match(r"^([a-z_\-]+)(<=|>=|=)?(.*)$", token).groups()
        if key == "transfers" and op == "=":
            rules.append({"id": "expect.transfers", "type": "count", "match": "transfer[copy=server]", "eq": int(value)})
        elif key == "credits" and op == "=":
            rules.append({"id": "expect.credits", "type": "count", "match": "credit", "eq": int(value)})
        elif key in ("loaded", "reserve") and op == "=":
            delta[key] = value
        elif key == "no-auto-reload" and not op:
            rules.append({"id": "expect.no_auto_reload", "type": "never", "match": "auto_reload[suppressed=false]"})
        elif key == "no-fire-mag-out" and not op:
            rules.append({"id": "expect.no_fire_mag_out", "type": "never", "match": "fire[result=shot]", "while": "mag_out"})
        elif key == "seat_to_transfer_ms" and op == "<=":
            rules.append({"id": "expect.seat_to_transfer", "type": "within", "from": "seat",
                          "to": "transfer[copy=server]", "max_ms": float(value)})
        else:
            raise RuleError(f"unknown #! expect token {token!r}")
    if delta:
        rules.append({"id": "expect.ammo", "type": "ammo_delta", "copy": "server", **delta})
    return rules


def tape_expect(path: Path) -> list[str]:
    tokens = []
    with path.open(encoding="ascii") as handle:
        header = handle.readline().split()
        if header[:2] != ["FVR_BC2_INPUT", "1"]:
            raise RuleError(f"{path} is not an FVR_BC2_INPUT v1 tape")
        for line in handle:
            if line.startswith("#!"):
                parts = line[2:].split()
                if parts and parts[0] == "expect":
                    tokens += parts[1:]
            elif line[:1].isdigit():
                break
    return tokens


# ----------------------------------------------------------------------------- driver

def evaluate(rows, invalid, scenario: dict[str, Any], extra_rules=(), label="live") -> dict[str, Any]:
    capacity = scenario.get("capacity")
    rules = list(scenario.get("rules", [])) + list(extra_rules)
    if not rules:
        raise RuleError("scenario has no rules")
    results = []
    if invalid or not rows:
        results.append({"id": "input", "type": "input", "passed": False,
                        "detail": f"{len(rows)} rows, {len(invalid)} invalid", "invalid": invalid[:20]})
    for index, rule in enumerate(rules):
        kind = rule.get("type")
        if kind not in RULES:
            raise RuleError(f"rule {index}: unknown type {kind!r}")
        if kind == "ammo_delta":
            passed, detail, at = rule_ammo_delta(rule, rows, capacity)
        else:
            passed, detail, at = RULES[kind](rule, rows)
        results.append({"id": rule.get("id", f"{kind}.{index}"), "type": kind, "passed": passed,
                        "detail": detail, **({"label": rule["label"]} if "label" in rule else {}), **at})
    return {"schema": SCHEMA, "scenario": scenario.get("name"), "label": label, "events": len(rows),
            "passed": all(r["passed"] for r in results), "rules": results, "headset_accepted": False}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("events", nargs="+", type=Path, help="gun-events JSONL file(s)")
    parser.add_argument("--scenario", type=Path, required=True)
    parser.add_argument("--tape", type=Path, help="also apply the tape's #! expect line")
    parser.add_argument("--label", choices=("live", "sim"), default="live")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        scenario = json.loads(args.scenario.read_text(encoding="utf-8"))
        extra = expect_rules(tape_expect(args.tape)) if args.tape else []
        rows, invalid = load_events(args.events)
        report = evaluate(rows, invalid, scenario, extra, args.label)
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"replay_assert: {error}", file=sys.stderr)
        return 2
    text = json.dumps(report, indent=2)
    if args.output:
        args.output.write_text(text + "\n", encoding="utf-8")
    for r in report["rules"]:
        print(f"{'PASS' if r['passed'] else 'FAIL'}  {r['id']}: {r['detail']}")
    print(f"RESULT: {'PASS' if report['passed'] else 'FAIL'} ({report['label']}, {report['events']} events)")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
