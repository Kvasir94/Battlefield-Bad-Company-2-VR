#!/usr/bin/env python3
"""Analyze FrostbiteVR gun-event JSONL logs (schema: docs/GUN-EVENT-LOG.md).

Reads one or more JSONL files, merges them by (qpc_us, tick), rebuilds one
timeline per reload and reports:

  * seat -> ammo-credit delay (ms and ticks)
  * ammo conservation (loaded + reserve across a reload, accounting for shots)
  * double credits (fvr credit or native transfer gains beyond the seats)
  * auto-reloads (native, with and without the suppression override)
  * fire attempts while the magazine is out or the chamber/magazine is empty
  * disagreement among the three firing copies (client0, client1, server)
  * cancels by reason, freeze leaks, dropped events, stale samples

and ends with an overall PASS/FAIL under configurable thresholds.
Stdlib only, Python 3.10+. Exit status: 0 PASS, 1 FAIL, 2 usage/input error.

Ammo is judged only from native observations (transfer, restore, fire, step,
sample, firing_state). Mod-side events (credit, seat, ...) are used for timing
and event order; `mod_chamber` is never treated as game truth.
"""
from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from dataclasses import dataclass, field, fields, asdict
from pathlib import Path
from typing import Any, Iterable

SCHEMA_VERSION = 1
COPIES = ("client0", "client1", "server")
BRANCH_COPY = {0: "client0", 1: "client1", 2: "server"}
RELOAD_STATES = (10, 11, 12)  # begin, wait, transfer (single-round reload loops 12 -> 11)
IDLE_STATE = 2
ORPHAN_WINDOW_US = 10_000_000

NATIVE_PHASES = {
    "firing_state", "transfer", "commit", "restore", "step", "auto_reload",
    "freeze_start", "freeze_stop", "fire", "weapon_switch", "sample", "dropped",
    "tape", "claim", "slide_lock", "anim_sound",
}
FVR_PHASES = {
    "sample", "begin", "pouch_start", "capture", "seat", "credit", "rack", "cancel",
    "unseat", "release", "complete", "end", "haptic", "claim", "tape", "chamber", "prop", "pack", "rack_trace", "hint",
}
KNOWN_PHASES = NATIVE_PHASES | FVR_PHASES
# Accepted spellings from the one-liner vocabulary (FVR_BC2_GUN / KF2 phase=).
PHASE_ALIASES = {
    "state": "firing_state", "switch": "weapon_switch", "pouch": "pouch_start",
}
# Phases that, arriving with no open reload, are attached to the weapon's most recent reload.
ORPHAN_OK = {
    "seat", "credit", "transfer", "restore", "commit", "rack", "complete", "cancel",
    "unseat", "release", "freeze_stop", "end",
}


# --------------------------------------------------------------------------- thresholds

@dataclass
class Thresholds:
    """Pass/fail limits. None disables a limit (the check is reported as INFO)."""
    max_seat_credit_ms: float | None = 500.0
    max_seat_credit_ticks: int | None = None
    max_seat_transfer_ms: float | None = None
    max_uncredited_seats: int | None = 0
    max_conservation_violations: int | None = 0
    max_double_credits: int | None = 0
    max_auto_reloads_unsuppressed: int | None = 0
    max_auto_reloads: int | None = None
    max_fire_leaks: int | None = 0
    max_fire_attempts_unloaded: int | None = None
    max_copy_disagreement_ticks: int | None = 2
    max_copy_disagreement_ms: float | None = None
    max_cancels: int | None = None
    max_cancels_by_reason: dict[str, int] = field(default_factory=dict)
    deny_cancel_reasons: list[str] = field(default_factory=list)
    max_freeze_leaks: int | None = 0
    max_dropped_events: int | None = 0
    max_stale_samples: int | None = None
    max_invalid_lines: int | None = 0
    max_incomplete_reloads: int | None = None
    min_reloads: int = 0


def thresholds_from_mapping(mapping: dict[str, Any], base: Thresholds | None = None) -> Thresholds:
    th = Thresholds(**asdict(base)) if base else Thresholds()
    known = {f.name for f in fields(Thresholds)}
    for key, value in mapping.items():
        if key not in known:
            raise ValueError(f"unknown rule {key!r}; known: {', '.join(sorted(known))}")
        if key == "max_cancels_by_reason":
            if not isinstance(value, dict) or not all(isinstance(v, int) for v in value.values()):
                raise ValueError("max_cancels_by_reason must map reason -> int")
        elif key == "deny_cancel_reasons":
            if not isinstance(value, list) or not all(isinstance(v, str) for v in value):
                raise ValueError("deny_cancel_reasons must be a list of strings")
        elif value is not None and (isinstance(value, bool) or not isinstance(value, (int, float))):
            raise ValueError(f"rule {key} must be a number or null")
        setattr(th, key, value)
    return th


