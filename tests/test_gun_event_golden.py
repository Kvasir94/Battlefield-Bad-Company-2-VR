"""The live emitter's formatter output (tests/data/gun-events-live-xm8.golden.jsonl,
written byte-for-byte by Bc2GunEventLogTests from the DLL's tracker entry points)
must pass both judges the replay runner uses: tools/analyze_gun_events.py and
tools/replay/replay_assert.py with the xm8-mag-reload scenario and tape. This
proves the live shape only; it is not a live game or headset pass."""
import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "replay"))
import analyze_gun_events as age  # noqa: E402
import replay_assert  # noqa: E402

GOLDEN = ROOT / "tests" / "data" / "gun-events-live-xm8.golden.jsonl"
SCENARIO = ROOT / "tools" / "replay" / "scenarios" / "xm8-mag-reload.json"
TAPE = ROOT / "tools" / "replay" / "tapes" / "xm8-mag-reload.fvrtape"


def _run(main, argv):
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
        code = main(argv)
    return code, out.getvalue()


class LiveGoldenTests(unittest.TestCase):
    def rows(self):
        return [json.loads(line) for line in GOLDEN.read_text(encoding="utf-8").splitlines()]

    def test_lines_are_strict_schema_v1(self):
        raw = GOLDEN.read_bytes()
        self.assertFalse(raw.startswith(b"\xef\xbb\xbf"))
        self.assertTrue(raw.endswith(b"\n") and b"\r" not in raw)
        for line in raw.decode("ascii").splitlines():
            row = json.loads(line)
            self.assertEqual(row["v"], 1)
            self.assertEqual(list(row)[:4], ["v", "tick", "qpc_us", "src"])
            self.assertIn(row["src"], ("native", "fvr"))
            self.assertNotIn("sim", row)  # live rows never claim to be simulator output
            self.assertFalse(any("chamber" in key for key in row))  # chamber is never game truth
            if row["src"] == "fvr":
                self.assertIsNone(row["copy"])
            age.parse_event(row, 1)

    def test_all_three_copies_and_both_producers(self):
        rows = self.rows()
        copies = {r["copy"] for r in rows if r["phase"] == "transfer"}
        self.assertEqual(copies, {"client0", "client1", "server"})
        states = [(r["from"], r["to"]) for r in rows if r["phase"] == "firing_state" and r["copy"] == "server"]
        self.assertEqual(states, [(2, 10), (10, 11), (11, 12), (12, 1), (1, 2)])
        self.assertEqual({r["src"] for r in rows}, {"native", "fvr"})
        ticks = [r["tick"] for r in rows]
        self.assertEqual(ticks, sorted(ticks))

    def test_analyzer_passes(self):
        code, text = _run(age.main, ["--min-reloads", "1", "--max-seat-credit-ms", "1000", str(GOLDEN)])
        self.assertEqual(code, 0, text)
        self.assertIn("RESULT: PASS", text)
        code, text = _run(age.main, ["--json", str(GOLDEN)])
        report = json.loads(text)
        self.assertFalse(report["headset_accepted"])

    def test_replay_assert_passes_scenario_and_tape_expectations(self):
        code, text = _run(replay_assert.main, [str(GOLDEN), "--scenario", str(SCENARIO), "--tape", str(TAPE)])
        self.assertEqual(code, 0, text)
        self.assertIn("RESULT: PASS (live", text)

    def test_judges_still_fail_a_broken_live_log(self):
        rows = [r for r in self.rows() if not (r["phase"] == "transfer" and r["copy"] == "server")]
        rows.append({"v": 1, "tick": 200, "qpc_us": 3000000, "src": "native", "copy": None, "phase": "dropped", "n": 4})
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "gun-events.jsonl"
            path.write_text("".join(json.dumps(r) + "\n" for r in rows), encoding="utf-8")
            self.assertEqual(_run(age.main, [str(path)])[0], 1)
            self.assertEqual(_run(replay_assert.main, [str(path), "--scenario", str(SCENARIO)])[0], 1)


if __name__ == "__main__":
    unittest.main()
