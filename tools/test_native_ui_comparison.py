"""Protect native UI fixture retries and input-bound cache reuse."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import native_ui_comparison as fixture


class NativeComparisonFixtureTests(unittest.TestCase):
    def test_interrupted_attempt_is_preserved_and_changed_inputs_recapture(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            executable = root / "Automation/SKULLBONEZ_CORE.exe"
            executable.parent.mkdir()
            executable.write_bytes(b"binary")
            asset = root / "SkullbonezData/hulls/test.hull"
            asset.parent.mkdir(parents=True)
            asset.write_bytes(b"hull-v3")
            attempts = []

            def capture(_a, _b, _scene, output, seconds):
                self.assertEqual(seconds, 2.0)
                output.mkdir(parents=True, exist_ok=False)
                attempts.append(output)
                if len(attempts) == 1:
                    (output / "partial.txt").write_text("failed evidence")
                    raise RuntimeError("interrupted")
                manifest = output / "comparison.json"
                manifest.write_text(json.dumps({"status": "complete"}))
                return manifest

            with patch.object(fixture, "REPO", root), patch.object(fixture, "capture", capture):
                with self.assertRaisesRegex(RuntimeError, "interrupted"):
                    fixture.current_wall_comparison()
                completed = fixture.current_wall_comparison()
                self.assertNotEqual(attempts[0], attempts[1])
                self.assertEqual((attempts[0] / "partial.txt").read_text(), "failed evidence")
                self.assertEqual(fixture.current_wall_comparison(), completed)
                self.assertEqual(len(attempts), 2)
                asset.write_bytes(b"changed-hull")
                self.assertNotEqual(fixture.current_wall_comparison(), completed)
                self.assertEqual(len(attempts), 3)


if __name__ == "__main__":
    unittest.main()