# --------------------------------------------------------------------------- parsing

@dataclass
class Ev:
    order: int
    raw: dict
    tick: int
    t: float  # qpc_us
    src: str
    phase: str
    wkey: tuple
    copy: str | None

    def get(self, key: str, default=None):
        return self.raw.get(key, default)


def _is_num(x) -> bool:
    return isinstance(x, (int, float)) and not isinstance(x, bool)


def _is_int(x) -> bool:
    return isinstance(x, int) and not isinstance(x, bool)


def parse_event(obj: Any, order: int) -> Ev:
    """Validate one decoded line; raise ValueError describing the first problem."""
    if not isinstance(obj, dict):
        raise ValueError("not a JSON object")
    v = obj.get("v")
    if not _is_int(v) or v < 1 or v > SCHEMA_VERSION:
        raise ValueError(f"unsupported or missing v: {v!r}")
    tick = obj.get("tick", obj.get("frame"))
    if not _is_int(tick) or tick < 0:
        raise ValueError("missing or invalid tick")
    t = obj.get("qpc_us")
    if not _is_num(t) or t < 0:
        raise ValueError("missing or invalid qpc_us")
    src = obj.get("src")
    if src not in ("native", "fvr"):
        raise ValueError(f"invalid src: {src!r}")
    phase = obj.get("phase")
    if not isinstance(phase, str) or not phase:
        raise ValueError("missing phase")
    phase = PHASE_ALIASES.get(phase.replace("-", "_"), phase.replace("-", "_"))
    copy = obj.get("copy")
    if copy is None and _is_int(obj.get("branch")):
        copy = BRANCH_COPY.get(obj["branch"])
    if copy is not None and copy not in COPIES:
        raise ValueError(f"invalid copy: {copy!r}")
    weapon = obj.get("weapon")
    if weapon is not None and not isinstance(weapon, dict):
        raise ValueError("weapon must be an object")
    weapon = weapon or {}
    wkey = (weapon.get("asset"), weapon.get("id"))
    try:
        hash(wkey)
    except TypeError:
        raise ValueError("weapon asset/id must be scalars") from None
    if phase == "firing_state":
        if "to" not in obj and "cur" in obj:
            obj["to"], obj["from"] = obj["cur"], obj.get("prev")
    return Ev(order, obj, tick, float(t), src, phase, wkey, copy)


def read_events(paths: Iterable[Path]) -> tuple[list[Ev], list[dict]]:
    events: list[Ev] = []
    invalid: list[dict] = []
    order = 0
    for path in paths:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            for lineno, line in enumerate(fh, 1):
                line = line.strip()
                if not line:
                    continue
                order += 1
                try:
                    events.append(parse_event(json.loads(line), order))
                except (ValueError, TypeError) as exc:  # JSONDecodeError is a ValueError
                    invalid.append({"file": str(path), "line": lineno, "error": str(exc)})
    events.sort(key=lambda e: (e.t, e.tick, e.order))
    return events, invalid


# --------------------------------------------------------------------------- model

def _ammo_sum(loaded, reserve):
    if _is_int(loaded) and _is_int(reserve):
        return loaded + reserve
    return None


@dataclass
class Reload:
    rid: int
    wkey: tuple
    origin: str
    start_t: float
    start_tick: int
    end_t: float = 0.0
    end_tick: int = 0
    closed: bool = False
    closed_by: str | None = None
    fvr_seen: bool = False
    fvr_begun: bool = False
    fvr_done: bool = False
    end_reason: str | None = None
    native_seen: bool = False
    native_entered: bool = False
    active: set = field(default_factory=set)
    completed: bool = False
    seats: list = field(default_factory=list)
    credit_n: int = 0
    excess_credits: int = 0
    gain_n: Counter = field(default_factory=Counter)
    excess_gains: Counter = field(default_factory=Counter)
    cancels: list = field(default_factory=list)
    auto: list = field(default_factory=list)
    freeze_open: dict = field(default_factory=dict)
    freeze_ms: list = field(default_factory=list)
    snap: dict = field(default_factory=dict)
    violations: list = field(default_factory=list)
    late: list = field(default_factory=list)
    seq: list = field(default_factory=list)

    @property
    def native_done(self) -> bool:
        return self.native_entered and not self.active

    def done(self) -> bool:
        fvr_ok = (not self.fvr_seen) or self.fvr_done
        nat_ok = (not self.native_seen) or self.native_done
        return (self.fvr_done or self.native_done) and fvr_ok and nat_ok

    def outcome(self) -> str:
        if self.cancels:
            return "cancelled"
        if self.completed or (not self.fvr_seen and self.native_done):
            return "completed"
        return "incomplete"


