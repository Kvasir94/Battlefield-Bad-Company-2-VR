#!/usr/bin/env python3
"""BC2 developer test range: parse, validate and prepare range files.

A range (docs/BC2_TEST_RANGE.md) says which level a replay or headset session
starts in and which weapons the player holds. This tool is the runner half; the
in-game half is fvr::bc2::TestRange, which reads the same text with
src/runtime/TestRange.cpp. Both must agree byte for byte: the golden files in
tests/fixtures/test-range are checked by the C++ and the Python suites.

This tool never launches the game or writes its memory. `prepare` writes a
fresh run directory (canonical range.ini plus range-run.json) for the replay
runner and the PC launcher; the probe reads range.ini. `stage`/`unstage`
optionally rename the intro movies aside and back. `analyze` judges the
`phase:"range"` rows of a gun event log (docs/GUN-EVENT-LOG.md v1).

  python tools/bc2_test_range.py check tools/test-range/ranges/*.ini --catalog reports/weapon-configurations.json
  python tools/bc2_test_range.py settings xm8-spas
  python tools/bc2_test_range.py prepare --range sp02-xm8 --tape t.fvrtape --out runs/0001
  python tools/bc2_test_range.py analyze runs/0001/gun-events.jsonl --range runs/0001/range.ini
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

HEADER = "FVR_BC2_RANGE 1"
MAX_WEAPONS = 8
MAX_SETTINGS = 32
MAX_LINE_BYTES = 512
MAX_FILE_BYTES = 64 * 1024
MAX_TIMEOUT_S = 3600
MAX_ROUNDS = 100000
SLOT_NAMES = ("Primary1", "Primary2", "GrenadeLauncher1", "GrenadeLauncher2",
              "StaticGadget", "DynamicGadget1", "DynamicGadget2", "MeleeWeapon", "HandGrenade")
ROUTES = ("menu", "override", "manual")
LEVEL_IDS = ("sp_01", "sp_02", "sp_03", "sp_04", "sp_003_b", "sp_004_b",
             "sp_005", "sp_005_b", "sp_006", "sp_007", "sp_008", "sp_008_b", "sp_009")
# Optional, reversible runner step: intro movies renamed aside (PC, 2026-10-08).
INTRO_MOVIES = ("movies/ea_logo_hd.res", "movies/dolbydigital.res")
DISABLED_SUFFIX = ".fvr-disabled"
STAGE_JOURNAL = "stage-journal.json"
STAGE_SCHEMA = "fvr.bc2.test_range_stage.v1"
RANGES_DIR = Path(__file__).resolve().parent / "test-range" / "ranges"
RUN_SCHEMA = "fvr.bc2.test_range_run.v1"
ANALYSIS_SCHEMA = "fvr.bc2.test_range_analysis.v1"
# Launch switches emitted only when present, in this order (key, setting, invert).
BOOL_SWITCHES = (
    ("menu", "Client.LoadMenu", False),
    ("skip_intro", "Client.DisableMovies", False),
    ("fast_startup", "Client.IsFastStartupEnabled", False),
    ("cutscenes", "Server.DisableCutscenes", True),
    ("ai", "Server.IsAiEnabled", False),
    ("god", "Server.IsGodMode", False),
    ("checkpoints", "Server.DisableCheckpoints", True),
)
_ASSET = re.compile(r"[A-Za-z0-9][A-Za-z0-9_-]{0,63}")
_IDENT = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,63}")
_SETTING_PART = re.compile(r"[A-Za-z][A-Za-z0-9_]*")
_DECIMAL = re.compile(r"-?[0-9]+(\.[0-9]+)?")
_BAD_VALUE_CHARS = set("\"'\\`$&|;<>%^")


class RangeError(ValueError):
    pass


@dataclass
class AmmoOverride:
    asset: str
    full: bool = False
    loaded: int = -1
    reserve: int = -1


@dataclass
class Range:
    name: str = ""
    notes: str = ""
    level: str = ""
    route: str = "menu"
    spawn: str = ""
    savepoint: str = ""
    weapons: list = field(default_factory=list)
    slots: list = field(default_factory=list)
    select: str = ""
    clear_inventory: bool = True
    skip_cinematics: bool = False
    ammo: str = "native"
    ammo_overrides: list = field(default_factory=list)
    teleport: str = ""
    switches: dict = field(default_factory=dict)  # optional booleans by key
    kit: int | None = None
    team: int | None = None
    fixed_dt: str = ""
    timeout: int = 180
    ready_timeout: int = 90
    settings: list = field(default_factory=list)  # [(Class.Field, value)]

    @property
    def selected(self) -> str:
        return self.select or (self.weapons[0] if self.weapons else "")


def valid_asset(s: str) -> bool:
    return bool(_ASSET.fullmatch(s))


def valid_identifier(s: str) -> bool:
    return bool(_IDENT.fullmatch(s))


def valid_level(s: str) -> bool:
    return len(s) >= 8 and s[:7].lower() == "levels/" and s[7:].lower() in LEVEL_IDS


def valid_setting_key(s: str) -> bool:
    parts = s.split(".")
    return len(s) <= 96 and len(parts) == 2 and all(_SETTING_PART.fullmatch(p) for p in parts)


def valid_setting_value(s: str) -> bool:
    if not s or len(s) > 128 or s[0] == "-":
        return False
    return all(0x20 < ord(c) < 0x7F and c not in _BAD_VALUE_CHARS for c in s)


def _bool(value: str) -> bool | None:
    return {"true": True, "false": False}.get(value)


def _unsigned(value: str, lo: int, hi: int) -> int | None:
    if not value or len(value) > 10 or not value.isascii() or not value.isdigit() or (len(value) > 1 and value[0] == "0"):
        return None
    n = int(value)
    return n if lo <= n <= hi else None


def _decimal(value: str, lo: float, hi: float) -> bool:
    if len(value) > 24 or not _DECIMAL.fullmatch(value):
        return False
    v = float(value)
    return math.isfinite(v) and lo <= v <= hi


def _split(value: str, sep: str) -> list:
    return [p.strip(" \t") for p in value.split(sep)]


def apply_key(r: Range, key: str, value: str, seen: list) -> None:
    if not key:
        raise RangeError("empty key")
    if key in seen:
        raise RangeError(f"duplicate key {key}")
    bad = RangeError(f"invalid value for {key}")
    if key == "name":
        if not valid_identifier(value):
            raise bad
        r.name = value
    elif key == "notes":
        if len(value) > 256 or not all(0x20 <= ord(c) < 0x7F for c in value):
            raise bad
        r.notes = value
    elif key == "level":
        if not valid_level(value):
            raise bad
        r.level = value
    elif key == "route":
        if value not in ROUTES:
            raise bad
        r.route = value
    elif key in ("spawn", "savepoint"):
        if not valid_identifier(value):
            raise bad
        setattr(r, key, value)
    elif key == "weapons":
        items = value
        if items.startswith("["):
            if len(items) < 2 or not items.endswith("]"):
                raise bad
            items = items[1:-1]
        weapons, slots = [], []
        for item in _split(items, ","):
            slot = -1
            if "@" in item:
                item, name = item.split("@", 1)
                if name not in SLOT_NAMES:
                    raise RangeError(f"unknown weapon slot {name}")
                slot = SLOT_NAMES.index(name)
                if slot in slots:
                    raise RangeError(f"duplicate weapon slot {name}")
            if not valid_asset(item):
                raise bad
            if any(w.lower() == item.lower() for w in weapons):
                raise RangeError(f"duplicate weapon {item}")
            weapons.append(item)
            slots.append(slot)
        if not weapons or len(weapons) > MAX_WEAPONS:
            raise bad
        r.weapons, r.slots = weapons, slots
    elif key == "select":
        if not valid_asset(value):
            raise bad
        r.select = value
    elif key in ("clear_inventory", "skip_cinematics"):
        b = _bool(value)
        if b is None:
            raise bad
        setattr(r, key, b)
    elif key == "ammo":
        if value not in ("native", "full"):
            raise bad
        r.ammo = value
    elif key.startswith("ammo."):
        o = AmmoOverride(key[5:])
        if not valid_asset(o.asset):
            raise RangeError(f"invalid asset in {key}")
        if any(e.asset.lower() == o.asset.lower() for e in r.ammo_overrides):
            raise RangeError(f"duplicate key {key}")
        if value == "full":
            o.full = True
        else:
            parts = _split(value, "/")
            if len(parts) != 2:
                raise bad
            for n, part in enumerate(parts):
                if part == "native":
                    continue
                v = _unsigned(part, 0, MAX_ROUNDS)
                if v is None:
                    raise bad
                if n == 0:
                    o.loaded = v
                else:
                    o.reserve = v
            if o.loaded < 0 and o.reserve < 0:
                raise bad
        if len(r.ammo_overrides) >= MAX_WEAPONS:
            raise bad
        r.ammo_overrides.append(o)
    elif key == "teleport":
        parts = _split(value, ",")
        if len(parts) not in (3, 4):
            raise bad
        for n, p in enumerate(parts):
            lim = 360.0 if n == 3 else 100000.0
            if not _decimal(p, -lim, lim):
                raise bad
        r.teleport = value
    elif key in ("menu", "skip_intro", "fast_startup", "cutscenes", "ai", "god", "checkpoints", "unlocks",
                 "instant_spawn", "unlimited_ammo", "unlimited_mags", "scripted_input"):
        b = _bool(value)
        if b is None:
            raise bad
        r.switches[key] = b
    elif key in ("kit", "team"):
        v = _unsigned(value, 0, 13 if key == "kit" else 15)
        if v is None:
            raise bad
        setattr(r, key, v)
    elif key == "fixed_dt":
        if not _decimal(value, 0.001, 0.1):
            raise bad
        r.fixed_dt = value
    elif key in ("timeout", "ready_timeout"):
        v = _unsigned(value, 1, MAX_TIMEOUT_S)
        if v is None:
            raise bad
        setattr(r, key, v)
    elif key.startswith("set."):
        setting = key[4:]
        if not valid_setting_key(setting):
            raise RangeError(f"invalid setting key {key}")
        if not valid_setting_value(value):
            raise bad
        if any(k.lower() == setting.lower() for k, _ in r.settings):
            raise RangeError(f"duplicate key {key}")
        if len(r.settings) >= MAX_SETTINGS:
            raise RangeError("too many set.* keys")
        r.settings.append((setting, value))
    else:
        raise RangeError(f"unknown key {key}")
    seen.append(key)


def range_settings(r: Range) -> list:
    """Engine settings pairs in the fixed order shared with RangeSettings()."""
    out = []
    t = lambda b: "true" if b else "false"
    if r.level:
        out.append(("Client.Level", r.level))  # newSP loads Client.Level
    if r.spawn:
        out.append(("Client.SpawnPoint", r.spawn))
    if r.savepoint:
        out += [("Server.SavePoint", r.savepoint), ("Server.LoadSavePoint", "true")]
    s = r.switches
    for key, setting, invert in BOOL_SWITCHES:
        if key in s:
            out.append((setting, t(s[key] != invert)))
    if "unlocks" in s:
        out += [("Server.UnrestrictedUnlocks", t(s["unlocks"])), ("Client.UnrestrictedUnlocks", t(s["unlocks"]))]
    for key, setting in (("instant_spawn", "Server.InstantSpawn"), ("unlimited_ammo", "Server.HasUnlimitedAmmo"),
                         ("unlimited_mags", "Server.HasUnlimitedMags")):
        if key in s:
            out.append((setting, t(s[key])))
    if r.kit is not None:
        out.append(("Client.Kit", str(r.kit)))
    if r.team is not None:
        out.append(("Client.Team", str(r.team)))
    if r.fixed_dt:
        out.append(("Game.ForceDeltaTime", r.fixed_dt))
    if "scripted_input" in s:
        out.append(("Client.UseScriptedInput", t(s["scripted_input"])))
    out += list(r.settings)
    return out


def validate(r: Range) -> None:
    if not r.level:
        raise RangeError("missing level")
    if not r.weapons:
        raise RangeError("missing weapons")
    if r.select and r.select not in r.weapons:
        raise RangeError("select is not in weapons")
    for o in r.ammo_overrides:
        if o.asset not in r.weapons:
            raise RangeError(f"ammo.{o.asset} is not in weapons")
    if r.ready_timeout > r.timeout:
        raise RangeError("ready_timeout exceeds timeout")
    if r.route == "manual" and (r.spawn or r.savepoint):
        raise RangeError("spawn/savepoint are level-load settings and need route=menu or override")
    pairs = range_settings(r)
    for i, (a, _) in enumerate(pairs):
        for b, _ in pairs[i + 1:]:
            if a.lower() == b.lower():
                raise RangeError(f"setting {a} is set twice")


def parse(text: str | bytes) -> Range:
    if isinstance(text, bytes):
        if len(text) > MAX_FILE_BYTES:
            raise RangeError("range file too large")
        if text.startswith(b"\xef\xbb\xbf"):
            raise RangeError("UTF-8 BOM is not allowed")
        try:
            text = text.decode("ascii")
        except UnicodeDecodeError as e:
            line = text[:e.start].count(b"\n") + 1
            raise RangeError(f"line {line}: non-ASCII or control character") from None
    if text.startswith("﻿"):
        raise RangeError("UTF-8 BOM is not allowed")
    r, seen, header = Range(), [], False
    lines = text.split("\n")
    if lines and lines[-1] == "" and len(lines) > 1:
        lines.pop()
    for n, raw in enumerate(lines, 1):
        where = f"line {n}: "
        if len(raw.encode("latin-1", "replace")) > MAX_LINE_BYTES:
            raise RangeError(where + "line too long")
        if any((ord(c) < 0x20 and c not in "\t\r") or ord(c) >= 0x7F for c in raw):
            raise RangeError(where + "non-ASCII or control character")
        s = raw.strip(" \t\r")
        if not s or s[0] in "#;":
            continue
        if not header:
            if s != HEADER:
                raise RangeError(where + "expected header " + HEADER)
            header = True
            continue
        if "=" not in s:
            raise RangeError(where + "expected key=value")
        key, value = s.split("=", 1)
        try:
            apply_key(r, key.strip(" \t"), value.strip(" \t"), seen)
        except RangeError as e:
            raise RangeError(where + str(e)) from None
    if not header:
        raise RangeError("missing header " + HEADER)
    validate(r)
    return r


def format_range(r: Range) -> str:
    """Canonical text, identical to fvr::runtime::FormatRange."""
    out = [HEADER]
    put = lambda k, v: out.append(f"{k}={v}")
    t = lambda b: "true" if b else "false"
    if r.name:
        put("name", r.name)
    if r.notes:
        put("notes", r.notes)
    put("level", r.level)
    put("route", r.route)
    if r.spawn:
        put("spawn", r.spawn)
    if r.savepoint:
        put("savepoint", r.savepoint)
    put("weapons", ",".join(w + (f"@{SLOT_NAMES[s]}" if s >= 0 else "") for w, s in zip(r.weapons, r.slots)))
    if r.select:
        put("select", r.select)
    put("clear_inventory", t(r.clear_inventory))
    put("skip_cinematics", t(r.skip_cinematics))
    put("ammo", r.ammo)
    for o in r.ammo_overrides:
        n = lambda v: "native" if v < 0 else str(v)
        put("ammo." + o.asset, "full" if o.full else f"{n(o.loaded)}/{n(o.reserve)}")
    if r.teleport:
        put("teleport", r.teleport)
    for key in ("menu", "skip_intro", "fast_startup", "cutscenes", "ai", "god", "checkpoints", "unlocks",
                "instant_spawn", "unlimited_ammo", "unlimited_mags"):
        if key in r.switches:
            put(key, t(r.switches[key]))
    if r.kit is not None:
        put("kit", r.kit)
    if r.team is not None:
        put("team", r.team)
    if r.fixed_dt:
        put("fixed_dt", r.fixed_dt)
    if "scripted_input" in r.switches:
        put("scripted_input", t(r.switches["scripted_input"]))
    put("timeout", r.timeout)
    put("ready_timeout", r.ready_timeout)
    for k, v in r.settings:
        put("set." + k, v)
    return "\n".join(out) + "\n"


def resolve_path(name_or_path: str) -> Path:
    p = Path(name_or_path)
    if p.suffix == ".ini" or p.exists() or "/" in name_or_path or "\\" in name_or_path:
        return p
    if not valid_identifier(name_or_path):
        raise RangeError(f"invalid range name {name_or_path}")
    return RANGES_DIR / f"{name_or_path}.ini"


def load(name_or_path: str) -> tuple:
    path = resolve_path(name_or_path)
    try:
        data = path.read_bytes()
    except OSError as e:
        raise RangeError(f"{path}: {e.strerror}") from None
    try:
        return parse(data), path
    except RangeError as e:
        raise RangeError(f"{path}: {e}") from None


def tape_header(tape_text: str) -> dict:
    """`#!` tokens of an FVR_BC2_INPUT tape, except `#! expect` lines.

    Returns {"range": name|None, "overrides": [(key, value)], "weapon": str|None}.
    Tokens follow the tape rule `key`, `key=value` or `key<op>value`.
    """
    out = {"range": None, "overrides": [], "weapon": None}
    lines = tape_text.splitlines()
    if not lines or not lines[0].startswith("FVR_BC2_INPUT "):
        raise RangeError("not an FVR_BC2_INPUT tape")
    for line in lines[1:]:
        if not line.startswith("#!"):
            continue
        tokens = line[2:].split()
        if tokens and tokens[0] == "expect":
            continue
        for tok in tokens:
            m = re.match(r"([A-Za-z0-9_.]+)(<=|>=|!=|=|<|>)?(.*)$", tok)
            if not m:
                continue
            key, op, value = m.group(1), m.group(2), m.group(3)
            if key == "range":
                if op != "=" or out["range"] is not None:
                    raise RangeError("tape must carry at most one range=<name>")
                out["range"] = value
            elif key.startswith("range."):
                if op != "=":
                    raise RangeError(f"tape token {tok} must use =")
                out["overrides"].append((key[6:], value))
            elif key == "weapon" and op == "=":
                out["weapon"] = value
    return out


def _apply_tape_override(r: Range, key: str, value: str, seen: list) -> None:
    # Overrides replace earlier file values: drop the file's value first.
    defaults = Range()
    if key == "weapons":
        r.weapons, r.slots = [], []
    elif key.startswith("ammo."):
        r.ammo_overrides = [o for o in r.ammo_overrides if o.asset.lower() != key[5:].lower()]
    elif key.startswith("set."):
        r.settings = [s for s in r.settings if s[0].lower() != key[4:].lower()]
    elif key in r.switches:
        del r.switches[key]
    elif hasattr(defaults, key) and key not in ("switches", "settings", "ammo_overrides", "slots"):
        setattr(r, key, getattr(defaults, key))
    apply_key(r, key, value, seen)


def apply_tape_overrides(r: Range, header: dict) -> Range:
    seen: list = []
    for key, value in header["overrides"]:
        _apply_tape_override(r, key, value, seen)
    validate(r)
    weapon = header.get("weapon")
    if weapon and not any(w == weapon or w.lower().startswith(weapon.lower()) for w in r.weapons):
        raise RangeError(f"tape precondition weapon={weapon} is not in the range weapons {','.join(r.weapons)}")
    return r


def load_catalog(path: Path) -> set:
    """Weapon names from tools/bc2_weapon_config_pipeline.py output.

    Accepts its `fvr.bc2.authored_weapon_configuration` JSON (resolved_weapons[].
    native_name, read from sp_common/level-00.fbrb), a JSON list of names, or a
    text file with one name per line.
    """
    text = path.read_text(encoding="utf-8")
    try:
        doc = json.loads(text)
    except json.JSONDecodeError:
        return {line.strip() for line in text.splitlines() if line.strip() and not line.startswith("#")}
    if isinstance(doc, list):
        return {str(x) for x in doc}
    if isinstance(doc, dict) and isinstance(doc.get("resolved_weapons"), list):
        return {w["native_name"] for w in doc["resolved_weapons"] if isinstance(w, dict) and w.get("native_name")}
    raise RangeError(f"{path}: not a weapon catalog")


def check_catalog(r: Range, catalog: set | None) -> None:
    if catalog is None:
        return
    missing = [w for w in r.weapons if w not in catalog]
    if missing:
        raise RangeError(f"weapons not in the catalog: {','.join(missing)}")


def _journal(run: Path) -> Path:
    return run / STAGE_JOURNAL


def _sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def stage(game: Path, run: Path) -> dict:
    """Rename the intro movies aside, reversibly (optional runner step).

    Every change is journaled in the run directory before it is made, so
    `unstage` (or a later recovery) restores the game folder exactly. Refuses
    to stage twice, or over an earlier run's unrestored leftovers. This is the
    only game-folder change the runner makes; the command line and
    Win32Game.cfg do not reach the settings registry (docs/BC2_TEST_RANGE.md).
    """
    if _journal(run).exists():
        raise RangeError(f"{_journal(run)} exists; unstage that run first")
    if not (game / "BFBC2Game.exe").is_file():
        raise RangeError(f"{game} has no BFBC2Game.exe")
    actions = []
    for rel in INTRO_MOVIES:
        src = game / rel
        dst = src.with_name(src.name + DISABLED_SUFFIX)
        if dst.exists():
            raise RangeError(f"{dst} already exists from an earlier run; restore it first")
        if src.is_file():
            actions.append({"kind": "movie", "target": rel, "sha256": _sha(src)})
    journal = {"schema": STAGE_SCHEMA, "game": str(game.resolve()), "actions": actions, "restored": False}
    run.mkdir(parents=True, exist_ok=True)
    _journal(run).write_text(json.dumps(journal, indent=2) + "\n", encoding="utf-8")
    for a in actions:
        src = game / a["target"]
        src.rename(src.with_name(src.name + DISABLED_SUFFIX))
    return journal


def unstage(run: Path) -> dict:
    """Undo `stage` from its journal. Idempotent; never deletes a file it did not write."""
    jp = _journal(run)
    if not jp.exists():
        raise RangeError(f"{jp} not found; nothing staged")
    journal = json.loads(jp.read_text(encoding="utf-8"))
    if journal.get("schema") != STAGE_SCHEMA:
        raise RangeError(f"{jp}: unsupported journal")
    game = Path(journal["game"])
    problems = []
    for a in reversed(journal["actions"]):
        src = game / a["target"]
        dst = src.with_name(src.name + DISABLED_SUFFIX)
        if dst.exists() and not src.exists():
            dst.rename(src)
        elif not src.exists():
            problems.append(f"{src} and its renamed copy are both missing")
        elif dst.exists():
            problems.append(f"{src} reappeared while {dst} still exists; left both in place")
    journal["restored"] = not problems
    journal["problems"] = problems
    jp.write_text(json.dumps(journal, indent=2) + "\n", encoding="utf-8")
    if problems:
        raise RangeError("; ".join(problems))
    jp.rename(run / (STAGE_JOURNAL + ".restored"))
    return journal


CFG_NAME = "BC2TestRange.cfg"


def cfg_text(r: Range) -> str:
    """BC2TestRange.cfg for the PC launcher (bc2/test/range tools/testrange).

    Same "Key value" line format as the engine's dead text-config loader; the
    injected DLL applies each pair through SettingsManager::Set (0x4f4bd0) and
    reads it back. The range name and digest go in comments only.
    """
    canonical = format_range(r)
    lines = [f"# FrostbiteVR test range {r.name or '-'} ({hashlib.sha256(canonical.encode('ascii')).hexdigest()})",
             "# generated by tools/bc2_test_range.py; do not edit"]
    lines += [f"{k} {v}" for k, v in range_settings(r)]
    return "\n".join(lines) + "\n"


def run_document(r: Range, source: str, tape: str | None = None, tape_sha: str | None = None) -> dict:
    canonical = format_range(r)
    return {
        "schema": RUN_SCHEMA,
        "range": r.name,
        "route": r.route,
        "level": r.level,
        "settings": [[k, v] for k, v in range_settings(r)],
        "weapons": list(r.weapons),
        "select": r.selected,
        "timeout_s": r.timeout,
        "ready_timeout_s": r.ready_timeout,
        "range_sha256": hashlib.sha256(canonical.encode("ascii")).hexdigest(),
        "source": source,
        "tape": tape,
        "tape_sha256": tape_sha,
        "intro_movies": list(INTRO_MOVIES),
        "cfg": CFG_NAME,
        "launch_owned_by": "tools/testrange/Start-BC2TestRange.ps1 (bc2/test/range)",
        "launch": ["Start-BC2TestRange.ps1", "-GamePath", "<BC2 dir>", "-Bin", "<dir holding BC2TestRange.dll, BC2TestRangeInject.exe and this cfg>",
                   "-InjectDelayMs", "3000", "-Seconds", str(min(max(r.timeout, 10), 600))],
        "level_route_proven": False,
    }


def prepare(range_ref: str | None, tape: Path | None, out: Path, catalog: set | None = None) -> dict:
    header = None
    tape_sha = None
    if tape is not None:
        raw = tape.read_bytes()
        tape_sha = hashlib.sha256(raw).hexdigest()
        header = tape_header(raw.decode("ascii", "replace"))
        if range_ref and header["range"] and header["range"] != range_ref:
            raise RangeError(f"--range {range_ref} disagrees with tape range={header['range']}")
        range_ref = range_ref or header["range"]
    if not range_ref:
        raise RangeError("no range: pass --range or a tape with #! range=<name>")
    r, path = load(range_ref)
    if header is not None:
        r = apply_tape_overrides(r, header)
    check_catalog(r, catalog)
    if out.exists() and any(out.iterdir()):
        raise RangeError(f"{out} is not empty; each run needs a fresh directory")
    out.mkdir(parents=True, exist_ok=True)
    canonical = format_range(r)
    (out / "range.ini").write_bytes(canonical.encode("ascii"))
    (out / CFG_NAME).write_bytes(cfg_text(r).encode("ascii"))
    doc = run_document(r, str(path), str(tape) if tape else None, tape_sha)
    (out / "range-run.json").write_text(json.dumps(doc, indent=2) + "\n", encoding="ascii", newline="\n")
    return doc


def range_for_scenario(scenario: dict, tape: Path | None, out: Path, catalog: set | None = None) -> dict | None:
    """Integration hook for tools/replay/run.py (bc2/tools/replay-harness).

    The range comes from the replay scenario's optional `"range"` key or the
    tape's `#! range=<name>`; both may be present only if they agree. Returns
    None when neither names a range (today's attach-at-checkpoint flow). The
    scenario's `"weapon"` must be one of the range weapons. Writes range.ini,
    BC2TestRange.cfg and range-run.json into `out` (a fresh per-attempt directory).
    """
    name = scenario.get("range")
    if name is not None and not isinstance(name, str):
        raise RangeError("scenario range must be a range name")
    header = tape_header(tape.read_bytes().decode("ascii", "replace")) if tape is not None else None
    if header is not None and header["range"] and name and header["range"] != name:
        raise RangeError(f"scenario range {name} disagrees with tape range={header['range']}")
    if not name and not (header and header["range"]):
        return None
    doc = prepare(name, tape, out, catalog)
    weapon = scenario.get("weapon")
    if weapon and weapon not in doc["weapons"]:
        raise RangeError(f"scenario weapon {weapon} is not in range {doc['range']} weapons {','.join(doc['weapons'])}")
    return doc


def range_rows(paths: list) -> list:
    rows = []
    for p in paths:
        with open(p, encoding="utf-8") as f:
            for n, line in enumerate(f, 1):
                line = line.strip()
                if not line:
                    continue
                try:
                    row = json.loads(line)
                except json.JSONDecodeError:
                    raise RangeError(f"{p}:{n}: not JSON") from None
                if not isinstance(row, dict) or row.get("phase") != "range":
                    continue
                if row.get("v") != 1:
                    raise RangeError(f"{p}:{n}: unsupported v={row.get('v')}")
                rows.append(row)
    rows.sort(key=lambda r: (r.get("tick", 0), r.get("qpc_us", 0), r.get("serial", 0)))
    return rows


def analyze(rows: list, r: Range | None = None) -> dict:
    ready = next((x for x in rows if x.get("result") == "ready"), None)
    fails = [x for x in rows if x.get("result") == "fail"]
    finished = next((x for x in rows if x.get("result") == "finished"), None)
    failures = []
    if ready is None:
        failures.append("range never reached ready")
    pre_ready_fail = [x for x in fails if ready is None or x.get("serial", 0) < ready.get("serial", 0)]
    for x in pre_ready_fail:
        failures.append(f"step {x.get('step')} failed: {x.get('reason')}")
    if any(x.get("step") == "restore" and x.get("result") == "fail" for x in rows):
        failures.append("loadout restore failed")
    if r is not None and ready is not None:
        asset = (ready.get("weapon") or {}).get("asset")
        if asset != r.selected:
            failures.append(f"ready weapon {asset} is not the range selection {r.selected}")
    gave = [x["weapon"]["asset"] for x in rows
            if x.get("step") == "give_weapon" and x.get("result") == "ok" and isinstance(x.get("weapon"), dict)]
    return {
        "schema": ANALYSIS_SCHEMA,
        "pass": not failures,
        "failures": failures,
        "ready_tick": ready.get("tick") if ready else None,
        "ready_qpc_us": ready.get("qpc_us") if ready else None,
        "given": gave,
        "finished_reason": finished.get("reason") if finished else None,
        "rows": len(rows),
        "headset_accepted": False,
    }


def main(argv: list | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check", help="validate range files")
    c.add_argument("ranges", nargs="+")
    c.add_argument("--catalog", type=Path, help="weapon catalog (bc2_weapon_config_pipeline.py output)")
    c = sub.add_parser("canonical", help="print the canonical range text")
    c.add_argument("range")
    c = sub.add_parser("settings", help="print the engine settings pairs (Class.Field value)")
    c.add_argument("range")
    c.add_argument("--json", action="store_true")
    c = sub.add_parser("prepare", help="write a fresh run directory with range.ini, BC2TestRange.cfg and range-run.json")
    c.add_argument("--range")
    c.add_argument("--tape", type=Path)
    c.add_argument("--out", type=Path, required=True)
    c.add_argument("--catalog", type=Path, help="weapon catalog (bc2_weapon_config_pipeline.py output)")
    c = sub.add_parser("stage", help="rename the intro movies aside (journaled, reversible)")
    c.add_argument("--game", type=Path, required=True)
    c.add_argument("--run", type=Path, required=True)
    c = sub.add_parser("unstage", help="restore everything `stage` changed")
    c.add_argument("--run", type=Path, required=True)
    c = sub.add_parser("analyze", help="judge phase=range rows of gun event logs")
    c.add_argument("logs", nargs="+")
    c.add_argument("--range")
    a = ap.parse_args(argv)
    try:
        if a.cmd == "check":
            bad = 0
            catalog = load_catalog(a.catalog) if a.catalog else None
            for name in a.ranges:
                try:
                    r, path = load(name)
                    check_catalog(r, catalog)
                    print(f"OK {path} ({r.name or '-'}: {r.level}, {','.join(r.weapons)}, route={r.route})")
                except RangeError as e:
                    bad += 1
                    print(f"FAIL {e}", file=sys.stderr)
            return 1 if bad else 0
        if a.cmd == "canonical":
            sys.stdout.write(format_range(load(a.range)[0]))
            return 0
        if a.cmd == "settings":
            pairs = range_settings(load(a.range)[0])
            if a.json:
                print(json.dumps([[k, v] for k, v in pairs]))
            else:
                sys.stdout.write("".join(f"{k} {v}\n" for k, v in pairs))
            return 0
        if a.cmd == "prepare":
            doc = prepare(a.range, a.tape, a.out, load_catalog(a.catalog) if a.catalog else None)
            print(json.dumps({"out": str(a.out), "range": doc["range"], "route": doc["route"]}))
            return 0
        if a.cmd == "stage":
            print(json.dumps(stage(a.game, a.run)))
            return 0
        if a.cmd == "unstage":
            print(json.dumps(unstage(a.run)))
            return 0
        if a.cmd == "analyze":
            r = load(a.range)[0] if a.range else None
            result = analyze(range_rows(a.logs), r)
            print(json.dumps(result, indent=2))
            return 0 if result["pass"] else 1
    except RangeError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    return 2


if __name__ == "__main__":
    sys.exit(main())
