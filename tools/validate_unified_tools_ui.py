"""Check native Tools diagnostics, retained inspection state and shared hover help."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import time
from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]

def run(session: Path) -> None:
    assert launch(session, REPO / "Automation/SKULLBONEZ_CORE.exe",
                  REPO / "SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json", hidden=True) == 0
    connection = SkarnessConnection(session)
    latest: dict = {}
    offset = 0
    def send(command: str, **args: object) -> dict:
        result = connection.wait(connection.send(command, args))
        assert result.get("status") == "applied", result
        return result
    def sample(label: str) -> dict:
        nonlocal offset
        send("run.step_frames", count=3)
        with (session / "runtime.skarness.ndjson").open() as trace:
            trace.seek(offset)
            for line in trace:
                row = json.loads(line)
                if "topic" in row:
                    latest[row["topic"]] = row["payload"]
            offset = trace.tell()
        (session / (label + ".json")).write_text(json.dumps(latest, indent=2))
        return latest["ui.presentation"]
    def click(x: float, y: float) -> None:
        send("input.pointer_drag", button="left", x=int(x), y=int(y), deltaX=0, deltaY=0)
    def top(ui: dict) -> float:
        return ui["viewport"][1] + ui["viewport"][3] + (28 if ui["layout"] == "Editor" else 0)
    def tab(ui: dict, index: int, label: str) -> dict:
        click(14 + (ui["window"][0] - 28) * (index + 0.5) / 11, top(ui) + 66)
        result = sample(label)
        assert result["activeTool"] == index, result
        return result
    def hover(x: float, y: float, expected: int, label: str) -> dict:
        send("input.pointer_position", x=int(x), y=int(y), enabled=True)
        send("state.subscribe", topics=["ui.presentation"], detail="normal")
        until = time.monotonic() + 0.65
        while time.monotonic() < until:
            connection.read_event()
        send("state.subscribe", topics=[], detail="normal")
        result = sample(label)
        assert result["tooltipId"] == expected, result
        send("capture.screenshot", path=str((session / (label + ".png")).resolve()))
        send("input.pointer_position", x=0, y=0, enabled=False)
        return result
    try:
        assert "input.pointer_drag" in send("capabilities.get")["commands"]
        send("state.subscribe", topics=[], detail="normal")
        ui = sample("initial")
        width, height = ui["window"]
        click(width - 110, 20)
        ui = sample("editor")
        click(width / 2 - 44, height - 128)
        ui = sample("profiler-details")
        assert ui["toolsVisible"] and ui["activeTool"] == 0
        assert ui["profilerMarkerCount"] > 0 and ui["profilerDrawNodeCount"] > 0, ui
        y = top(ui)
        hover(90, y + 123, 2300, "profiler-worker-tooltip")
        workers = ui["workerThreads"]
        click(90, y + 123)
        changed = sample("worker-toggle")
        assert changed["workerThreads"] == (0 if workers else max(1, ui["maxWorkerThreads"] - 1)), changed
        click(90, y + 123)
        ui = sample("workers-restored")
        assert ui["workerThreads"] == workers, ui
        hover(43, y + 243, 4000, "profiler-expansion-tooltip")
        hover(600, y + 243, 4200, "profiler-values-tooltip")
        expansion = ui["profilerExpansionHash"]
        click(43, y + 243)
        ui = sample("profiler-root-fold")
        assert ui["profilerExpansionHash"] != expansion, ui
        folded = ui["profilerExpansionHash"]
        footer = height - 144 - 84
        click(250, footer + 60)
        ui = sample("profiler-timeline")
        assert ui["profilerTimeline"], ui
        send("capture.screenshot", path=str((session / "profiler-timeline.png").resolve()))
        click(width - 30, 20)
        closed = sample("tools-closed")
        assert not closed["toolsVisible"] and closed["profilerTimeline"]
        assert closed["profilerExpansionHash"] == folded
        click(width - 110, 20)
        ui = sample("canvas")
        assert ui["profilerExpansionHash"] == folded and ui["profilerTimeline"]
        click(width - 30, 20)
        ui = sample("tools-reopened")
        assert ui["activeTool"] == 0 and ui["profilerExpansionHash"] == folded
        ui = tab(ui, 10, "memory")
        y = top(ui)
        content_width = width - 44
        button_width = (content_width - 40) / 3
        for index, (seconds, budget) in enumerate(((60, 256), (45, 128), (20, 64))):
            x = 32 + index * (button_width + 6) + button_width / 2
            click(x, y + 147)
            ui = sample("memory-preset-" + str(index))
            assert (ui["replayMemoryPreset"], ui["replayRetentionSeconds"], ui["replayBudgetMiB"]) == (index, seconds, budget), ui
        hover(width / 2, y + 186, 2203, "memory-retention-tooltip")
        click(width * 0.25, y + 223)
        ui = sample("memory-budget-slider")
        assert ui["replayBudgetMiB"] != 64, ui
        retained = (ui["replayMemoryPreset"], ui["replayRetentionSeconds"], ui["replayBudgetMiB"])
        click(width - 30, 20)
        click(width - 110, 20)
        click(width - 44, height - 128)
        ui = sample("memory-editor-restored")
        assert ui["activeTool"] == 10 and ui["toolsVisible"]
        assert (ui["replayMemoryPreset"], ui["replayRetentionSeconds"], ui["replayBudgetMiB"]) == retained
        assert ui["profilerExpansionHash"] == folded and ui["profilerTimeline"]
        for index, position, tooltip_id, label in (
            (3, (100, 155), 2000, "physics"),
            (4, (100, 155), 2100, "options"),
            (5, (100, 155), 2800, "render"),
            (7, (width / 2, 158), 2700, "keys"),
            (8, (100, 196), 3101, "sky"),
            (9, (100, 233), 3301, "cinematic"),
        ):
            ui = tab(ui, index, "tab-" + label)
            hover(position[0], top(ui) + position[1], tooltip_id, label + "-tooltip")
        print("PASS: native Profiler worker/tree/timeline retention, Memory policy routes and Tools tooltips")
    finally:
        try:
            send("session.stop")
        finally:
            connection.close()

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--session", type=Path, required=True)
    run(parser.parse_args().session.resolve())