@dataclass
class WState:
    wkey: tuple
    open: Reload | None = None
    last: Reload | None = None
    mag_out: bool = False
    copies: dict = field(default_factory=dict)
    span: dict | None = None
    last_tick: int = 0
    last_t: float = 0.0


class Analyzer:
    def __init__(self) -> None:
        self.weapons: dict[tuple, WState] = {}
        self.reloads: list[Reload] = []
        self.cancels: Counter = Counter()
        self.auto = {"total": 0, "unsuppressed": 0}
        self.fire_events = 0
        self.fire_attempts: dict[tuple, dict] = {}
        self.disagreements: list[dict] = []
        self.phase_counts: Counter = Counter()
        self.unknown_phases: Counter = Counter()
        self.dropped = 0
        self.stale_samples = 0
        self.tape_events: list[dict] = []
        self._tick: int | None = None
        self._touched: set = set()
        self._now = 0.0

    # ---- driver
    def run(self, events: list[Ev]) -> None:
        for ev in events:
            self._feed(ev)
        self._flush()
        for ws in self.weapons.values():
            if ws.span is not None:
                self._close_span(ws, ws.last_tick, ws.last_t, unresolved=True)
            if ws.open is not None:
                self._close(ws, ws.open, "eof", ws.last_t, ws.last_tick)
        for rl in self.reloads:
            for cp, t0 in rl.freeze_open.items():
                rl.violations.append({"type": "freeze_leak", "copy": cp, "tick": rl.end_tick})

    def _feed(self, ev: Ev) -> None:
        if self._tick is not None and ev.tick != self._tick:
            self._flush()
        self._tick, self._now = ev.tick, ev.t
        ws = self.weapons.setdefault(ev.wkey, WState(ev.wkey))
        self._touched.add(ev.wkey)
        ws.last_tick, ws.last_t = ev.tick, ev.t
        self.phase_counts[ev.phase] += 1
        if ev.phase not in KNOWN_PHASES:
            self.unknown_phases[ev.phase] += 1
        if ev.phase == "dropped":
            n = ev.get("n", 0)
            self.dropped += n if _is_int(n) else 0
        if ev.phase == "sample" and ev.get("stale") not in (None, 0, False):
            self.stale_samples += 1
        if ev.phase == "tape":
            self.tape_events.append({"tick": ev.tick, "reason": ev.get("reason"), "name": ev.get("name")})
        if ev.phase == "cancel":
            self.cancels[str(ev.get("reason", "unknown"))] += 1
        if ev.phase == "auto_reload":
            self.auto["total"] += 1
            if not ev.get("suppressed", False):
                self.auto["unsuppressed"] += 1
        if ev.phase == "fire":
            self._fire(ev, ws)
        if ev.src == "fvr":
            self._track_mag(ev, ws)
        self._track_copies(ev, ws)
        rl = self._timeline_for(ev, ws)
        if rl is not None:
            self._on_event(ev, ws, rl)
            if not rl.closed and ws.open is rl and rl.done():
                self._close(ws, rl, "end" if rl.fvr_done else "native_idle", ev.t, ev.tick)

    # ---- fire / magazine tracking
    def _track_mag(self, ev: Ev, ws: WState) -> None:
        p = ev.phase
        if p == "unseat":
            ws.mag_out = True
        elif p == "seat":
            ws.mag_out = False
        elif p in ("cancel", "end", "complete"):
            ws.mag_out = False
        if isinstance(ev.get("mag_out"), bool):
            ws.mag_out = ev.get("mag_out")

    def _fire(self, ev: Ev, ws: WState) -> None:
        self.fire_events += 1
        result = ev.get("result", "shot")
        consumed = ev.get("consumed", 1 if result == "shot" else 0)
        loaded = ev.get("loaded")
        reasons = []
        if ws.mag_out:
            reasons.append("mag_out")
        if _is_int(loaded) and _is_int(consumed):
            if loaded + consumed <= 0:
                reasons.append("empty")
        if not reasons:
            return
        key = (ev.wkey, ev.tick)
        rec = self.fire_attempts.setdefault(key, {
            "tick": ev.tick, "qpc_us": ev.t, "weapon": _wlabel(ev.wkey),
            "reasons": set(), "results": set(), "copies": set()})
        rec["reasons"].update(reasons)
        rec["results"].add(result)
        if ev.copy:
            rec["copies"].add(ev.copy)

    # ---- copy agreement
    def _track_copies(self, ev: Ev, ws: WState) -> None:
        if ev.copy is None:
            return
        st = ws.copies.setdefault(ev.copy, {})
        raw, p = ev.raw, ev.phase
        if p == "firing_state" and _is_int(raw.get("to")):
            st["state"] = raw["to"]
        elif p == "step" and _is_int(raw.get("state")):
            st["state"] = raw["state"]
        if p in ("transfer", "restore"):
            loaded, reserve = raw.get("loaded_after"), raw.get("reserve_after")
        else:
            loaded, reserve = raw.get("loaded"), raw.get("reserve")
        if _is_int(loaded):
            st["loaded"] = loaded
        if _is_int(reserve):
            st["reserve"] = reserve

    def _flush(self) -> None:
        for wkey in self._touched:
            ws = self.weapons[wkey]
            diff = {}
            for f in ("state", "loaded", "reserve"):
                vals = {c: ws.copies[c][f] for c in COPIES if c in ws.copies and f in ws.copies[c]}
                if len(vals) >= 2 and len(set(vals.values())) > 1:
                    diff[f] = vals
            if diff:
                if ws.span is None:
                    ws.span = {"weapon": _wlabel(wkey), "start_tick": self._tick,
                               "start_t": self._now, "fields": {}}
                ws.span["fields"].update(diff)
            elif ws.span is not None:
                self._close_span(ws, self._tick, self._now, unresolved=False)
        self._touched = set()

    def _close_span(self, ws: WState, tick: int, t: float, unresolved: bool) -> None:
        sp = ws.span
        ws.span = None
        self.disagreements.append({
            "weapon": sp["weapon"], "start_tick": sp["start_tick"], "end_tick": tick,
            "ticks": tick - sp["start_tick"], "ms": round((t - sp["start_t"]) / 1000.0, 3),
            "fields": sp["fields"], "unresolved": unresolved})

    # ---- timelines
    def _opener(self, ev: Ev) -> str | None:
        p = ev.phase
        if p == "begin" and ev.src == "fvr":
            return "fvr"
        if p == "firing_state" and ev.get("to") in RELOAD_STATES:
            return "native"
        if p == "freeze_start":
            return "native"
        if p == "auto_reload" and not ev.get("suppressed", False):
            return "native"
        return None

    def _open(self, ws: WState, ev: Ev, origin: str) -> Reload:
        rl = Reload(len(self.reloads) + 1, ev.wkey, origin, ev.t, ev.tick, end_t=ev.t, end_tick=ev.tick)
        self.reloads.append(rl)
        ws.open = rl
        return rl

    def _close(self, ws: WState, rl: Reload, why: str, t: float, tick: int) -> None:
        rl.closed, rl.closed_by = True, why
        rl.end_t, rl.end_tick = max(rl.end_t, t), max(rl.end_tick, tick)
        if ws.open is rl:
            ws.open = None
        ws.last = rl

    def _timeline_for(self, ev: Ev, ws: WState) -> Reload | None:
        rl = ws.open
        op = self._opener(ev)
        if op:
            if rl is not None and op == "fvr" and rl.fvr_begun and not rl.fvr_done:
                self._close(ws, rl, "superseded", ev.t, ev.tick)
                rl = None
            if rl is None:
                rl = self._open(ws, ev, op)
            return rl
        if rl is None and ev.phase in ORPHAN_OK:
            last = ws.last
            if last is not None and ev.t - last.end_t <= ORPHAN_WINDOW_US:
                last.late.append(ev.phase)
                last.end_t, last.end_tick = max(last.end_t, ev.t), max(last.end_tick, ev.tick)
                return last
            return self._open(ws, ev, "orphan")
        return rl

    @staticmethod
    def _token(ev: Ev) -> str | None:
        p = ev.phase
        if p == "firing_state":
            return f"s{ev.get('to')}"
        if p in ("sample", "step", "haptic", "fire", "claim", "tape", "dropped", "commit", "weapon_switch"):
            return None
        return p

    def _snapshot(self, rl: Reload, ev: Ev, before, after, consumed: int, kind: str) -> None:
        if after is None and before is None:
            return
        key = ev.copy or "-"
        s = rl.snap.setdefault(key, {"start": None, "cur": None, "consumed": 0})
        if before is None:
            before = after + consumed
        if s["cur"] is None:
            s["start"] = before
        elif before != s["cur"]:
            rl.violations.append({"type": "fire_mismatch" if kind == "fire" else "drift",
                                  "tick": ev.tick, "copy": ev.copy, "phase": ev.phase,
                                  "expected": s["cur"], "got": before})
        if after is None:
            after = before - consumed
        if after != before - consumed:
            rl.violations.append({"type": "not_conserved", "tick": ev.tick, "copy": ev.copy,
                                  "phase": ev.phase, "expected": before - consumed, "got": after})
        s["cur"] = after
        s["consumed"] += consumed

    def _on_event(self, ev: Ev, ws: WState, rl: Reload) -> None:
        p, raw = ev.phase, ev.raw
        rl.end_t, rl.end_tick = max(rl.end_t, ev.t), max(rl.end_tick, ev.tick)
        if ev.src == "fvr":
            rl.fvr_seen = True
        tok = self._token(ev)
        if tok and (not rl.seq or rl.seq[-1] != tok):
            rl.seq.append(tok)
        cp = ev.copy or "-"
        if p == "begin":
            rl.fvr_begun = True
        elif p == "seat":
            rl.seats.append({"t": ev.t, "tick": ev.tick, "credit": None, "xfer": {}})
        elif p == "credit":
            k = rl.credit_n
            rl.credit_n += 1
            if k < len(rl.seats):
                rl.seats[k]["credit"] = (ev.t, ev.tick)
            else:
                rl.excess_credits += 1
            self._snapshot(rl, ev, *self._point(raw), 0, "point")
        elif p == "complete":
            rl.completed = True
        elif p == "cancel":
            rl.cancels.append(str(raw.get("reason", "unknown")))
        elif p == "end":
            rl.fvr_done = True
            rl.end_reason = raw.get("reason")
        elif p == "auto_reload":
            rl.native_seen = True
            rl.auto.append(bool(raw.get("suppressed", False)))
        elif p == "firing_state":
            to = raw.get("to")
            if to in RELOAD_STATES:
                rl.native_seen = rl.native_entered = True
                rl.active.add(cp)
            elif to == IDLE_STATE:
                rl.active.discard(cp)
        elif p == "freeze_start":
            rl.native_seen = True
            rl.freeze_open[cp] = ev.t
        elif p == "freeze_stop":
            t0 = rl.freeze_open.pop(cp, None)
            if t0 is not None:
                rl.freeze_ms.append((ev.t - t0) / 1000.0)
            elif _is_num(raw.get("held_us")):
                rl.freeze_ms.append(raw["held_us"] / 1000.0)
        elif p in ("transfer", "restore"):
            if p == "transfer":
                rl.native_seen = True
            lb, la = raw.get("loaded_before"), raw.get("loaded_after")
            rb, ra = raw.get("reserve_before"), raw.get("reserve_after")
            before, after = _ammo_sum(lb, rb), _ammo_sum(la, ra)
            if before is not None and after is not None:
                self._snapshot(rl, ev, before, after, 0, p)
            if p == "transfer" and _is_int(lb) and _is_int(la) and la > lb:
                k = rl.gain_n[cp]
                rl.gain_n[cp] += 1
                if k < len(rl.seats):
                    rl.seats[k]["xfer"][cp] = (ev.t, ev.tick)
                else:
                    rl.excess_gains[cp] += 1
        elif p == "fire":
            consumed = raw.get("consumed", 1 if raw.get("result", "shot") == "shot" else 0)
            after = _ammo_sum(raw.get("loaded"), raw.get("reserve"))
            if after is not None and _is_int(consumed):
                self._snapshot(rl, ev, None, after, consumed, "fire")
        elif p in ("step", "sample", "auto_reload"):
            self._snapshot(rl, ev, *self._point(raw), 0, "point")

    @staticmethod
    def _point(raw):
        s = _ammo_sum(raw.get("loaded"), raw.get("reserve"))
        return (s, s)


