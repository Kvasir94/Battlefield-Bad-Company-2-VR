import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "replay"))
import run  # noqa: E402


class NewTraceFolder(unittest.TestCase):
    """A live attempt whose trace exits nonzero still finds its own trace folder."""

    def test_single_new_folder_is_recovered(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "reports" / "native-trace-20261008-055956-475").mkdir(parents=True)
            with mock.patch.object(run, "ROOT", root):
                before = set(run._trace_folders())
                new = root / "reports" / "native-trace-20261008-061056-141"
                new.mkdir()
                self.assertEqual(run._new_trace_folder(before), new)

    def test_none_or_ambiguous_is_not_guessed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "reports").mkdir()
            with mock.patch.object(run, "ROOT", root):
                before = set(run._trace_folders())
                self.assertIsNone(run._new_trace_folder(before))
                (root / "reports" / "native-trace-a").mkdir()
                (root / "reports" / "native-trace-b").mkdir()
                self.assertIsNone(run._new_trace_folder(before))


if __name__ == "__main__":
    unittest.main()
