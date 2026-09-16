"""Compare the native memory presenter with external Windows process counters."""
from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import time

from skarness import SkarnessConnection, launch
from validate_ui_themes import wait_for_exit

REPO = Path(__file__).resolve().parents[1]
MIB = 1024 * 1024


class ProcessCounters(ctypes.Structure):
    # Documented PROCESS_MEMORY_COUNTERS_EX2 ABI, queried outside the game.
    _fields_ = [("cb", wintypes.DWORD), ("faults", wintypes.DWORD)] + [
        (name, ctypes.c_size_t) for name in (
            "peakWorkingSet", "workingSet", "peakPaged", "paged",
            "peakNonpaged", "nonpaged", "pagefile", "peakPagefile",
            "privateCommit", "privateWorkingSet",
        )
    ] + [("sharedCommit", ctypes.c_ulonglong)]


def process_memory(process_id: int) -> dict:
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    psapi.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD]
    handle = kernel.OpenProcess(0x410, False, process_id)
    assert handle, ctypes.get_last_error()
    try:
        counters = ProcessCounters()
        counters.cb = ctypes.sizeof(counters)
        counters.privateWorkingSet = ctypes.c_size_t(-1).value
        assert psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb), ctypes.get_last_error()
        assert counters.privateWorkingSet != ctypes.c_size_t(-1).value, "Windows EX2 counter support required"
        return {name: getattr(counters, name) for name in ("workingSet", "privateCommit", "privateWorkingSet")}
    finally:
        kernel.CloseHandle(handle)