def _wlabel(wkey: tuple) -> str:
    asset, wid = wkey
    return f"{asset}#{wid}" if wid is not None else f"{asset}"


# --------------------------------------------------------------------------- reporting

def _ms(a: float, b: float) -> float:
    return round((b - a) / 1000.0, 3)


def reload_report(rl: Reload) -> dict:
    seats = []
    uncredited = 0
    for i, s in enumerate(rl.seats):
        entry: dict[str, Any] = {"seat_tick": s["tick"], "delay_ms": None, "delay_ticks": None,
                                 "source": None, "transfer_delay_ms": None}
        if s["xfer"]:
            cp, (t, tick) = min(s["xfer"].items(), key=lambda kv: kv[1][0])
            entry["transfer_delay_ms"] = _ms(s["t"], t)
            entry["transfer_copy"] = cp
        if s["credit"]:
            entry.update(delay_ms=_ms(s["t"], s["credit"][0]), delay_ticks=s["credit"][1] - s["tick"],
                         source="credit")
        elif s["xfer"]:
            cp, (t, tick) = min(s["xfer"].items(), key=lambda kv: kv[1][0])
            entry.update(delay_ms=_ms(s["t"], t), delay_ticks=tick - s["tick"], source="transfer")
        else:
            if not rl.cancels:
                uncredited += 1
        seats.append(entry)
    fvr_excess = rl.excess_credits
    nat_excess = max(rl.excess_gains.values(), default=0) if rl.fvr_seen else 0
    cons_copies = {}
    for key, s in rl.snap.items():
        cons_copies[key] = {"start": s["start"], "end": s["cur"], "consumed": s["consumed"]}
    seq = rl.seq
    return {
        "id": rl.rid,
        "weapon": _wlabel(rl.wkey),
        "origin": "both" if rl.fvr_seen and rl.native_seen else ("fvr" if rl.fvr_seen else rl.origin if rl.origin == "orphan" else "native"),
        "outcome": rl.outcome(),
        "closed_by": rl.closed_by,
        "end_reason": rl.end_reason,
        "start_tick": rl.start_tick,
        "end_tick": rl.end_tick,
        "duration_ms": _ms(rl.start_t, rl.end_t),
        "seats": seats,
        "uncredited_seats": uncredited,
        "credits": rl.credit_n,
        "native_gains": dict(rl.gain_n),
        "double_credits": fvr_excess + nat_excess,
        "double_credit_detail": {"credit_excess": fvr_excess, "transfer_excess": dict(rl.excess_gains) if rl.fvr_seen else {}},
        "conservation": {"ok": not [v for v in rl.violations if v["type"] != "freeze_leak"],
                         "copies": cons_copies,
                         "violations": [v for v in rl.violations if v["type"] != "freeze_leak"]},
        "auto_reloads": {"total": len(rl.auto), "unsuppressed": sum(1 for s in rl.auto if not s)},
        "cancels": list(rl.cancels),
        "freeze_ms": [round(x, 3) for x in rl.freeze_ms],
        "freeze_leaks": [v["copy"] for v in rl.violations if v["type"] == "freeze_leak"],
        "late_events": list(rl.late),
        "sequence": ">".join(seq),
    }


