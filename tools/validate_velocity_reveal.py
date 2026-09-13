"""Verify retained Original geometry and paced, full-detail Modified paths."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import time

from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]


def run(session: Path, executable: Path) -> None:
    session.mkdir(parents=True, exist_ok=True)
    scene = REPO / "SkullbonezData/scenes/prediction_ragdoll_wall_200.scene.json"
    assert launch(session, executable, scene, hidden=True, worker_threads=4,
                  layout_file=session / "layout.preferences", allocation_guard="gameplay") == 0
    connection = SkarnessConnection(session)
    latest = {}
    offset = 0
    samples = []

    def send(command: str, **arguments: object) -> dict:
        result = connection.wait(connection.send(command, arguments))
        assert result["status"] == "applied", (command, result)
        return result

    def observe() -> tuple[dict, dict]:
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
        state, packet = latest["replay.state"], latest["replay.visual_packet"]
        samples.append({"time": time.monotonic(), "header": packet["header"],
                        "original": packet["originalPath"], "active": packet["activePath"],
                        "originalStreamId": packet["originalStreamId"]})
        return state, packet

    def ready(predicate) -> tuple[dict, dict]:
        deadline = time.monotonic() + 300
        while time.monotonic() < deadline:
            state, packet = observe()
            if predicate(state, packet):
                return state, packet
        raise AssertionError("Prediction did not reach the expected state")

    def screenshot(name: str) -> None:
        send("capture.screenshot", path=str(session / f"{name}.png"))

    try:
        catalog = send("capabilities.get")["catalog"]
        (session / "capabilities.json").write_text(json.dumps(catalog), encoding="utf-8")
        assert {"replay.velocity_preview", "replay.velocity_commit", "replay.set_reveal_speed"} <= {row["name"] for row in catalog}
        send("state.subscribe", topics=["frame.clocks"], detail="normal")
        send("replay.set_prediction_detail", highDetail=True)
        send("replay.set_prediction_horizon", seconds=20)
        send("replay.set_reveal_speed", rate=1000)
        send("prediction.select_target", name="prediction_striker_ball")
        send("replay.set_prediction_enabled", enabled=True)
        stock, normal = ready(lambda state, packet: state["predictionComplete"] and not state["causeLoading"] and packet["header"]["revealFrame"] >= 2400)
        target = stock["pathTargetId"]
        assert target == stock["publishedPredictionTargetId"] == stock["submittedPredictionTargetId"] == 1
        original = normal["activePath"]
        assert original["records"] > 2400, original
        screenshot("normal")
        send("replay.set_velocity_edit_enabled", enabled=True)
        _, widget = observe()
        assert widget["activePath"] == original
        assert widget["originalStreamId"] == 0
        send("replay.velocity_preview", linear=[130, -1, 0], angular=[0, 0, -14])
        held, packet = observe()
        frozen = packet["originalPath"]
        stream_id = packet["originalStreamId"]
        assert frozen["records"] == original["records"], (frozen, original)
        assert frozen["geometryHash"] == original["geometryHash"], (frozen, original)
        assert frozen["allBlue"] and stream_id != 0
        assert held["divergence"]["active"] and not held["predictionGenerationPermitted"]
        assert packet["activePath"]["records"] == 0
        screenshot("held")
        send("replay.velocity_commit")
        start = len(samples)
        _, partial = ready(lambda state, packet: packet["activePath"]["records"] > 100 and packet["header"]["revealFrame"] < 1800)
        assert partial["activePath"]["allRed"]
        screenshot("partial")
        _, complete = ready(lambda state, packet: state["divergence"]["redReady"] and packet["header"]["revealFrame"] >= 2400)
        assert complete["activePath"]["records"] > partial["activePath"]["records"]
        assert complete["activePath"]["records"] > 2400
        assert complete["activePath"]["allRed"]
        reveal_samples = samples[start:]
        assert len({row["header"]["revealFrame"] for row in reveal_samples}) > 10
        for row in reveal_samples:
            assert row["original"] == frozen and row["originalStreamId"] == stream_id
        screenshot("complete")
        send("replay.velocity_preview", linear=[110, -1, 0], angular=[0, 0, -14])
        state, grabbed = observe()
        assert not state["predictionGenerationPermitted"]
        assert grabbed["originalPath"] == frozen and grabbed["originalStreamId"] == stream_id
        send("replay.velocity_commit")
        _, restarted = ready(lambda state, packet: 0 < packet["header"]["revealFrame"] < 1200 and packet["activePath"]["records"] > 0)
        assert restarted["originalPath"] == frozen
        result = {"passed": True, "targetId": target, "original": frozen,
                  "modifiedComplete": complete["activePath"], "revealSamples": len(reveal_samples)}
    finally:
        (session / "samples.json").write_text(json.dumps(samples, indent=2), encoding="utf-8")
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
    assert "[allocation-guard] PASS:" in shutdown, shutdown[-2000:]
    validation = (executable.parent.parent / "dx12_validation.txt").read_text()
    (session / "dx12_validation.txt").write_text(validation)
    assert validation.strip().splitlines()[-1] == "0", validation
    result.update(allocationGuard="pass", dx12Errors=0)
    (session / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print("PASS: Original retained exactly; Modified reveals progressively at normal detail")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, required=True)
    parser.add_argument("--exe", type=Path, default=REPO / "Automation/SKULLBONEZ_CORE.exe")
    args = parser.parse_args()
    run(args.session.resolve(), args.exe.resolve())
