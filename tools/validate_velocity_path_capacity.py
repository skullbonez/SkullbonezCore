"""Verify both comparison paths cover every moving body beyond the range limit."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import time

from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]


def run(session: Path, executable: Path, count: int) -> None:
    session.mkdir(parents=True, exist_ok=True)
    scene = json.loads((REPO / "SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json").read_text())
    scene["objects"] = [dict(type="box", name=f"moving_{i}",
                             position=[20.25 + (i % 80), 80.25, 20.25 + (i // 80)],
                             halfExtents=[.05, .05, .05], mass=1, restitution=0, fixed=False, velocity=[.1, 0, 0])
                        for i in range(count)]
    scene["cameras"] = [dict(name="capacity_overview", position=[60, 125, 95],
                             view=[60, 80, 38], up=[0, 1, 0])]
    fixture = session / "moving-bodies.scene.json"
    fixture.write_text(json.dumps(scene))
    assert launch(session, executable, fixture, hidden=True, model_capacity=count + 16,
                  worker_threads=4, allocation_guard="gameplay") == 0
    connection = SkarnessConnection(session)
    latest = {}
    offset = 0

    def send(command: str, **arguments: object) -> dict:
        result = connection.wait(connection.send(command, arguments))
        assert result["status"] == "applied", (command, result)
        return result

    def observe() -> dict:
        nonlocal offset
        send("run.step_frames", count=2)
        with (session / "runtime.skarness.ndjson").open("rb") as stream:
            stream.seek(offset)
            for line in stream:
                if not line.endswith(b"\n"):
                    break
                offset += len(line)
                row = json.loads(line)
                if "topic" in row:
                    latest[row["topic"]] = row["payload"]
        return latest.get("replay.state", {})

    def ready(predicate) -> dict:
        deadline = time.monotonic() + 120
        while time.monotonic() < deadline:
            observed = observe()
            if predicate(observed):
                return observed
        raise AssertionError("Prediction did not finish within 120 seconds")

    try:
        commands = {row["name"] for row in send("capabilities.get")["catalog"]}
        assert {"replay.velocity_preview", "replay.velocity_commit", "state.subscribe"} <= commands
        # Large packet evidence is read from the recorded stream. Keep live pipe
        # notifications compact so its 64 KiB transport cannot truncate them.
        send("state.subscribe", topics=["frame.clocks"], detail="normal")
        send("prediction.select_target", name="moving_0")
        send("replay.set_prediction_horizon", seconds=1)
        send("replay.set_prediction_enabled", enabled=True)
        stock = ready(lambda state: state.get("predictionComplete"))
        target = stock["pathTargetId"]
        assert target == stock["publishedPredictionTargetId"] == stock["submittedPredictionTargetId"]
        send("replay.set_velocity_edit_enabled", enabled=True)
        send("replay.velocity_preview", linear=[.15, 0, 0], angular=[0, 0, 0])
        send("replay.velocity_commit")
        ready(lambda state: state.get("divergence", {}).get("redReady"))
        send("replay.scrub", normalized=1.0)
        send("state.subscribe", topics=["frame.clocks"], detail="full")
        state = observe()
        send("state.subscribe", topics=["frame.clocks"], detail="normal")
        packet = latest["replay.visual_packet"]
        values = packet["retainedCompactRecords"]["values"]
        assert len(values) % 19 == 0
        # Capacity gaps are not draw commands. Inspect only the published ranges
        # that the renderer consumes, including ranges late in the second branch.
        records = [values[index * 19:(index + 1) * 19]
                   for span in packet["retainedRanges"]
                   for index in range(span["firstRecord"], span["firstRecord"] + span["recordCount"])]
        branches = {"original": set(), "modified": set()}
        for record in records:
            branch = "original" if record[9] > record[7] else "modified"
            # Initial x/z uniquely identifies the fixture body; every sampled
            # segment must stay on its body's one-second track.
            column = round(record[0] - 20.25)
            row = round(record[2] - 20.25)
            body = row * 80 + column
            assert 0 <= body < count
            assert abs(record[2] - scene["objects"][body]["position"][2]) < .001
            assert 0 <= record[3] - scene["objects"][body]["position"][0] <= .16
            branches[branch].add(body)
        expected = set(range(count))
        assert branches["original"] == expected, ("Original", len(branches["original"]), count)
        assert branches["modified"] == expected, ("Modified", len(branches["modified"]), count)
        assert state["publishedPredictionTargetId"] == state["submittedPredictionTargetId"] == target
        assert len(state["divergence"]["ghosts"]) == count
        result = {"passed": True, "bodyCount": count, "original": len(branches["original"]),
                  "modified": len(branches["modified"]), "records": len(records), "targetId": target}
        send("capture.screenshot", path=str(session / "comparison.png"))
    finally:
        try:
            send("session.stop")
        finally:
            connection.close()
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        shutdown = (session / "process.stdout.log").read_text(errors="replace")
        if "[allocation-guard] PASS:" in shutdown or "[allocation-guard] FAIL:" in shutdown:
            break
        time.sleep(.05)
    assert "[allocation-guard] PASS:" in shutdown, "Native shutdown did not pass the allocation guard"
    validation = (executable.parent.parent / "dx12_validation.txt").read_text()
    (session / "dx12_validation.txt").write_text(validation)
    assert validation.strip().splitlines()[-1] == "0", validation
    result["allocationGuard"] = "pass"
    result["dx12Errors"] = 0
    (session / "result.json").write_text(json.dumps(result, indent=2))
    print(f"PASS: Original and Modified paths cover all {count} moving bodies")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, required=True)
    parser.add_argument("--exe", type=Path, default=REPO / "Automation/SKULLBONEZ_CORE.exe")
    parser.add_argument("--bodies", type=int, default=3000)
    arguments = parser.parse_args()
    run(arguments.session.resolve(), arguments.exe.resolve(), arguments.bodies)