def _status(value, limit, higher_is_bad=True) -> str:
    if limit is None or value is None:
        return "INFO"
    return "FAIL" if value > limit else "PASS"


def evaluate(an: Analyzer, invalid: list[dict], th: Thresholds) -> dict:
    reloads = [reload_report(r) for r in an.reloads]
    checks: list[dict] = []

    def add(name, value, limit, detail=None, status=None, op="<="):
        checks.append({"name": name, "status": status or _status(value, limit),
                       "value": value, "limit": limit, "op": op, "detail": detail})

    add("invalid_lines", len(invalid), th.max_invalid_lines,
        invalid[:5] or None)
    add("dropped_events", an.dropped, th.max_dropped_events,
        "ammo checks are unreliable when events were dropped" if an.dropped else None)
    add("reloads_found", len(reloads), th.min_reloads, status=("FAIL" if len(reloads) < th.min_reloads else "PASS"), op=">=")

    delays = [(s["delay_ms"], s["delay_ticks"], r["id"]) for r in reloads for s in r["seats"] if s["delay_ms"] is not None]
    worst = max(delays, default=None)
    add("seat_credit_ms", worst[0] if worst else None, th.max_seat_credit_ms,
        {"reload": worst[2], "n": len(delays)} if worst else "no seat->credit pairs")
    worst_t = max(delays, key=lambda d: d[1], default=None)
    add("seat_credit_ticks", worst_t[1] if worst_t else None, th.max_seat_credit_ticks,
        {"reload": worst_t[2]} if worst_t else None)
    xf = [(s["transfer_delay_ms"], r["id"]) for r in reloads for s in r["seats"] if s["transfer_delay_ms"] is not None]
    wx = max(xf, default=None)
    add("seat_transfer_ms", wx[0] if wx else None, th.max_seat_transfer_ms, {"reload": wx[1]} if wx else None)
    add("uncredited_seats", sum(r["uncredited_seats"] for r in reloads), th.max_uncredited_seats,
        [r["id"] for r in reloads if r["uncredited_seats"]] or None)
    nviol = sum(len(r["conservation"]["violations"]) for r in reloads)
    add("conservation_violations", nviol, th.max_conservation_violations,
        [{"reload": r["id"], **v} for r in reloads for v in r["conservation"]["violations"]][:10] or None)
    add("double_credits", sum(r["double_credits"] for r in reloads), th.max_double_credits,
        [{"reload": r["id"], **r["double_credit_detail"]} for r in reloads if r["double_credits"]] or None)
    add("auto_reloads_unsuppressed", an.auto["unsuppressed"], th.max_auto_reloads_unsuppressed)
    add("auto_reloads", an.auto["total"], th.max_auto_reloads)

    attempts = sorted(an.fire_attempts.values(), key=lambda a: a["qpc_us"])
    leaks = [a for a in attempts if "shot" in a["results"]]
    def _ser(a):
        return {**a, "reasons": sorted(a["reasons"]), "results": sorted(a["results"]), "copies": sorted(a["copies"])}
    add("fire_leaks", len(leaks), th.max_fire_leaks, [_ser(a) for a in leaks][:10] or None)
    add("fire_attempts_unloaded", len(attempts), th.max_fire_attempts_unloaded)

    dis = an.disagreements
    worst_d = max(dis, key=lambda d: d["ticks"], default=None)
    add("copy_disagreement_ticks", worst_d["ticks"] if worst_d else 0, th.max_copy_disagreement_ticks, worst_d)
    worst_m = max(dis, key=lambda d: d["ms"], default=None)
    add("copy_disagreement_ms", worst_m["ms"] if worst_m else 0, th.max_copy_disagreement_ms)

    add("cancels", sum(an.cancels.values()), th.max_cancels, dict(an.cancels) or None)
    for reason, limit in th.max_cancels_by_reason.items():
        add(f"cancels[{reason}]", an.cancels.get(reason, 0), limit)
    for reason in th.deny_cancel_reasons:
        add(f"cancels_denied[{reason}]", an.cancels.get(reason, 0), 0)
    add("freeze_leaks", sum(len(r["freeze_leaks"]) for r in reloads), th.max_freeze_leaks,
        [r["id"] for r in reloads if r["freeze_leaks"]] or None)
    add("incomplete_reloads", sum(1 for r in reloads if r["outcome"] == "incomplete"), th.max_incomplete_reloads)
    add("stale_samples", an.stale_samples, th.max_stale_samples)

    result = "FAIL" if any(c["status"] == "FAIL" for c in checks) else "PASS"
    return {
        "schema": "fvr.gun_event_analysis.v1",
        "result": result,
        "headset_accepted": False,
        "thresholds": asdict(th),
        "summary": {
            "reloads": len(reloads),
            "outcomes": dict(Counter(r["outcome"] for r in reloads)),
            "fire_events": an.fire_events,
            "weapons": sorted(_wlabel(k) for k in an.weapons),
            "phases": dict(an.phase_counts),
            "unknown_phases": dict(an.unknown_phases),
            "tape_events": an.tape_events,
        },
        "checks": checks,
        "reloads": reloads,
        "copy_disagreements": dis,
        "fire_attempts": [_ser(a) for a in attempts],
        "cancels": dict(an.cancels),
        "invalid_lines": invalid,
    }


