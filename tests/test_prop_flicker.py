import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools" / "replay"))
import prop_flicker  # noqa: E402


def write(folder, name, rows):
    (folder / name).write_text("".join(json.dumps(r) + "\n" for r in rows))


class PropFlickerTests(unittest.TestCase):
    def run_case(self, props, pairs=range(10)):
        with tempfile.TemporaryDirectory() as d:
            folder = pathlib.Path(d)
            write(folder, "pairs.jsonl", [{"pair": p, "elapsed_ms": 100 * p} for p in pairs])
            write(folder, "props.jsonl", props)
            return prop_flicker.evaluate(folder, min_span_ms=500)

    def row(self, pair, props=(1, 1), pixels=(40, 38)):
        return {"pair": pair, "elapsed_ms": 100 * pair, "props": list(props), "prop_pixels": list(pixels)}

    def test_steady_prop_passes(self):
        result = self.run_case([self.row(p) for p in range(2, 9)])
        self.assertTrue(result["passed"])
        self.assertEqual(result["span_ms"], 600)

    def test_one_eye_publication_is_a_dropout(self):
        result = self.run_case([self.row(p, (0, 1) if p == 5 else (1, 1)) for p in range(2, 9)])
        self.assertFalse(result["passed"])
        self.assertEqual(result["one_eye_only"], 1)

    def test_missing_pair_inside_span_is_a_dropout(self):
        result = self.run_case([self.row(p) for p in range(2, 9) if p != 4])
        self.assertEqual([d["pair"] for d in result["dropouts"]], [4])

    def test_published_but_invisible_is_a_dropout(self):
        result = self.run_case([self.row(p, pixels=(0, 0) if p == 6 else (40, 38)) for p in range(2, 9)])
        self.assertEqual([d["pair"] for d in result["dropouts"]], [6])

    def test_short_span_fails(self):
        result = self.run_case([self.row(p) for p in range(2, 5)])
        self.assertFalse(result["passed"])
        self.assertIn("shorter", result["reason"])


if __name__ == "__main__":
    unittest.main()
