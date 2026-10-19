import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "replay"))
import replay_assert  # noqa: E402

SCENARIOS = ROOT / "tools" / "replay" / "scenarios"


def row(tick, src, phase, copy=None, **fields):
    out = {"v": 1, "tick": tick, "qpc_us": 1_000_000 + tick * 10_000, "src": src,
           "weapon": {"asset": "XM8_sp_s", "id": "0xa0000"}, "copy": copy, "phase": phase}
    out.update(fields)
    return out


def magazine_reload(seat_tick=200, transfer_tick=230, extra=()):
    rows = [
        row(1, "native", "sample", "server", loaded=27, reserve=83, cur=2),
        row(10, "native", "firing_state", "server", **{"from": 2, "to": 10}),
        row(10, "fvr", "begin", hand="left", kind="magazine"),
        row(25, "native", "firing_state", "server", **{"from": 10, "to": 11}),
        row(25, "native", "freeze_start", "server"),
        row(80, "fvr", "unseat"),
        row(120, "fvr", "pouch_start"),
        row(150, "fvr", "capture"),
        row(seat_tick, "fvr", "seat"),
        row(seat_tick, "native", "freeze_stop", "server", reason="release"),
        row(seat_tick, "native", "firing_state", "server", **{"from": 11, "to": 12}),
        row(transfer_tick, "native", "transfer", "server", loaded_before=27, reserve_before=83, loaded_after=30, reserve_after=80),
        row(transfer_tick, "fvr", "credit", loaded=30, reserve=80),
        row(transfer_tick, "native", "firing_state", "server", **{"from": 12, "to": 1}),
        row(transfer_tick, "native", "firing_state", "server", **{"from": 1, "to": 2}),
        row(transfer_tick, "fvr", "complete"),
        row(transfer_tick, "fvr", "end", reason="complete"),
        row(transfer_tick + 30, "native", "sample", "server", loaded=30, reserve=80, cur=2),
    ]
    rows += list(extra)
    return sorted(rows, key=lambda r: r["tick"])


def scenario(name="xm8-mag-reload"):
    return json.loads((SCENARIOS / f"{name}.json").read_text(encoding="utf-8"))


def run(rows, scen=None, extra=()):
    for index, r in enumerate(rows):
        r["_order"] = index
    rows = sorted(rows, key=lambda r: (r["qpc_us"], r["tick"], r["_order"]))
    return replay_assert.evaluate(rows, [], scen or scenario(), extra, "sim")


def failed(report):
    return {r["id"] for r in report["rules"] if not r["passed"]}