def analyze(paths: Iterable[Path], th: Thresholds | None = None) -> dict:
    events, invalid = read_events(paths)
    an = Analyzer()
    an.run(events)
    out = evaluate(an, invalid, th or Thresholds())
    out["summary"]["events"] = len(events)
    return out


def analyze_lines(lines: Iterable[str], th: Thresholds | None = None) -> dict:
    """Analyze in-memory JSONL lines (used by tests)."""
    events, invalid = [], []
    for i, line in enumerate(lines, 1):
        line = line.strip()
        if not line:
            continue
        try:
            events.append(parse_event(json.loads(line), i))
        except (ValueError, TypeError) as exc:
            invalid.append({"file": "<memory>", "line": i, "error": str(exc)})
    events.sort(key=lambda e: (e.t, e.tick, e.order))
    an = Analyzer()
    an.run(events)
    out = evaluate(an, invalid, th or Thresholds())
    out["summary"]["events"] = len(events)
    return out


def render_text(rep: dict, verbose: bool = False) -> str:
    out = []
    s = rep["summary"]
    out.append(f"gun event analysis: {s['events']} events, {s['reloads']} reloads "
               f"({', '.join(f'{k} {v}' for k, v in s['outcomes'].items()) or 'none'}), "
               f"{len(rep['invalid_lines'])} invalid lines")
    out.append(f"weapons: {', '.join(s['weapons']) or '-'}")
    if s["unknown_phases"]:
        out.append(f"unknown phases (ignored): {s['unknown_phases']}")
    for r in rep["reloads"]:
        out.append("")
        out.append(f"RELOAD #{r['id']} {r['weapon']} [{r['origin']}] {r['outcome']} "
                   f"ticks {r['start_tick']}..{r['end_tick']} ({r['duration_ms']:.1f} ms)"
                   + (f" end={r['end_reason']}" if r["end_reason"] else ""))
        for sd in r["seats"]:
            if sd["delay_ms"] is None:
                out.append(f"  seat @tick {sd['seat_tick']}: NO CREDIT")
            else:
                extra = (f", native transfer +{sd['transfer_delay_ms']:.1f} ms"
                         if sd["transfer_delay_ms"] is not None and sd["source"] == "credit" else "")
                out.append(f"  seat @tick {sd['seat_tick']}: credit +{sd['delay_ms']:.1f} ms / "
                           f"{sd['delay_ticks']} ticks ({sd['source']}){extra}")
        cons = r["conservation"]
        for key, c in cons["copies"].items():
            out.append(f"  ammo[{key}]: {c['start']} -> {c['end']} (shots consumed {c['consumed']})")
        for v in cons["violations"]:
            out.append(f"  CONSERVATION {v['type']} tick {v['tick']} copy={v['copy']} "
                       f"expected {v['expected']} got {v['got']}")
        if r["double_credits"]:
            out.append(f"  DOUBLE CREDIT x{r['double_credits']} {r['double_credit_detail']}")
        if r["cancels"]:
            out.append(f"  cancels: {', '.join(r['cancels'])}")
        if r["freeze_ms"]:
            out.append(f"  freeze: {', '.join(f'{x:.0f} ms' for x in r['freeze_ms'])}")
        if r["late_events"]:
            out.append(f"  late events after close: {', '.join(r['late_events'])}")
        if verbose:
            out.append(f"  seq: {r['sequence']}")
    for d in rep["copy_disagreements"]:
        fields_ = ", ".join(f"{f}={v}" for f, v in d["fields"].items())
        out.append(f"COPY DISAGREEMENT {d['weapon']} ticks {d['start_tick']}..{d['end_tick']} "
                   f"({d['ticks']} ticks, {d['ms']:.1f} ms){' UNRESOLVED' if d['unresolved'] else ''}: {fields_}")
    for a in rep["fire_attempts"]:
        out.append(f"FIRE while {'+'.join(a['reasons'])} {a['weapon']} tick {a['tick']} -> {'/'.join(a['results'])}")
    out.append("")
    out.append("CHECKS")
    for c in rep["checks"]:
        if c["status"] == "INFO" and not c["value"]:
            continue
        lim = "no limit" if c["limit"] is None else f"{c['op']} {c['limit']}"
        val = "-" if c["value"] is None else c["value"]
        out.append(f"  {c['status']:<4} {c['name']}: {val} ({lim})")
    out.append("")
    out.append(f"RESULT: {rep['result']}")
    return "\n".join(out)


