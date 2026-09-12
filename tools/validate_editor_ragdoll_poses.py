"""Place sleeping ragdoll poses through native editor input and round-trip a level."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import time

from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]


def run(session: Path) -> None:
    session.mkdir(parents=True, exist_ok=True)
    scene = session / "poses.scene.json"
    scene.write_text(json.dumps({
        "format": "skullbonez.scene.json", "version": 5,
        "simulation": {"physics": True, "text": True,
                       "world": {"gravity": -9.81, "fluidHeight": -1000, "fluidDensity": 0}},
        "editor": {"editableScene": True},
        "playback": {"frames": "unlimited", "fixedStep": True},
        "terrain": {"flatSlope": {"baseY": 30, "slopeX": 0, "slopeZ": 0}},
        "debug": {"waterHidden": True},
        "cameras": [{"name": "main", "position": [500, 110, 620],
                     "view": [500, 40, 500], "up": [0, 1, 0]}], "objects": []
    }), encoding="utf-8")
    assert launch(session, REPO / "Automation/SKULLBONEZ_CORE.exe", scene,
                  hidden=True, fixed_step=True, allocation_guard="gameplay") == 0
    connection = SkarnessConnection(session)
    latest: dict = {}
    offset = 0

    def send(command: str, **arguments: object) -> dict:
        reply = connection.wait(connection.send(command, arguments))
        assert reply.get("status") == "applied", (command, reply)
        return reply

    def observe(label: str) -> dict:
        nonlocal offset
        send("run.step_frames", count=3)
        with (session / "runtime.skarness.ndjson").open(encoding="utf-8") as stream:
            stream.seek(offset)
            for line in stream:
                event = json.loads(line)
                if "topic" in event:
                    latest[event["topic"]] = event["payload"]
            offset = stream.tell()
        (session / f"{label}.json").write_text(json.dumps(latest, indent=2), encoding="utf-8")
        return latest["ui.presentation"]

    def click(x: float, y: float) -> None:
        send("input.pointer_position", enabled=True, x=int(x), y=int(y))
        send("run.step_frames", count=2)
        time.sleep(0.22)
        send("input.pointer_drag", button="left", x=int(x), y=int(y), deltaX=0, deltaY=0)

    def middle(bounds: list) -> None:
        x, y, width, height = bounds
        assert width > 0 and height > 0
        click(x + width / 2, y + height / 2)

    def resolve(name: str) -> dict:
        reply = send("scene.object.resolve", name=name)
        return reply["result"]["objects"][0]

    def bodies(names: list[str]) -> dict:
        return {name: resolve(name) for name in names}

    def require_sleeping(rows: dict) -> None:
        for name, body in rows.items():
            assert body["sleepStateAvailable"] and body["sleeping"] and not body["fixed"], (name, body)
            assert body["linearVelocity"] == [0, 0, 0] and body["angularVelocity"] == [0, 0, 0], body

    try:
        catalog = send("capabilities.get")
        assert {"input.pointer_drag", "input.pointer_wheel", "scene.object.resolve", "scene.save"} <= set(catalog["commands"])
        (session / "capabilities.json").write_text(json.dumps(catalog, indent=2), encoding="utf-8")
        send("state.subscribe", topics=[], detail="normal")
        ui = observe("initial")
        if ui["layout"] != "Editor":
            middle(ui["headerLayoutBounds"])
            ui = observe("editor-layout")
        if ui["editorControlsBounds"][2] == 0:
            middle(ui["editorTabBounds"])
            ui = observe("editor-panel")
        if not ui["editorMode"]:
            x, y, _, _ = ui["editorControlsBounds"]
            click(x + 25, y + 54)
            ui = observe("editor-enabled")
        assert ui["editorObjectOptions"] == 39
        groups = []
        all_names: list[str] = []
        for order, object_type in enumerate((29, 37, 38)):
            x, y, _, _ = ui["editorControlsBounds"]
            click(x + 110, y + 286)
            ui = observe(f"popup-{object_type}")
            assert ui["editorPopupOpen"]
            for _ in range(50):
                if ui["editorPopupFirstOption"] <= object_type < ui["editorPopupFirstOption"] + ui["editorPopupVisibleOptions"]:
                    break
                px, py, _, _ = ui["editorPopupBounds"]
                send("input.pointer_wheel", x=int(px + 10), y=int(py + 10),
                     wheelDelta=-120 if object_type >= ui["editorPopupFirstOption"] else 120)
                ui = observe(f"scroll-{object_type}")
            px, py, _, ph = ui["editorPopupBounds"]
            click(px + 20, py + (object_type - ui["editorPopupFirstOption"] + .5) * ph / ui["editorPopupVisibleOptions"])
            ui = observe(f"selected-{object_type}")
            assert ui["editorObjectType"] == object_type and ui["editorPlacement"]
            if ui["editorStaticObject"]:
                x, y, _, _ = ui["editorControlsBounds"]
                click(x + 25, y + 242)
                ui = observe("dynamic-placement")
            x, y, width, height = ui["viewport"]
            cx, cy = int(x + width * (.25 + order * .25)), int(y + height * .55)
            send("input.pointer_position", enabled=True, x=cx, y=cy)
            observe(f"preview-{object_type}")
            send("capture.screenshot", path=str(session / f"preview-{object_type}.png"))
            click(cx, cy)
            ui = observe(f"placed-{object_type}")
            listed = send("scene.object.list")["result"]["objects"]
            names = [row["name"] for row in listed if row["name"] not in all_names]
            assert len(names) == 10, listed
            group = bodies(names)
            require_sleeping(group)
            head = next(row["position"][1] for name, row in group.items() if name.endswith("_head"))
            left = next(row["position"][1] for name, row in group.items() if name.endswith("_lower_arm_l"))
            right = next(row["position"][1] for name, row in group.items() if name.endswith("_lower_arm_r"))
            assert (left > head) == (order >= 1) and (right > head) == (order == 2), group
            groups.append(group)
            all_names.extend(names)
        # Placement is dynamic sleep, not static pinning: physics ticks must leave
        # all undisturbed parts exactly at their authored pose.
        if ui['editorMode']:
            x, y, _, _ = ui['editorControlsBounds']
            click(x + 25, y + 54)
            ui = observe('leave-editor-for-physics')
        assert not ui['editorMode']
        before = bodies(all_names)
        send("run.step", count=240)
        after = bodies(all_names)
        require_sleeping(after)
        assert before == after
        send("scene.save")
        observe("saved")
        saved = json.loads(scene.read_text(encoding="utf-8"))
        assert len(saved["objects"]) == 30 and len(saved["ragdollJoints"]) == 27
        send("scene.reset")
        observe("reloaded")
        restored = bodies(all_names)
        require_sleeping(restored)
        for name in all_names:
            assert restored[name]["position"] == before[name]["position"]
        send("run.step", count=240)
        require_sleeping(bodies(all_names))
        send("capture.screenshot", path=str(session / "three-sleeping-poses.png"))
        # An awake striker must wake the connected ragdoll, while the other
        # two separated ragdolls keep their exact sleeping poses.
        head_name = next(name for name in groups[0] if name.endswith("_head"))
        striker = dict(next(row for row in saved["objects"] if row["name"] == head_name))
        striker.update(sceneObjectId=31, name="wake_striker", sleeping=False,
                       position=[before[head_name]["position"][0], before[head_name]["position"][1] + 8,
                                 before[head_name]["position"][2]], velocity=[0, -30, 0])
        saved["objects"].append(striker)
        impact_scene = session / "impact.scene.json"
        impact_scene.write_text(json.dumps(saved), encoding="utf-8")
        (session / "saved-poses.scene.json").write_bytes(scene.read_bytes())
        scene.write_bytes(impact_scene.read_bytes())
        send("scene.reset")
        observe("impact-loaded")
        send("run.step", count=120)
        struck = bodies(list(groups[0]))
        assert all(not row["sleeping"] for row in struck.values()), struck
        assert any(row["position"] != before[name]["position"] for name, row in struck.items())
        unaffected = bodies(all_names[10:])
        require_sleeping(unaffected)
        assert all(row == before[name] for name, row in unaffected.items())
        observe("impact-woke-connected-ragdoll")
        send("capture.screenshot", path=str(session / "impact-wake.png"))
        result = {"passed": True, "poses": groups,
            "sleepingBodies": 30, "savedJoints": 27, "unchangedAfterTicks": 240,
            "impactWokeBodies": len(struck), "undisturbedSleepers": len(unaffected)}
    finally:
        try:
            send("session.stop")
        finally:
            connection.close()
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        output = (session / "process.stdout.log").read_text(encoding="utf-8", errors="replace")
        if "[allocation-guard] PASS:" in output or "[allocation-guard] FAIL:" in output:
            break
        time.sleep(.05)
    assert "[allocation-guard] PASS:" in output
    report = (REPO / "dx12_validation.txt").read_text()
    (session / "dx12_validation.txt").write_text(report)
    assert report.strip().splitlines()[-1] == "0", report
    (session / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print("PASS: three sleeping poses, native placement, save/reload, impact wake and zero DX12 errors")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=REPO / "TestOutput/skarness/editor-ragdoll-poses")
    run(parser.parse_args().session.resolve())
