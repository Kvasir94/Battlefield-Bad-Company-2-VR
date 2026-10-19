import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import analyze_gun_events as age

XM8 = {"asset": "XM8_sp_s", "id": "0x1a2b"}
SPAS = {"asset": "SPAS12_sp", "id": "0x3c4d"}
TICK_US = 11_111  # 90 Hz


class Log:
    """Builds a synthetic JSONL log; `at(ms)` positions the next events in time."""

    def __init__(self, weapon=XM8, t0_us=1_000_000, tick0=100):
        self.w, self.t0, self.tick0, self.rows = weapon, t0_us, tick0, []
        self.ms = 0.0

    def at(self, ms):
        self.ms = ms
        return self

    def emit(self, src, phase, copy=None, **kw):
        t = self.t0 + int(self.ms * 1000)
        row = {"v": 1, "tick": self.tick0 + int(self.ms * 1000 // TICK_US), "qpc_us": t,
               "src": src, "weapon": self.w, "copy": copy, "phase": phase}
        row.update(kw)
        self.rows.append(row)
        return self

    def native(self, phase, copy=None, **kw):
        return self.emit("native", phase, copy, **kw)

    def fvr(self, phase, **kw):
        return self.emit("fvr", phase, None, **kw)

    def state(self, frm, to, copies=age.COPIES):
        for c in copies:
            self.native("firing_state", c, **{"from": frm, "to": to})
        return self

    def transfer(self, lb, rb, la, ra, copies=age.COPIES):
        for c in copies:
            self.native("transfer", c, loaded_before=lb, reserve_before=rb, loaded_after=la, reserve_after=ra)
        return self

    def fire(self, loaded, reserve, copy="server", result="shot", **kw):
        return self.native("fire", copy, result=result, loaded=loaded, reserve=reserve, **kw)

    def lines(self):
        return [json.dumps(r) for r in self.rows]

    def write(self, directory, name="log.jsonl"):
        p = Path(directory) / name
        p.write_text("\n".join(self.lines()) + "\n", encoding="utf-8")
        return p


def clean_xm8_reload(credit_delay_ms=80, start_ms=0):
    """XM8 magazine reload: loaded 3/reserve 60 -> 30/33, physical seat, hold released at credit."""
    L = Log()
    L.at(start_ms).fvr("begin", hand="left", kind="magazine")
    L.native("firing_state", "server", **{"from": 2, "to": 10})
    for c in ("client0", "client1"):
        L.native("firing_state", c, **{"from": 2, "to": 10})
    L.at(start_ms + 20).state(10, 11)
    for c in age.COPIES:
        L.native("freeze_start", c, reason="hold")
    L.at(start_ms + 300).fvr("unseat")
    L.at(start_ms + 700).fvr("pouch_start")
    L.at(start_ms + 900).fvr("capture")
    L.at(start_ms + 1000).fvr("seat", credits=1)
    L.at(start_ms + 1000 + credit_delay_ms)
    for c in age.COPIES:
        L.native("freeze_stop", c, reason="release")
    L.state(11, 12).transfer(3, 60, 30, 33)
    L.fvr("credit", loaded=30, reserve=33)
    L.state(12, 1).at(start_ms + 1200 + credit_delay_ms).state(1, 2)
    L.fvr("complete")
    L.fvr("end", reason="complete")
    return L


def run(log, **th):
    return age.analyze_lines(log.lines(), age.thresholds_from_mapping(th))


def check(rep, name):
    return next(c for c in rep["checks"] if c["name"] == name)


class CleanReloadTests(unittest.TestCase):
    def test_clean_reload_passes(self):
        rep = run(clean_xm8_reload())
        self.assertEqual(rep["result"], "PASS", [c for c in rep["checks"] if c["status"] == "FAIL"])
        self.assertEqual(len(rep["reloads"]), 1)
        r = rep["reloads"][0]
        self.assertEqual(r["outcome"], "completed")
        self.assertEqual(r["origin"], "both")
        self.assertEqual(r["closed_by"], "end")
        self.assertAlmostEqual(r["seats"][0]["delay_ms"], 80.0, places=1)
        self.assertEqual(r["seats"][0]["source"], "credit")
        self.assertTrue(r["conservation"]["ok"])
        self.assertEqual(r["double_credits"], 0)
        self.assertEqual(len(r["freeze_ms"]), 3)
        self.assertIn("s10>s11>freeze_start>unseat", r["sequence"])

    def test_text_and_json_output(self):
        rep = run(clean_xm8_reload())
        text = age.render_text(rep, verbose=True)
        self.assertIn("RELOAD #1 XM8_sp_s#0x1a2b", text)
        self.assertIn("RESULT: PASS", text)
        self.assertIn("seq:", text)
        json.dumps(rep)  # serializable

    def test_native_only_reload_is_completed(self):
        L = Log(SPAS)
        L.native("auto_reload", "server", suppressed=False, loaded=0, reserve=8)
        L.state(2, 10).at(100).state(10, 11).at(200).state(11, 12)
        L.transfer(0, 8, 4, 4).at(300).state(12, 1).at(400).state(1, 2)
        rep = run(L)
        self.assertEqual(rep["reloads"][0]["outcome"], "completed")
        self.assertEqual(rep["reloads"][0]["origin"], "native")
        self.assertEqual(rep["reloads"][0]["seats"], [])

    def test_single_round_loop_keeps_one_reload(self):
        L = Log(SPAS)
        L.fvr("begin", hand="left", kind="shell")
        L.state(2, 10).state(10, 11)
        for i in range(3):
            L.at(500 * (i + 1)).fvr("seat")
            L.state(11, 12).transfer(1 + i, 8 - i, 2 + i, 7 - i)
            L.fvr("credit", loaded=2 + i, reserve=7 - i)
            L.state(12, 11)
        L.at(2000).state(11, 1).state(1, 2).fvr("complete").fvr("end", reason="complete")
        rep = run(L)
        self.assertEqual(len(rep["reloads"]), 1)
        r = rep["reloads"][0]
        self.assertEqual(len(r["seats"]), 3)
        self.assertEqual(r["double_credits"], 0)
        self.assertEqual(rep["result"], "PASS", [c for c in rep["checks"] if c["status"] == "FAIL"])


class SeatCreditDelayTests(unittest.TestCase):
    def test_late_credit_2800ms_fails(self):
        rep = run(clean_xm8_reload(credit_delay_ms=2800))
        c = check(rep, "seat_credit_ms")
        self.assertEqual(c["status"], "FAIL")
        self.assertAlmostEqual(c["value"], 2800.0, places=1)
        self.assertEqual(rep["result"], "FAIL")
        self.assertGreater(rep["reloads"][0]["seats"][0]["delay_ticks"], 200)
        self.assertIn("RESULT: FAIL", age.render_text(rep))

    def test_late_credit_passes_with_relaxed_threshold(self):
        rep = run(clean_xm8_reload(credit_delay_ms=2800), max_seat_credit_ms=3000)
        self.assertEqual(check(rep, "seat_credit_ms")["status"], "PASS")
        self.assertEqual(rep["result"], "PASS")

    def test_tick_threshold(self):
        rep = run(clean_xm8_reload(credit_delay_ms=200), max_seat_credit_ticks=5)
        self.assertEqual(check(rep, "seat_credit_ticks")["status"], "FAIL")
        rep = run(clean_xm8_reload(credit_delay_ms=20), max_seat_credit_ticks=5)
        self.assertEqual(check(rep, "seat_credit_ticks")["status"], "PASS")

    def test_native_transfer_used_when_no_fvr_credit(self):
        L = Log()
        L.fvr("begin").state(2, 10).state(10, 11).at(500).fvr("seat")
        L.at(2900).state(11, 12).transfer(3, 60, 30, 33).state(12, 1).state(1, 2).fvr("end", reason="complete")
        rep = run(L)
        s = rep["reloads"][0]["seats"][0]
        self.assertEqual(s["source"], "transfer")
        self.assertAlmostEqual(s["delay_ms"], 2400.0, places=1)
        self.assertEqual(check(rep, "seat_credit_ms")["status"], "FAIL")

    def test_transfer_delay_threshold(self):
        # credit arrives promptly but the native transfer lags by 1 s
        L = Log()
        L.fvr("begin").state(2, 10).state(10, 11).at(500).fvr("seat").at(520).fvr("credit")
        L.at(1500).state(11, 12).transfer(3, 60, 30, 33).state(12, 1).state(1, 2).fvr("end", reason="complete")
        rep = run(L, max_seat_transfer_ms=300)
        self.assertEqual(check(rep, "seat_credit_ms")["status"], "PASS")
        self.assertEqual(check(rep, "seat_transfer_ms")["status"], "FAIL")

    def test_seat_without_credit_fails(self):
        L = Log()
        L.fvr("begin").at(500).fvr("seat").at(900).fvr("end", reason="complete")
        rep = run(L)
        self.assertEqual(check(rep, "uncredited_seats")["value"], 1)
        self.assertEqual(rep["result"], "FAIL")

    def test_seat_without_credit_ok_when_cancelled(self):
        L = Log()
        L.fvr("begin").at(500).fvr("seat").at(600).fvr("cancel", reason="claim_lost").fvr("end", reason="cancel")
        rep = run(L)
        self.assertEqual(check(rep, "uncredited_seats")["value"], 0)
        self.assertEqual(rep["reloads"][0]["outcome"], "cancelled")


class ConservationTests(unittest.TestCase):
    def test_lost_round_in_transfer_fails(self):
        L = clean_xm8_reload()
        for r in L.rows:
            if r["phase"] == "transfer":
                r["reserve_after"] = 32  # reserve lost one round
        rep = run(L)
        self.assertEqual(check(rep, "conservation_violations")["status"], "FAIL")
        self.assertEqual(rep["reloads"][0]["conservation"]["violations"][0]["type"], "not_conserved")

    def test_unexplained_drift_between_events_fails(self):
        L = Log()
        L.fvr("begin").state(2, 10)
        L.native("step", "server", state=10, loaded=3, reserve=60)
        L.at(100).native("step", "server", state=11, loaded=3, reserve=57)  # 3 rounds vanished
        L.at(200).state(11, 1).state(1, 2).fvr("end", reason="complete")
        rep = run(L)
        v = rep["reloads"][0]["conservation"]["violations"]
        self.assertEqual(v[0]["type"], "drift")
        self.assertEqual((v[0]["expected"], v[0]["got"]), (63, 60))

    def test_shots_during_reload_are_accounted(self):
        L = Log()
        L.fvr("begin").state(2, 10)
        L.native("step", "server", state=10, loaded=3, reserve=60)
        L.at(50).fire(2, 60)  # a native shot while reloading is still legal: consumes one
        L.at(100).fire(1, 60)
        L.at(200).state(10, 1).state(1, 2).fvr("end", reason="complete")
        rep = run(L)
        c = rep["reloads"][0]["conservation"]
        self.assertTrue(c["ok"], c)
        self.assertEqual(c["copies"]["server"], {"start": 63, "end": 61, "consumed": 2})

    def test_shot_that_does_not_consume_fails(self):
        L = Log()
        L.fvr("begin").state(2, 10)
        L.native("step", "server", state=10, loaded=3, reserve=60)
        L.at(50).fire(3, 60)  # result=shot but nothing consumed (consumed defaults to 1)
        L.at(200).state(10, 1).state(1, 2).fvr("end", reason="complete")
        rep = run(L)
        self.assertEqual(rep["reloads"][0]["conservation"]["violations"][0]["type"], "fire_mismatch")

    def test_restore_rollback_is_conserved(self):
        L = clean_xm8_reload()
        # a client prediction rollback after the transfer, sum-preserving
        L.rows.insert(-3, dict(L.rows[-1], phase="restore", src="native", copy="client0",
                               loaded_before=30, reserve_before=33, loaded_after=3, reserve_after=60))
        rep = run(L)
        self.assertEqual(check(rep, "conservation_violations")["value"], 0)

    def test_restore_that_loses_rounds_fails(self):
        L = clean_xm8_reload()
        L.rows.insert(-3, dict(L.rows[-1], phase="restore", src="native", copy="client0",
                               loaded_before=30, reserve_before=33, loaded_after=3, reserve_after=50))
        rep = run(L)
        self.assertEqual(check(rep, "conservation_violations")["status"], "FAIL")


class DoubleCreditTests(unittest.TestCase):
    def test_two_credits_for_one_seat(self):
        L = clean_xm8_reload()
        L.at(1300).fvr("credit", loaded=30, reserve=33)
        # credit landed before 'end'? insert order is by qpc; end is at 1280 so this is late-after-close
        rep = run(L)
        self.assertEqual(check(rep, "double_credits")["value"], 1)
        self.assertEqual(rep["result"], "FAIL")
        self.assertIn("credit", rep["reloads"][0]["late_events"])

    def test_extra_native_transfer_after_cancel_is_takeover(self):
        # SPAS cancel, then native keeps loading shells by itself (the 2026-10-01 takeover)
        L = Log(SPAS)
        L.fvr("begin", kind="shell").state(2, 10).state(10, 11)
        L.at(400).fvr("cancel", reason="claim_lost")
        for i in range(3):
            L.at(500 + 300 * i).state(11, 12).transfer(1 + i, 8 - i, 2 + i, 7 - i).state(12, 11)
        L.at(1800).state(11, 1).state(1, 2).fvr("end", reason="cancel")
        rep = run(L)
        r = rep["reloads"][0]
        self.assertEqual(r["double_credits"], 3)
        self.assertEqual(check(rep, "double_credits")["status"], "FAIL")

    def test_one_transfer_per_copy_per_seat_is_not_double(self):
        rep = run(clean_xm8_reload())
        self.assertEqual(rep["reloads"][0]["double_credits"], 0)

    def test_native_double_transfer_on_one_copy(self):
        L = clean_xm8_reload()
        L.rows.insert(-4, dict(L.rows[-1], phase="transfer", src="native", copy="server",
                               loaded_before=30, reserve_before=33, loaded_after=33, reserve_after=30))
        rep = run(L)
        self.assertGreaterEqual(rep["reloads"][0]["double_credits"], 1)


class AutoReloadTests(unittest.TestCase):
    def test_unsuppressed_auto_reload_fails(self):
        L = Log().native("auto_reload", "server", suppressed=False, loaded=0, reserve=60)
        L.state(2, 10).at(100).state(10, 11).at(200).state(11, 12).transfer(0, 60, 30, 30).state(12, 1).state(1, 2)
        rep = run(L)
        self.assertEqual(check(rep, "auto_reloads_unsuppressed")["value"], 1)
        self.assertEqual(rep["result"], "FAIL")

    def test_suppressed_auto_reload_passes_and_opens_no_reload(self):
        L = Log().native("auto_reload", "server", suppressed=True, loaded=0, reserve=60)
        L.at(50).native("step", "server", state=2, loaded=0, reserve=60)
        rep = run(L)
        self.assertEqual(rep["result"], "PASS")
        self.assertEqual(rep["summary"]["reloads"], 0)
        c = check(rep, "auto_reloads")
        self.assertEqual((c["value"], c["status"]), (1, "INFO"))

    def test_total_auto_reload_limit(self):
        L = Log().native("auto_reload", "server", suppressed=True)
        rep = run(L, max_auto_reloads=0)
        self.assertEqual(check(rep, "auto_reloads")["status"], "FAIL")


class FireTests(unittest.TestCase):
    def test_shot_with_magazine_out_is_a_leak(self):
        L = Log()
        L.fvr("begin").fvr("unseat").at(100).fire(2, 60, result="shot", consumed=1)
        L.at(300).fvr("end", reason="cancel")
        rep = run(L)
        self.assertEqual(check(rep, "fire_leaks")["value"], 1)
        self.assertEqual(rep["result"], "FAIL")
        self.assertEqual(rep["fire_attempts"][0]["reasons"], ["mag_out"])

    def test_dry_and_masked_attempts_with_magazine_out_are_counted_not_failed(self):
        L = Log()
        L.fvr("begin").fvr("unseat")
        L.at(100).fire(0, 60, result="dry").at(200).fire(0, 60, result="masked")
        L.at(300).fvr("end", reason="complete")
        rep = run(L, max_fire_attempts_unloaded=1)
        self.assertEqual(check(rep, "fire_leaks")["value"], 0)
        self.assertEqual(check(rep, "fire_attempts_unloaded")["value"], 2)
        self.assertEqual(check(rep, "fire_attempts_unloaded")["status"], "FAIL")

    def test_attempt_after_reseat_is_fine(self):
        L = Log()
        L.fvr("begin").fvr("unseat").at(100).fvr("seat").at(150).fvr("credit").at(200).fire(29, 33)
        L.at(300).fvr("end", reason="complete")
        rep = run(L)
        self.assertEqual(rep["fire_attempts"], [])

    def test_shot_without_consumption_on_empty_is_a_leak(self):
        L = Log().fire(0, 60, result="shot", consumed=0)
        rep = run(L)
        self.assertEqual(check(rep, "fire_leaks")["value"], 1)
        self.assertEqual(rep["fire_attempts"][0]["reasons"], ["empty"])

    def test_dry_fire_on_empty_is_counted_only(self):
        rep = run(Log().fire(0, 0, result="dry"))
        self.assertEqual(check(rep, "fire_attempts_unloaded")["value"], 1)
        self.assertEqual(rep["result"], "PASS")

    def test_copies_of_one_shot_are_one_attempt(self):
        L = Log().fvr("unseat")
        for c in age.COPIES:
            L.at(100).fire(2, 60, copy=c)
        rep = run(L)
        self.assertEqual(check(rep, "fire_leaks")["value"], 1)

    def test_normal_fire_has_no_attempts(self):
        L = Log().fire(29, 33).at(100).fire(28, 33)
        rep = run(L)
        self.assertEqual(rep["fire_attempts"], [])
        self.assertEqual(rep["summary"]["fire_events"], 2)


class CopyDisagreementTests(unittest.TestCase):
    def test_transient_skew_passes(self):
        L = Log()
        L.native("firing_state", "client0", **{"from": 2, "to": 10})
        L.at(11).native("firing_state", "client1", **{"from": 2, "to": 10})  # 1 tick later
        L.at(22).native("firing_state", "server", **{"from": 2, "to": 10})
        rep = run(L)
        self.assertEqual(check(rep, "copy_disagreement_ticks")["status"], "PASS")
        self.assertTrue(all(d["ticks"] <= 2 for d in rep["copy_disagreements"]))

    def test_persistent_ammo_disagreement_fails(self):
        L = Log()
        L.native("step", "client0", state=2, loaded=30, reserve=33)
        L.native("step", "client1", state=2, loaded=30, reserve=33)
        L.native("step", "server", state=2, loaded=3, reserve=60)  # server never saw the transfer
        L.at(500).native("step", "server", state=2, loaded=3, reserve=60)
        rep = run(L)
        d = rep["copy_disagreements"][0]
        self.assertEqual(sorted(d["fields"]), ["loaded", "reserve"])
        self.assertTrue(d["unresolved"] or d["ticks"] > 2)
        self.assertEqual(check(rep, "copy_disagreement_ticks")["status"], "FAIL")

    def test_resolved_after_threshold_fails_then_passes_with_larger_limit(self):
        L = Log()
        for c in ("client0", "client1"):
            L.native("step", c, state=2, loaded=30, reserve=33)
        L.native("step", "server", state=2, loaded=3, reserve=60)
        L.at(200).native("step", "server", state=2, loaded=30, reserve=33)
        rep = run(L)
        self.assertEqual(rep["copy_disagreements"][0]["unresolved"], False)
        self.assertGreater(rep["copy_disagreements"][0]["ticks"], 2)
        self.assertEqual(check(rep, "copy_disagreement_ticks")["status"], "FAIL")
        rep = run(L, max_copy_disagreement_ticks=100)
        self.assertEqual(check(rep, "copy_disagreement_ticks")["status"], "PASS")

    def test_ms_threshold(self):
        L = Log()
        L.native("step", "client0", state=2, loaded=30, reserve=33)
        L.native("step", "server", state=2, loaded=3, reserve=60)
        L.at(200).native("step", "server", state=2, loaded=30, reserve=33)
        rep = run(L, max_copy_disagreement_ticks=None, max_copy_disagreement_ms=100)
        self.assertEqual(check(rep, "copy_disagreement_ms")["status"], "FAIL")

    def test_state_disagreement_detected(self):
        L = Log()
        L.native("firing_state", "client0", **{"from": 11, "to": 12})
        L.native("firing_state", "client1", **{"from": 11, "to": 12})
        L.native("firing_state", "server", **{"from": 2, "to": 11})
        L.at(1000).native("step", "server", state=11, loaded=1, reserve=1)
        rep = run(L)
        self.assertIn("state", rep["copy_disagreements"][0]["fields"])

    def test_agreeing_copies_report_nothing(self):
        rep = run(clean_xm8_reload())
        self.assertEqual(rep["copy_disagreements"], [])

    def test_branch_alias(self):
        row = {"v": 1, "tick": 5, "qpc_us": 10, "src": "native", "phase": "state", "branch": 2,
               "weapon": XM8, "cur": 11, "prev": 10}
        ev = age.parse_event(row, 1)
        self.assertEqual((ev.copy, ev.phase, ev.raw["to"]), ("server", "firing_state", 11))


class CancelTests(unittest.TestCase):
    def build(self, reasons):
        L = Log()
        for i, r in enumerate(reasons):
            L.at(i * 1000).fvr("begin").at(i * 1000 + 100).fvr("cancel", reason=r)
            L.at(i * 1000 + 200).fvr("end", reason="cancel")
        return L

    def test_counts_by_reason(self):
        rep = run(self.build(["claim_lost", "claim_lost", "owner_changed"]))
        self.assertEqual(rep["cancels"], {"claim_lost": 2, "owner_changed": 1})
        self.assertEqual(check(rep, "cancels")["status"], "INFO")
        self.assertEqual(rep["result"], "PASS")
        self.assertEqual(rep["summary"]["outcomes"], {"cancelled": 3})

    def test_total_limit(self):
        rep = run(self.build(["a", "b"]), max_cancels=1)
        self.assertEqual(rep["result"], "FAIL")

    def test_per_reason_limit(self):
        rep = run(self.build(["claim_lost", "claim_lost"]), max_cancels_by_reason={"claim_lost": 1})
        self.assertEqual(check(rep, "cancels[claim_lost]")["status"], "FAIL")
        rep = run(self.build(["claim_lost"]), max_cancels_by_reason={"claim_lost": 1})
        self.assertEqual(rep["result"], "PASS")

    def test_denied_reason(self):
        rep = run(self.build(["interaction_rejected"]), deny_cancel_reasons=["interaction_rejected"])
        self.assertEqual(rep["result"], "FAIL")
        rep = run(self.build(["drop"]), deny_cancel_reasons=["interaction_rejected"])
        self.assertEqual(rep["result"], "PASS")


class HousekeepingTests(unittest.TestCase):
    def test_freeze_leak(self):
        L = Log().fvr("begin").native("freeze_start", "server").at(100).fvr("end", reason="cancel")
        rep = run(L)
        self.assertEqual(check(rep, "freeze_leaks")["value"], 1)
        self.assertEqual(rep["result"], "FAIL")

    def test_dropped_events_fail_and_stale_counted(self):
        L = Log().native("dropped", n=7).native("sample", "server", loaded=1, reserve=1, stale=1)
        rep = run(L, max_stale_samples=0)
        self.assertEqual(check(rep, "dropped_events")["value"], 7)
        self.assertEqual(check(rep, "stale_samples")["status"], "FAIL")
        self.assertEqual(rep["result"], "FAIL")

    def test_invalid_lines_fail(self):
        lines = clean_xm8_reload().lines() + ["{not json", '{"v":2,"tick":1,"qpc_us":1,"src":"fvr","phase":"x"}',
                                              '{"v":1,"tick":1,"qpc_us":1,"src":"gpu","phase":"x"}', ""]
        rep = age.analyze_lines(lines)
        self.assertEqual(len(rep["invalid_lines"]), 3)
        self.assertEqual(check(rep, "invalid_lines")["status"], "FAIL")
        rep = age.analyze_lines(lines, age.Thresholds(max_invalid_lines=3))
        self.assertEqual(rep["result"], "PASS")

    def test_unknown_phase_is_ignored_but_reported(self):
        L = clean_xm8_reload().fvr("future_thing", x=1)
        rep = run(L)
        self.assertEqual(rep["summary"]["unknown_phases"], {"future_thing": 1})
        self.assertEqual(rep["result"], "PASS")

    def test_min_reloads(self):
        rep = run(Log().fire(29, 33), min_reloads=1)
        self.assertEqual(rep["result"], "FAIL")

    def test_incomplete_reload_and_limit(self):
        L = Log().fvr("begin").state(2, 10)
        rep = run(L)
        self.assertEqual(rep["reloads"][0]["outcome"], "incomplete")
        self.assertEqual(rep["reloads"][0]["closed_by"], "eof")
        self.assertEqual(rep["result"], "PASS")
        self.assertEqual(run(L, max_incomplete_reloads=0)["result"], "FAIL")

    def test_begin_supersedes_unfinished_session(self):
        L = Log().fvr("begin").at(500).fvr("begin").at(900).fvr("end", reason="complete")
        rep = run(L)
        self.assertEqual([r["closed_by"] for r in rep["reloads"]], ["superseded", "end"])

    def test_two_weapons_are_separate_timelines(self):
        a, b = clean_xm8_reload(), Log(SPAS)
        b.t0 = a.t0
        b.fvr("begin").at(100).fvr("end", reason="swap")
        rep = age.analyze_lines(a.lines() + b.lines())
        self.assertEqual(sorted(r["weapon"] for r in rep["reloads"]), ["SPAS12_sp#0x3c4d", "XM8_sp_s#0x1a2b"])


class CliTests(unittest.TestCase):
    def cli(self, *args):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            try:
                code = age.main(list(args))
            except SystemExit as e:
                code = e.code
        return code, out.getvalue(), err.getvalue()

    def test_pass_text_exit_zero(self):
        with tempfile.TemporaryDirectory() as d:
            p = clean_xm8_reload().write(d)
            code, out, _ = self.cli(str(p))
        self.assertEqual(code, 0)
        self.assertIn("RESULT: PASS", out)

    def test_fail_json_exit_one(self):
        with tempfile.TemporaryDirectory() as d:
            p = clean_xm8_reload(credit_delay_ms=2800).write(d)
            code, out, _ = self.cli("--json", str(p))
        self.assertEqual(code, 1)
        doc = json.loads(out)
        self.assertEqual(doc["result"], "FAIL")
        self.assertEqual(doc["schema"], "fvr.gun_event_analysis.v1")
        self.assertFalse(doc["headset_accepted"])

    def test_cli_flag_overrides_threshold(self):
        with tempfile.TemporaryDirectory() as d:
            p = clean_xm8_reload(credit_delay_ms=2800).write(d)
            code, _, _ = self.cli(str(p), "--max-seat-credit-ms", "3000")
            self.assertEqual(code, 0)
            code, _, _ = self.cli(str(p), "--max-seat-credit-ms", "none")
            self.assertEqual(code, 0)

    def test_rules_file_and_flag_precedence(self):
        with tempfile.TemporaryDirectory() as d:
            p = clean_xm8_reload(credit_delay_ms=2800).write(d)
            rules = Path(d) / "rules.json"
            rules.write_text(json.dumps({"max_seat_credit_ms": 3000, "max_cancels": 0}))
            self.assertEqual(self.cli(str(p), "--rules", str(rules))[0], 0)
            # CLI beats the rules file
            self.assertEqual(self.cli(str(p), "--rules", str(rules), "--max-seat-credit-ms", "100")[0], 1)

    def test_bad_rules_exit_two(self):
        with tempfile.TemporaryDirectory() as d:
            p = clean_xm8_reload().write(d)
            rules = Path(d) / "rules.json"
            rules.write_text(json.dumps({"max_nonsense": 1}))
            code, _, err = self.cli(str(p), "--rules", str(rules))
        self.assertEqual(code, 2)
        self.assertIn("unknown rule", err)

    def test_missing_file_exit_two(self):
        self.assertEqual(self.cli("/nonexistent/gun.jsonl")[0], 2)

    def test_multiple_files_merge_native_and_fvr(self):
        full = clean_xm8_reload()
        native = Log()
        native.rows = [r for r in full.rows if r["src"] == "native"]
        fvr = Log()
        fvr.rows = [r for r in full.rows if r["src"] == "fvr"]
        with tempfile.TemporaryDirectory() as d:
            a, b = native.write(d, "native.jsonl"), fvr.write(d, "fvr.jsonl")
            code, out, _ = self.cli("--json", str(b), str(a))
        self.assertEqual(code, 0)
        doc = json.loads(out)
        self.assertEqual(doc["reloads"][0]["origin"], "both")
        self.assertEqual(doc["summary"]["events"], len(full.rows))

    def test_cancel_flags_and_dump_rules(self):
        with tempfile.TemporaryDirectory() as d:
            L = Log().fvr("begin").fvr("cancel", reason="claim_lost").fvr("end", reason="cancel")
            p = L.write(d)
            self.assertEqual(self.cli(str(p), "--deny-cancel-reason", "claim_lost")[0], 1)
            self.assertEqual(self.cli(str(p), "--max-cancels-by-reason", "claim_lost=0")[0], 1)
            self.assertEqual(self.cli(str(p), "--max-cancels-by-reason", "claim_lost=1")[0], 0)
        code, out, _ = self.cli("--dump-rules", "--max-cancels", "4")
        self.assertEqual(code, 0)
        self.assertEqual(json.loads(out)["max_cancels"], 4)


if __name__ == "__main__":
    unittest.main()