# --------------------------------------------------------------------------- CLI

def _opt_num(kind):
    def conv(text: str):
        if text.lower() in ("none", "off", "null"):
            return None
        return kind(text)
    return conv


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description="Analyze FrostbiteVR gun-event JSONL logs (docs/GUN-EVENT-LOG.md).")
    ap.add_argument("logs", nargs="*", type=Path, help="one or more .jsonl files (merged by qpc_us, tick)")
    ap.add_argument("--json", action="store_true", help="print the report as JSON instead of text")
    ap.add_argument("-v", "--verbose", action="store_true", help="show per-reload event sequences")
    ap.add_argument("--rules", type=Path, help="JSON file of thresholds (keys = flag names with underscores)")
    ap.add_argument("--dump-rules", action="store_true", help="print the effective thresholds as JSON and exit")
    for f in fields(Thresholds):
        flag = "--" + f.name.replace("_", "-")
        if f.name == "max_cancels_by_reason":
            ap.add_argument(flag, action="append", metavar="REASON=N", help="per-reason cancel limit (repeatable)")
        elif f.name == "deny_cancel_reasons":
            ap.add_argument("--deny-cancel-reason", dest="deny_cancel_reasons", action="append", metavar="REASON",
                            help="fail if this cancel reason occurs (repeatable)")
        else:
            kind = float if f.name in ("max_seat_credit_ms", "max_seat_transfer_ms", "max_copy_disagreement_ms") else int
            ap.add_argument(flag, type=_opt_num(kind), default=argparse.SUPPRESS, metavar="N|none")
    return ap


