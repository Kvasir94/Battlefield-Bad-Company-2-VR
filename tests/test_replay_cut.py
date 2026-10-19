import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "replay"))
import cut  # noqa: E402

ROW = "0 0 1 1 0 0 0 0 0 0 0 1 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 1"


def tape(rows):
    return ("FVR_BC2_INPUT 1 10000 5\n#! weapon=XM8_sp_s loaded<capacity anchor=world source=dll_gather_record\n"
            "#! expect transfers=1\n" + "".join(f"{n} {ROW.replace('0 0 1 1', f'{age} 0 1 1', 1)}\n" for n, age in rows))


def event(tick, phase, **fields):
    return json.dumps({"v": 1, "tick": tick, "qpc_us": tick * 10000, "src": "native" if phase in ("sample", "transfer") else "fvr",
                       "weapon": {"asset": "XM8_sp_s", "id": "0x1"}, "copy": "server" if phase in ("sample", "transfer") else None,
                       "phase": phase, **fields})


class CutTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.tape = self.dir / "session.fvrtape"
        self.tape.write_text(tape([(100, 0), (50, 3), (200, 0)]), encoding="ascii")
        self.events = self.dir / "gun-events.jsonl"
        self.events.write_text("\n".join([
            event(30, "sample", loaded=30, reserve=90), event(90, "transfer", loaded_before=4, reserve_before=86, loaded_after=30, reserve_after=60),
            event(120, "sample", loaded=22, reserve=60), event(200, "tape", reason="bookmark"), event(300, "tape", reason="bookmark")]) + "\n")

    def tearDown(self):
        self.tmp.cleanup()

    def ticks(self, text):
        return [int(line.split()[0]) for line in text.splitlines() if line[:1].isdigit()]

    def test_window_splits_runs_and_fills_conditions(self):
        text = cut.cut(self.tape, 90, 160, self.events, None)
        self.assertEqual(self.ticks(text), [11, 50, 10])
        header = text.splitlines()
        self.assertEqual(header[0], "FVR_BC2_INPUT 1 10000 5")
        self.assertIn("weapon=XM8_sp_s loaded=30 reserve=90", header[1])  # state as of tick 89
        self.assertIn("anchor=world", header[1])
        self.assertNotIn("loaded<capacity", header[1])
        self.assertTrue(header[2].startswith("#! source=cut:session.fvrtape:"))
        self.assertEqual(header[3], "#! expect transfers=1")

    def test_conditions_from_last_native_row_before_cut(self):
        text = cut.cut(self.tape, 150, 160, self.events, "credits=1 no-auto-reload")
        self.assertIn("loaded=22 reserve=60", text)
        self.assertIn("#! expect credits=1 no-auto-reload", text)

    def test_without_events_keeps_recorded_conditions(self):
        text = cut.cut(self.tape, 1, 10, None, None)
        self.assertIn("weapon=XM8_sp_s loaded<capacity", text)
        self.assertEqual(self.ticks(text), [10])

    def test_bookmark_window_and_cli(self):
        out = self.dir / "cut.fvrtape"
        with contextlib.redirect_stdout(io.StringIO()):
            code = cut.main([str(self.tape), "--events", str(self.events), "--bookmark", "1", "--before", "0.5",
                             "--after", "0.2", "--out", str(out)])
        self.assertEqual(code, 0)
        self.assertEqual(sum(self.ticks(out.read_text())), 71)  # ticks 150..220
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(cut.main([str(self.tape), "--events", str(self.events), "--bookmark", "3", "--out", str(self.dir / "x")]), 2)
            self.assertEqual(cut.main([str(self.tape), "--from-tick", "1", "--to-tick", "2", "--out", str(out)]), 2)  # exists

    def test_rejects_bad_windows_and_tapes(self):
        with self.assertRaises(ValueError):
            cut.cut(self.tape, 400, 500)
        with self.assertRaises(ValueError):
            cut.cut(self.tape, 10, 5)
        bad = self.dir / "bad.fvrtape"
        bad.write_text("KF2VR_INPUT 1 10000 0\n", encoding="ascii")
        with self.assertRaises(ValueError):
            cut.read_tape(bad)

    def test_committed_xm8_tape_cuts_cleanly(self):
        source = ROOT / "tools" / "replay" / "tapes" / "xm8-mag-reload.fvrtape"
        text = cut.cut(source, 600, 1129, None, None)
        self.assertEqual(sum(self.ticks(text)), 530)
        self.assertIn("#! expect transfers=1", text)


if __name__ == "__main__":
    unittest.main()