def run(directory: Path) -> None:
    directory = directory.resolve()
    preferences = directory.parent / (directory.name + ".preferences")
    preferences.parent.mkdir(parents=True, exist_ok=True)
    preferences.write_text("version 3\nlayout 0\nleft 280\nright 360\ndrawer 700\ndiagnostics 140\nfolded 7\ntool 1\nleftFolded 1\nrightFolded 1\nreplayFolded 1\n")
    assert launch(directory, REPO / "Automation/SKULLBONEZ_CORE.exe",
                  REPO / "SkullbonezData/scenes/prediction_ragdoll_wall_200.scene.json",
                  hidden=True, layout_file=preferences) == 0
    connection = SkarnessConnection(directory)
    process_id = json.loads((directory / "session.json").read_text())["processId"]
    latest: dict = {}
    offset = 0
    measurements = []

    def send(command: str, **arguments):
        result = connection.wait(connection.send(command, arguments))
        assert result.get("status") == "applied", result
        return result

    def read_state():
        nonlocal offset
        with (directory / "runtime.skarness.ndjson").open() as stream:
            stream.seek(offset)
            while True:
                start = stream.tell()
                line = stream.readline()
                if not line.endswith("\n"):
                    offset = start
                    break
                row = json.loads(line)
                if "topic" in row:
                    latest[row["topic"]] = row["payload"]
        return latest["ui.presentation"]

    def sample(label: str):
        # A full wall-clock sampling period must pass even though Physics is paused.
        time.sleep(1.1)
        send("run.step_frames", count=6)
        ui = read_state()
        external = process_memory(process_id)
        assert ui["memoryPrivateAvailable"], ui
        for field, counter in (("memoryPrivateBytes", "privateWorkingSet"),
                               ("memoryWorkingSetBytes", "workingSet"),
                               ("memoryCommitBytes", "privateCommit")):
            assert abs(ui[field] - external[counter]) < max(8 * MIB, external[counter] * .02), (label, field, ui[field], external)
        assert ui["memoryCapacityRowsValid"], (label, ui)
        assert not ui["panelDrawOverflow"], label
        assert ui["memoryCapacityTableBytes"] < 4 * external["privateCommit"], (label, ui)
        result = {"label": label, "external": external, "ui": ui, "replay": latest["replay.state"]}
        measurements.append(result)
        (directory / (label + ".json")).write_text(json.dumps(result, indent=2))
        send("capture.screenshot", path=str(directory / (label + ".png")))
        print(label, "private MiB", round(ui["memoryPrivateBytes"] / MIB, 2),
              "prediction capacity MiB", round(ui["memoryPredictionCapacityBytes"] / MIB, 2), flush=True)
        return result

    def click(x, y):
        send("input.pointer_position", x=int(x), y=int(y), enabled=True)
        send("input.pointer_drag", button="left", x=int(x), y=int(y), deltaX=0, deltaY=0)
        send("input.pointer_position", x=800, y=180, enabled=True)

    def key(code):
        send("input.set_key", key=code, down=True)
        send("run.step_frames", count=2)
        send("input.set_key", key=code, down=False)

    try:
        commands = send("capabilities.get")["commands"]
        assert {"input.set_key", "replay.set_prediction_horizon", "capture.screenshot"} <= set(commands)
        send("state.subscribe", topics=[], detail="normal")
        send("input.set_focus", focused=True)
        key(117)
        before = sample("before-prediction")
        assert before["ui"]["memoryWaterlineVisible"] and not before["ui"]["toolsVisible"]
        send("replay.set_prediction_detail", highDetail=True)
        send("replay.set_prediction_horizon", seconds=20)
        send("prediction.select_target", name="prediction_striker_ball")
        send("replay.set_prediction_enabled", enabled=True)
        send("run.until", condition="prediction.complete", maxFrames=10000)
        after = sample("after-prediction")
        replay = after["replay"]
        assert replay["predictionComplete"] and not replay["predictionBuilding"]
        assert replay["pathTargetId"] == replay["publishedPredictionTargetId"] == replay["submittedPredictionTargetId"] == 1
        assert replay["predictionSourceFrame"] == replay["submittedPredictionSourceFrame"] == 0
        assert replay["publishedPredictionTopologyVersion"] == replay["submittedPredictionTopologyVersion"]
        assert replay["publishedPredictionFrames"] == 2401
        assert after["ui"]["memoryPrivateBytes"] > before["ui"]["memoryPrivateBytes"] + 64 * MIB
        assert after["ui"]["memoryCommitBytes"] > before["ui"]["memoryCommitBytes"] + 64 * MIB
        assert after["ui"]["memoryPredictionCapacityBytes"] > before["ui"]["memoryPredictionCapacityBytes"] + 64 * MIB
        assert not after["ui"]["toolsVisible"]
        assert after["ui"]["memorySampleSeconds"] > before["ui"]["memorySampleSeconds"]
        width, height = after["ui"]["window"]
        click(width - 40, 20)
        send("run.step_frames", count=3)
        ui = read_state()
        bounds = ui["replayDetailsBounds"]
        click(bounds[0] + bounds[2] / 2, bounds[1] + bounds[3] / 2)
        send("run.step_frames", count=3)
        ui = read_state()
        drawer = ui["drawerBounds"]
        click(width * 10.5 / 11, drawer[1] + 70)
        send("input.pointer_position", x=int(width / 2), y=int(drawer[1] + 2), enabled=True)
        send("input.pointer_drag", button="left", x=int(width / 2), y=int(drawer[1] + 2), deltaX=0, deltaY=-390, moveClient=True)
        send("run.step_frames", count=3)
        drawer = read_state()["drawerBounds"]
        send("input.pointer_position", x=800, y=180, enabled=True)
        opened = sample("memory-menu")
        assert opened["ui"]["toolsVisible"] and opened["ui"]["memoryCapacityTableBytes"] > 0
        send("input.pointer_wheel", x=1000, y=700, wheelDelta=-1320)
        sample("capacity-rows")
        click(width * 1.5 / 11, drawer[1] + 70)
        closed = sample("other-menu")
        assert closed["ui"]["activeTool"] != opened["ui"]["activeTool"]
        assert abs(closed["ui"]["memoryPrivateBytes"] - opened["ui"]["memoryPrivateBytes"]) < 16 * MIB
        key(117)
        send("run.step_frames", count=3)
        click(width * 10.5 / 11, drawer[1] + 70)
        detail_only = sample("memory-without-f6")
        assert not detail_only["ui"]["memoryWaterlineVisible"]
        (directory / "result.json").write_text(json.dumps({"passed": True, "measurements": measurements}, indent=2))
        print("PASS: external counter parity, paused sampling, prediction growth, frame-owned capacity rows and tab-independent metrics")
    finally:
        try:
            send("session.stop")
        finally:
            connection.close()
            wait_for_exit(directory)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--session", type=Path, required=True)
    run(parser.parse_args().session)
