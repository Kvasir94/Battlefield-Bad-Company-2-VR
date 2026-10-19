"""Offline tests for tools/bc2_test_range.py: no game, temp directories only."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("bc2_test_range", ROOT / "tools" / "bc2_test_range.py")
mod = importlib.util.module_from_spec(SPEC)
sys.modules["bc2_test_range"] = mod
SPEC.loader.exec_module(mod)
FIXTURES = ROOT / "tests" / "fixtures" / "test-range"
NAMES = ("sp02-xm8", "xm8-spas", "aek971-fixed-dt")
BASE = "level=Levels/sp_02\nweapons=XM8_sp_s\n"


def parse(body: str):
    return mod.parse("FVR_BC2_RANGE 1\n" + body)


class ParityTests(unittest.TestCase):
    def test_shipped_ranges_match_golden_files_shared_with_cpp(self):
        for name in NAMES:
            with self.subTest(name=name):
                r, _ = mod.load(name)
                self.assertEqual(mod.format_range(r), (FIXTURES / f"{name}.canonical").read_text(encoding="ascii"))
                pairs = "".join(f"{k} {v}\n" for k, v in mod.range_settings(r))
                self.assertEqual(pairs, (FIXTURES / f"{name}.settings").read_text(encoding="ascii"))
                self.assertEqual(mod.format_range(mod.parse(mod.format_range(r))), mod.format_range(r))

    def test_fallback_range_needs_no_native_operation(self):
        r, _ = mod.load("sp02-xm8")
        self.assertEqual((r.route, r.weapons, r.clear_inventory, r.selected), ("manual", ["XM8_sp_s"], False, "XM8_sp_s"))

    def test_layout_tolerance(self):
        r = mod.parse("; c\r\n\r\nFVR_BC2_RANGE 1\r\n level = Levels/SP_02 \r\nweapons=[XM8_sp_s, SPAS12_sp@Primary2]\r\nammo.SPAS12_sp=7/native")
        self.assertEqual((r.level, r.weapons, r.slots), ("Levels/SP_02", ["XM8_sp_s", "SPAS12_sp"], [-1, 1]))
        self.assertEqual((r.ammo_overrides[0].loaded, r.ammo_overrides[0].reserve), (7, -1))
        self.assertEqual(r.route, "menu")
        self.assertEqual(mod.range_settings(r), [("Client.Level", "Levels/SP_02")])

    def test_inverted_and_typed_switches(self):
        r = parse(BASE + "cutscenes=true\ncheckpoints=true\nkit=3\nteam=1\nfixed_dt=0.01\nscripted_input=true\n"
                  "instant_spawn=true\nunlimited_ammo=false\nfast_startup=true\nset.Client.DisableMenuFlow=true\n")
        d = dict(mod.range_settings(r))
        self.assertEqual(d["Server.DisableCutscenes"], "false")
        self.assertEqual(d["Server.DisableCheckpoints"], "false")
        self.assertEqual((d["Client.Kit"], d["Client.Team"], d["Game.ForceDeltaTime"]), ("3", "1", "0.01"))
        self.assertEqual(d["Client.IsFastStartupEnabled"], "true")
        self.assertEqual(mod.range_settings(r)[-1], ("Client.DisableMenuFlow", "true"))


class RejectionTests(unittest.TestCase):
    """Same cases as tests/TestRangeSpecTests.cpp."""

    CASES = [
        ("weapons=XM8_sp_s\n", "missing level"),
        ("level=Levels/sp_02\n", "missing weapons"),
        (BASE + "color=red\n", "unknown key color"),
        (BASE + "level=Levels/sp_03\n", "duplicate key level"),
        ("level=Levels/SP_99\nweapons=XM8_sp_s\n", ""),
        ("level=Levels/../sp_02\nweapons=XM8_sp_s\n", ""),
        ("level=Levels/mp_005\nweapons=XM8_sp_s\n", ""),
        ("level=Levels/sp_02\nweapons=XM8 sp\n", ""),
        ("level=Levels/sp_02\nweapons=XM8_sp_s,xm8_SP_S\n", "duplicate weapon"),
        ("level=Levels/sp_02\nweapons=XM8_sp_s@Primary9\n", "unknown weapon slot"),
        ("level=Levels/sp_02\nweapons=XM8_sp_s@Primary1,SPAS12_sp@Primary1\n", "duplicate weapon slot"),
        ("level=Levels/sp_02\nweapons=[XM8_sp_s\n", ""),
        ("level=Levels/sp_02\nweapons=a,b,c,d,e,f,g,h,i\n", ""),
        (BASE + "select=SPAS12_sp\n", "select is not in weapons"),
        (BASE + "ammo.SPAS12_sp=full\n", "not in weapons"),
        (BASE + "ammo.XM8_sp_s=native/native\n", ""),
        (BASE + "ammo.XM8_sp_s=100001/0\n", ""),
        (BASE + "teleport=1,2,3,400\n", ""),
        (BASE + "teleport=1e3,2,3\n", ""),
        (BASE + "fixed_dt=0.5\n", ""),
        (BASE + "god=yes\n", ""),
        (BASE + "kit=03\n", ""),
        (BASE + "timeout=10\nready_timeout=20\n", "ready_timeout exceeds timeout"),
        (BASE + "route=registry\n", ""),
        (BASE + "route=settings\n", ""),
        (BASE + "route=manual\nspawn=Start\n", "route=menu or override"),
        (BASE + "set.Client.Level=Levels/sp_03\n", "Client.Level is set twice"),
        (BASE + "god=true\nset.server.isgodmode=false\n", "set twice"),
        (BASE + "set.Client=1\n", "invalid setting key"),
        (BASE + "set.Client.Foo=-x\n", ""),
        (BASE + "set.Client.Foo=a;b\n", ""),
        (BASE + "set.Client.Foo=x\nset.client.foo=y\n", "duplicate key"),
        (BASE + "just text\n", "expected key=value"),
        (BASE + "notes=" + "a" * 600 + "\n", "line too long"),
    ]

    def test_cases(self):
        for body, wanted in self.CASES:
            with self.subTest(body=body[:60]):
                with self.assertRaises(mod.RangeError) as ctx:
                    parse(body)
                self.assertIn(wanted, str(ctx.exception))

    def test_header_bom_and_bytes(self):
        for raw, wanted in ((BASE.encode(), "header"), (b"\xef\xbb\xbfFVR_BC2_RANGE 1\n" + BASE.encode(), "BOM"),
                            (b"FVR_BC2_RANGE 1\n" + BASE.encode() + "notes=café\n".encode(), "non-ASCII")):
            with self.subTest(wanted=wanted), self.assertRaises(mod.RangeError) as ctx:
                mod.parse(raw)
            self.assertIn(wanted, str(ctx.exception))
        with self.assertRaises(mod.RangeError) as ctx:
            mod.parse("FVR_BC2_RANGE 1\n# c\nlevel=Levels/sp_02\nbad=1\n")
        self.assertTrue(str(ctx.exception).startswith("line 4: "))


TAPE = "FVR_BC2_INPUT 1 10000 1\n#! weapon=XM8_sp_s loaded<capacity range={range}{extra}\n#! expect transfers=1\n1 0 0 1 1 0\n"


class PrepareTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def tape(self, name="xm8-spas", extra=""):
        p = self.dir / "t.fvrtape"
        p.write_text(TAPE.format(range=name, extra=extra), encoding="ascii")
        return p

    def test_prepare_from_tape_header_with_overrides(self):
        out = self.dir / "run"
        doc = mod.prepare(None, self.tape(extra=" range.ai=true range.select=SPAS12_sp"), out)
        self.assertEqual(doc["range"], "xm8-spas")
        self.assertEqual(doc["select"], "SPAS12_sp")
        self.assertIn(["Server.IsAiEnabled", "true"], doc["settings"])
        self.assertFalse(doc["level_route_proven"])
        text = (out / "range.ini").read_text(encoding="ascii")
        self.assertEqual(mod.format_range(mod.parse(text)), text)
        self.assertEqual(json.loads((out / "range-run.json").read_text())["range_sha256"], doc["range_sha256"])
        cfg = [l for l in (out / "BC2TestRange.cfg").read_text(encoding="ascii").splitlines() if not l.startswith("#")]
        self.assertEqual(cfg, [f"{k} {v}" for k, v in doc["settings"]])  # PC launcher's "Key value" format
        self.assertEqual(cfg[0], "Client.Level Levels/sp_02")
        self.assertEqual(doc["launch"][doc["launch"].index("-InjectDelayMs") + 1], "3000")

    def test_tape_weapon_must_be_in_range(self):
        with self.assertRaises(mod.RangeError) as ctx:
            mod.prepare(None, self.tape(name="aek971-fixed-dt"), self.dir / "run")
        self.assertIn("weapon=XM8_sp_s", str(ctx.exception))

    def test_refusals(self):
        out = self.dir / "used"
        out.mkdir()
        (out / "x").write_text("x")
        with self.assertRaises(mod.RangeError):
            mod.prepare("sp02-xm8", None, out)
        with self.assertRaises(mod.RangeError):
            mod.prepare("sp02-xm8", self.tape(), self.dir / "r2")  # disagrees with tape range
        with self.assertRaises(mod.RangeError):
            mod.prepare(None, None, self.dir / "r3")
        with self.assertRaises(mod.RangeError):
            mod.prepare("no-such-range", None, self.dir / "r4")
        with self.assertRaises(mod.RangeError):
            mod.prepare(None, self.tape(extra=" range.weapons=AEK971_sp"), self.dir / "r5")  # tape weapon dropped

    def test_catalog(self):
        cat = self.dir / "weapon-configurations.json"
        cat.write_text(json.dumps({"schema": "fvr.bc2.authored_weapon_configuration",
                                   "resolved_weapons": [{"native_name": "XM8_sp_s"}, {"native_name": "AEK971_sp"}]}))
        names = mod.load_catalog(cat)
        mod.check_catalog(mod.load("sp02-xm8")[0], names)
        with self.assertRaises(mod.RangeError) as ctx:
            mod.check_catalog(mod.load("xm8-spas")[0], names)
        self.assertIn("SPAS12_sp", str(ctx.exception))
        (self.dir / "list.txt").write_text("# c\nSPAS12_sp\n")
        self.assertEqual(mod.load_catalog(self.dir / "list.txt"), {"SPAS12_sp"})

    def test_replay_runner_hook(self):
        scenario = {"name": "xm8-mag-reload", "weapon": "XM8_sp_s", "range": "xm8-spas"}
        doc = mod.range_for_scenario(scenario, self.tape(), self.dir / "a1")
        self.assertEqual(doc["range"], "xm8-spas")
        self.assertIsNone(mod.range_for_scenario({"weapon": "XM8_sp_s"}, None, self.dir / "a2"))
        with self.assertRaises(mod.RangeError):
            mod.range_for_scenario({"range": "sp02-xm8"}, self.tape(), self.dir / "a3")
        with self.assertRaises(mod.RangeError):
            mod.range_for_scenario({"range": "aek971-fixed-dt", "weapon": "XM8_sp_s"}, None, self.dir / "a4")


class StageTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.game = Path(self.tmp.name) / "game"
        (self.game / "movies").mkdir(parents=True)
        (self.game / "BFBC2Game.exe").write_bytes(b"MZ")
        (self.game / "movies" / "ea_logo_hd.res").write_bytes(b"ea")
        (self.game / "movies" / "dolbydigital.res").write_bytes(b"dolby")
        self.run = Path(self.tmp.name) / "run"

    def tearDown(self):
        self.tmp.cleanup()

    def test_rename_and_restore_movies(self):
        j = mod.stage(self.game, self.run)
        self.assertEqual(len(j["actions"]), 2)
        self.assertFalse((self.game / "movies" / "ea_logo_hd.res").exists())
        self.assertTrue((self.game / "movies" / "ea_logo_hd.res.fvr-disabled").exists())
        with self.assertRaises(mod.RangeError):
            mod.stage(self.game, self.run)  # journal exists
        mod.unstage(self.run)
        self.assertEqual((self.game / "movies" / "dolbydigital.res").read_bytes(), b"dolby")
        self.assertFalse(list((self.game / "movies").glob("*.fvr-disabled")))
        with self.assertRaises(mod.RangeError):
            mod.unstage(self.run)  # journal retired

    def test_refuses_leftovers_and_missing_game(self):
        (self.game / "movies" / "ea_logo_hd.res.fvr-disabled").write_bytes(b"old")
        with self.assertRaises(mod.RangeError):
            mod.stage(self.game, self.run)
        with self.assertRaises(mod.RangeError):
            mod.stage(self.game / "nope", self.run / "x")

    def test_unstage_reports_conflicts(self):
        mod.stage(self.game, self.run)
        (self.game / "movies" / "ea_logo_hd.res").write_bytes(b"new")  # reappeared
        with self.assertRaises(mod.RangeError):
            mod.unstage(self.run)
        self.assertEqual((self.game / "movies" / "dolbydigital.res").read_bytes(), b"dolby")


def row(serial, step, result, reason="none", asset=None, tick=None):
    r = {"v": 1, "tick": tick if tick is not None else serial, "qpc_us": serial * 10, "src": "fvr", "phase": "range",
         "serial": serial, "step": step, "result": result, "reason": reason}
    if asset:
        r["weapon"] = {"asset": asset}
    return r


class AnalyzeTests(unittest.TestCase):
    def test_pass_and_failures(self):
        good = [row(1, "await_level", "ok"), row(2, "give_weapon", "dispatch", asset="SPAS12_sp"),
                row(3, "give_weapon", "ok", asset="SPAS12_sp"), row(4, "ready", "ready", asset="XM8_sp_s"),
                row(5, "ready", "finished", "owner_lost")]
        r = mod.load("xm8-spas")[0]
        out = mod.analyze(good, r)
        self.assertTrue(out["pass"])
        self.assertEqual((out["given"], out["finished_reason"], out["headset_accepted"]), (["SPAS12_sp"], "owner_lost", False))
        bad = mod.analyze([row(1, "give_weapon", "fail", "unavailable", asset="SPAS12_sp")], r)
        self.assertFalse(bad["pass"])
        self.assertIn("step give_weapon failed: unavailable", bad["failures"])
        wrong = mod.analyze([row(1, "ready", "ready", asset="SPAS12_sp")], r)
        self.assertFalse(wrong["pass"])
        restore = mod.analyze(good + [row(6, "restore", "fail", "restore_failed")], r)
        self.assertIn("loadout restore failed", restore["failures"])

    def test_rows_from_mixed_log(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "gun-events.jsonl"
            lines = [json.dumps({"v": 1, "tick": 1, "qpc_us": 1, "src": "native", "phase": "transfer"}),
                     json.dumps(row(2, "ready", "ready", asset="XM8_sp_s")), ""]
            log.write_text("\n".join(lines), encoding="utf-8")
            rows = mod.range_rows([log])
            self.assertEqual(len(rows), 1)
            self.assertTrue(mod.analyze(rows)["pass"])
            log.write_text(json.dumps({**row(1, "ready", "ready"), "v": 2}), encoding="utf-8")
            with self.assertRaises(mod.RangeError):
                mod.range_rows([log])


class CliTests(unittest.TestCase):
    def test_check_and_settings(self):
        self.assertEqual(mod.main(["check", *(str(ROOT / "tools" / "test-range" / "ranges" / f"{n}.ini") for n in NAMES)]), 0)
        self.assertEqual(mod.main(["check", "no-such-range"]), 1)
        self.assertEqual(mod.main(["settings", "sp02-xm8", "--json"]), 0)


if __name__ == "__main__":
    unittest.main()