def main(argv: list[str] | None = None) -> int:
    ap = build_parser()
    args = ap.parse_args(argv)
    try:
        th = Thresholds()
        if args.rules:
            th = thresholds_from_mapping(json.loads(args.rules.read_text(encoding="utf-8")), th)
        overrides: dict[str, Any] = {}
        for f in fields(Thresholds):
            if f.name == "max_cancels_by_reason" and args.max_cancels_by_reason:
                m = dict(th.max_cancels_by_reason)
                for item in args.max_cancels_by_reason:
                    reason, _, n = item.partition("=")
                    m[reason] = int(n)
                overrides[f.name] = m
            elif f.name == "deny_cancel_reasons" and args.deny_cancel_reasons:
                overrides[f.name] = list(th.deny_cancel_reasons) + args.deny_cancel_reasons
            elif f.name in args and f.name not in ("max_cancels_by_reason", "deny_cancel_reasons"):
                overrides[f.name] = getattr(args, f.name)
        th = thresholds_from_mapping(overrides, th)
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    if args.dump_rules:
        print(json.dumps(asdict(th), indent=2))
        return 0
    if not args.logs:
        ap.error("at least one log file is required")
    missing = [p for p in args.logs if not p.is_file()]
    if missing:
        print(f"error: not a file: {', '.join(map(str, missing))}", file=sys.stderr)
        return 2
    rep = analyze(args.logs, th)
    print(json.dumps(rep, indent=2) if args.json else render_text(rep, args.verbose))
    return 0 if rep["result"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