class ReplayAssertTests(unittest.TestCase):
    def test_clean_magazine_reload_passes(self):
        report = run(magazine_reload())
        self.assertTrue(report["passed"], failed(report))
        self.assertFalse(report["headset_accepted"])
        self.assertEqual(report["schema"], "fvr.bc2.replay_assert.v1")

    def test_missing_capture_breaks_order(self):
        rows = [r for r in magazine_reload() if r["phase"] != "capture"]
        report = run(rows)
        self.assertEqual(failed(report), {"fvr_order"})
        detail = next(r for r in report["rules"] if r["id"] == "fvr_order")["detail"]
        self.assertIn("capture", detail)

    def test_native_reload_without_seat_is_the_headset_1005_failure(self):
        rows = [r for r in magazine_reload() if r["phase"] not in ("seat", "capture")]
        report = run(rows)
        self.assertIn("no_native_reload_without_seat", failed(report))
        self.assertIn("no_transfer_state_without_seat", failed(report))

    def test_double_credit_fails_count_and_ammo(self):
        extra = [row(260, "native", "transfer", "server", loaded_before=30, reserve_before=80, loaded_after=30, reserve_after=80),
                 row(260, "fvr", "credit", loaded=30, reserve=80)]
        report = run(magazine_reload(extra=extra))
        self.assertTrue({"one_transfer", "one_credit"} <= failed(report))

    def test_wrong_fill_and_conservation(self):
        rows = magazine_reload()
        for r in rows:
            if r["phase"] == "transfer":
                r["loaded_after"], r["reserve_after"] = 30, 81  # one round invented
        rows[-1]["reserve"] = 81
        report = run(rows)
        self.assertIn("ammo_fill", failed(report))
        detail = next(r for r in report["rules"] if r["id"] == "ammo_fill")["detail"]
        self.assertIn("loaded+reserve", detail)

    def test_slow_transfer_violates_window(self):
        report = run(magazine_reload(seat_tick=200, transfer_tick=400))
        self.assertTrue({"seat_to_transfer", "seat_to_credit"} <= failed(report))

    def test_fire_with_magazine_out(self):
        extra = [row(100, "native", "fire", "server", result="shot", loaded=26, reserve=83)]
        report = run(magazine_reload(extra=extra))
        self.assertIn("no_fire_mag_out", failed(report))
        extra = [row(300, "native", "fire", "server", result="shot", loaded=29, reserve=80)]
        self.assertNotIn("no_fire_mag_out", failed(run(magazine_reload(extra=extra))))

    def test_auto_reload_and_drops(self):
        extra = [row(5, "native", "auto_reload", "server", suppressed=False), row(6, "native", "dropped", n=3)]
        self.assertTrue({"no_auto_reload", "no_drops"} <= failed(run(magazine_reload(extra=extra))))
        extra = [row(5, "native", "auto_reload", "server", suppressed=True)]
        self.assertTrue(run(magazine_reload(extra=extra))["passed"])

    def test_tape_expect_tokens(self):
        rules = replay_assert.expect_rules(["transfers=1", "credits=1", "loaded=+fill", "reserve=-fill",
                                            "no-auto-reload", "no-fire-mag-out", "seat_to_transfer_ms<=1500"])
        self.assertEqual(len(rules), 6)  # loaded/reserve fold into one ammo_delta
        self.assertTrue(run(magazine_reload(), extra=rules)["passed"])
        tight = replay_assert.expect_rules(["seat_to_transfer_ms<=100"])
        self.assertIn("expect.seat_to_transfer", failed(run(magazine_reload(), extra=tight)))
        with self.assertRaises(replay_assert.RuleError):
            replay_assert.expect_rules(["feels-good"])

    def test_spas_single_round_rules(self):
        spas = scenario("spas-shell-reload")
        rows = [row(1, "native", "sample", "server", loaded=2, reserve=20, cur=2),
                row(5, "native", "firing_state", "server", **{"from": 2, "to": 10}),
                row(5, "fvr", "begin", kind="shell"),
                row(8, "native", "firing_state", "server", **{"from": 10, "to": 11})]
        tick, loaded, reserve = 20, 2, 20
        for _ in range(3):
            rows += [row(tick, "fvr", "pouch_start"), row(tick + 5, "fvr", "capture"), row(tick + 10, "fvr", "seat"),
                     row(tick + 10, "native", "firing_state", "server", **{"from": 11, "to": 12}),
                     row(tick + 20, "native", "transfer", "server", loaded_before=loaded, reserve_before=reserve,
                         loaded_after=loaded + 1, reserve_after=reserve - 1),
                     row(tick + 20, "fvr", "credit", loaded=loaded + 1, reserve=reserve - 1),
                     row(tick + 21, "native", "firing_state", "server", **{"from": 12, "to": 11})]
            tick, loaded, reserve = tick + 40, loaded + 1, reserve - 1
        rows += [row(tick, "native", "firing_state", "server", **{"from": 11, "to": 1}),
                 row(tick, "native", "firing_state", "server", **{"from": 1, "to": 2}), row(tick, "fvr", "end", reason="complete")]
        self.assertTrue(run(rows, spas)["passed"], failed(run(rows, spas)))
        takeover = rows + [row(tick + 1, "fvr", "cancel", reason="claim_lost"),
                           row(tick + 5, "native", "transfer", "server", loaded_before=loaded, reserve_before=reserve,
                               loaded_after=loaded + 1, reserve_after=reserve - 1)]
        self.assertIn("no_takeover_after_cancel", failed(run(takeover, spas)))

    def test_cli_exit_codes_and_invalid_rows(self):
        with tempfile.TemporaryDirectory() as tmp:
            events = Path(tmp) / "gun-events.jsonl"
            events.write_text("\n".join(json.dumps(r) for r in magazine_reload()) + "\n", encoding="utf-8")
            output = Path(tmp) / "assert.json"
            args = [str(events), "--scenario", str(SCENARIOS / "xm8-mag-reload.json"), "--output", str(output)]
            quiet = contextlib.redirect_stdout(io.StringIO())
            with quiet, contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(replay_assert.main(args), 0)
            self.assertTrue(json.loads(output.read_text())["passed"])
            with events.open("a", encoding="utf-8") as handle:
                handle.write('{"v":2,"tick":1}\nnot json\n')
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(replay_assert.main(args), 1)
            self.assertEqual(json.loads(output.read_text())["rules"][0]["id"], "input")
            bad = Path(tmp) / "bad.json"
            bad.write_text('{"rules":[{"type":"vibes"}]}', encoding="utf-8")
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(replay_assert.main([str(events), "--scenario", str(bad)]), 2)

    def test_branch_alias_and_phase_alias(self):
        rows = magazine_reload()
        for r in rows:
            if r.get("copy") == "server":
                r["copy"] = None
                r["branch"] = 2
            if r["phase"] == "firing_state":
                r["phase"] = "state"
        with tempfile.TemporaryDirectory() as tmp:
            events = Path(tmp) / "e.jsonl"
            events.write_text("\n".join(json.dumps(r) for r in rows), encoding="utf-8")
            loaded, invalid = replay_assert.load_events([events])
        self.assertFalse(invalid)
        self.assertTrue(replay_assert.evaluate(loaded, invalid, scenario())["passed"])


if __name__ == "__main__":
    unittest.main()
