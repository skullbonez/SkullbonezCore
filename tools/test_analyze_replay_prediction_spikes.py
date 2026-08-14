"""
File: tools/test_analyze_replay_prediction_spikes.py
Purpose:
  Proves the replay-prediction spike analyzer's isolated preparation, parsing,
  attribution, ordering, and interaction-completion contracts.

Summary:
  Small synthetic scenes and profiler rows exercise the diagnostic tool without
  launching the engine. The tests keep frame magnitude informational while
  rejecting malformed inputs and overlapping prediction generations.

Invariants:
  - Tests use temporary files and never mutate repository scenes or artifacts.
  - No test introduces a frame-time threshold or performance pass/fail budget.

Related:
  - tools/analyze_replay_prediction_spikes.py
  - tools/validate_replay_prediction_frame_spikes.bat
"""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import analyze_replay_prediction_spikes as diagnostic


class ReplayPredictionSpikeDiagnosticTests(unittest.TestCase):
    def test_run_workload_applies_the_engine_watchdog(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            log = root / "run.log"
            completed = mock.Mock(returncode=0)

            with mock.patch.object(diagnostic.subprocess, "run", return_value=completed) as run:
                diagnostic.run_workload(
                    Path("Automation/SKULLBONEZ_CORE.exe"),
                    Path("generated.scene.json"),
                    Path("interaction.json"),
                    Path("report.json"),
                    log,
                    frames=3800,
                    timeout_seconds=105.0,
                )

        command = run.call_args.args[0]
        self.assertEqual(command[command.index("--frames") + 1], "3800")
        self.assertEqual(run.call_args.kwargs["timeout"], 105.0)
        self.assertIs(run.call_args.kwargs["stderr"], diagnostic.subprocess.STDOUT)

    def test_run_workload_reports_watchdog_expiration(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            log = Path(temp_dir) / "run.log"
            expired = diagnostic.subprocess.TimeoutExpired(cmd="Automation/SKULLBONEZ_CORE.exe", timeout=105.0)

            with mock.patch.object(diagnostic.subprocess, "run", side_effect=expired):
                with self.assertRaisesRegex(RuntimeError, "exceeded 105 seconds"):
                    diagnostic.run_workload(
                        Path("Automation/SKULLBONEZ_CORE.exe"),
                        Path("generated.scene.json"),
                        Path("interaction.json"),
                        Path("report.json"),
                        log,
                        frames=3800,
                        timeout_seconds=105.0,
                    )

    def test_prepare_scene_injects_perf_logging_without_mutating_source(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            source = root / "source.scene.json"
            generated = root / "generated.scene.json"
            source_payload = {"format": "skullbonez.scene.json", "version": 3, "objects": []}
            source.write_text(json.dumps(source_payload), encoding="utf-8")

            diagnostic.prepare_scene(source, generated, "TestOutput/diagnostic/perf.csv")

            self.assertEqual(json.loads(source.read_text(encoding="utf-8")), source_payload)
            generated_payload = json.loads(generated.read_text(encoding="utf-8"))
            self.assertEqual(generated_payload["logging"]["perfLog"], "TestOutput/diagnostic/perf.csv")
            self.assertFalse(generated_payload["logging"]["perfLogFlush"])
            self.assertEqual(generated_payload["logging"]["perfLogFlushInterval"], 0)

    def test_parse_perf_csv_tracks_each_dynamic_header(self) -> None:
        csv_text = """# MEM start pass=1 task_manager_mb=100.0
pass,frame,Frame,Frame/Replay/Prediction/BeginJob
1,31,4.0000,1.0000
pass,frame,Frame,Frame/Replay/Prediction/BeginJob,Frame/Replay/Prediction/WorkerRange_worker
1,32,50.0000,20.0000,25.0000
"""
        with tempfile.TemporaryDirectory() as temp_dir:
            csv_path = Path(temp_dir) / "perf.csv"
            csv_path.write_text(csv_text, encoding="utf-8")

            rows = diagnostic.parse_perf_csv(csv_path)

        self.assertEqual([row["frame"] for row in rows], [31, 32])
        self.assertNotIn("Frame/Replay/Prediction/WorkerRange_worker", rows[0]["timings"])
        self.assertEqual(rows[1]["timings"]["Frame/Replay/Prediction/WorkerRange_worker"], 25.0)

    def test_analyze_ranks_spikes_and_attributes_direct_and_worker_time(self) -> None:
        rows = [
            {
                "pass": 1,
                "frame": 31,
                "timings": {"Frame": 4.0},
            },
            {
                "pass": 1,
                "frame": 32,
                "timings": {
                    "Frame": 50.0,
                    "Frame/Replay/Prediction/BeginJob": 20.0,
                    "Frame/Replay/Prediction/BeginJob/SeedPrivateEngine": 15.0,
                    "Frame/Replay/Prediction/WorkerRange_worker": 25.0,
                    "Counter/Physics/TotalBodies": 203.0,
                },
            },
        ]
        interaction = {
            "ok": True,
            "actions": [
                {
                    "frame": 31,
                    "type": "setReplayPathTarget",
                    "target": "prediction_wall_brick_r00_c00",
                }
            ],
            "assertions": [],
            "finalState": {"predictionGenerationCount": 4},
        }

        report = diagnostic.analyze(rows, interaction, top_count=2, correlation_radius=2)

        spike = report["spikes"][0]
        self.assertEqual(spike["frame"], 32)
        self.assertEqual(spike["frame_ms"], 50.0)
        self.assertEqual(spike["unattributed_frame_ms"], 30.0)
        self.assertEqual(spike["nearby_events"][0]["frame"], 31)
        self.assertEqual(spike["worker_markers"][0]["name"], "Frame/Replay/Prediction/WorkerRange_worker")
        self.assertEqual(spike["worker_markers"][0]["ms"], 25.0)
        direct = {marker["name"]: marker["ms"] for marker in spike["direct_cpu_markers"]}
        self.assertEqual(direct["Frame/Replay/Prediction/BeginJob/SeedPrivateEngine"], 15.0)
        self.assertEqual(direct["Frame/Replay/Prediction/BeginJob"], 5.0)
        self.assertNotIn("Counter/Physics/TotalBodies", direct)

    def test_validate_interaction_rejects_overlap_and_accepts_completed_generations(self) -> None:
        valid_actions = [
            {"frame": 1, "setReplayPredictionHorizonSeconds": 120.0},
            {"frame": 2, "clickReplayControl": "predict"},
            {"frame": 100, "assert": {"predictionFullHorizonComplete": True}},
            {"frame": 101, "setReplayPathTarget": "body_b"},
            {"frame": 200, "assert": {"predictionFullHorizonComplete": True}},
            {"frame": 201, "clickReplayControl": "past"},
        ]
        overlapping_actions = [
            {"frame": 1, "setReplayPredictionHorizonSeconds": 120.0},
            {"frame": 2, "clickReplayControl": "predict"},
            {"frame": 3, "setReplayPathTarget": "body_b"},
        ]

        diagnostic.validate_interaction_actions(valid_actions)

        with self.assertRaisesRegex(ValueError, "frame 3.*before the active prediction completed"):
            diagnostic.validate_interaction_actions(overlapping_actions)


if __name__ == "__main__":
    unittest.main()
