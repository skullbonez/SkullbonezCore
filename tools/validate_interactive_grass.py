"""Identity-bound native grass contact, finite recovery and recorded-time checks.

Grass is off by default and can be enabled only in the generated demo. This dedicated
fixture opts in through Skarness; activation expires with its scene generation.
Command acknowledgements are followed by field/state observations.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
from skarness import SkarnessConnection, launch

ROOT = Path(__file__).resolve().parents[1]


def check_elevated_visibility(send, state, session: Path):
    from PIL import Image, ImageChops, ImageFilter, ImageStat

    send("run.pause")
    send("window.resize", width=1600, height=900)
    height = state()["terrainCenterHeight"]
    send("camera.set_pose", eye=[512, height+180, 1040], target=[512, height, 512])
    observations = {}
    for quality, name in ((0, "off"), (1, "low"), (2, "high")):
        send("render.set_parameter", index=38, value=quality)
        ui = state()
        assert ui["grassEnabled"] == (quality != 0), ui
        assert 0 < ui["grassPatchCount"] < 12288 if quality else ui["grassPatchCount"] == 0, ui
        assert ui["grassCacheBytes"] <= 24*1024*1024, ui
        path = session / f"elevated-{name}.png"
        send("capture.screenshot", path=str(path))
        observations[name] = dict(patches=ui["grassPatchCount"], cacheBytes=ui["grassCacheBytes"])

    # Compare paused, identical views. Separate foreground regions must contain
    # both substantial grass coverage and more fine detail than the bare turf.
    # Geometry counts alone passed the old camera-centred 24-unit implementation.
    with Image.open(session / "elevated-off.png") as source:
        off = source.convert("RGB")
    for name in ("low", "high"):
        with Image.open(session / f"elevated-{name}.png") as source:
            on = source.convert("RGB")
        regions = []
        for bounds in ((100, 510, 450, 820), (500, 510, 850, 820)):
            grass, bare = on.crop(bounds), off.crop(bounds)
            histogram = ImageChops.difference(grass, bare).convert("L").histogram()
            changed = sum(histogram[10:])/sum(histogram)
            detail = ImageStat.Stat(grass.convert("L").filter(ImageFilter.FIND_EDGES)).mean[0]
            bare_detail = ImageStat.Stat(bare.convert("L").filter(ImageFilter.FIND_EDGES)).mean[0]
            assert changed > .35, (name, bounds, changed)
            assert detail > bare_detail*2+2, (name, bounds, detail, bare_detail)
            regions.append(dict(bounds=bounds, changedFraction=changed, detail=detail, bareDetail=bare_detail))
        observations[name]["regions"] = regions
    return observations


def run(session: Path) -> None:
    assert not session.exists(), "Use a fresh evidence directory"
    fixture = ROOT / "SkullbonezData/scenes/grass_interaction.scene.json"
    assert launch(session, ROOT / "Automation/SKULLBONEZ_CORE.exe", fixture,
                  hidden=True, fixed_step=True, detail="summary", worker_threads=4, allocation_guard="gameplay") == 0
    connection = SkarnessConnection(session)
    trace = session / "commands.ndjson"
    observations = {}
    offset = 0
    latest = {}

    def send(command, **arguments):
        result = connection.wait(connection.send(command, arguments))
        with trace.open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(dict(command=command, arguments=arguments, response=result))+"\n")
        assert result.get("status") == "applied", result
        return result.get("result", result)

    def state():
        nonlocal offset
        send("run.step_frames", count=3)
        with (session / "runtime.skarness.ndjson").open(encoding="utf-8") as stream:
            stream.seek(offset)
            for line in stream:
                event = json.loads(line)
                if "topic" in event:
                    latest[event["topic"]] = event.get("payload", {})
            offset = stream.tell()
        return latest["ui.presentation"]

    def probe(x=512, z=512):
        return send("grass.sample", x=x, z=z)

    def same_pressure(actual, expected):
        assert actual["historyAvailable"], actual
        assert actual["sceneObjectId"] == expected["sceneObjectId"], (actual, expected)
        assert abs(actual["compression"]-expected["compression"]) < 1e-6, (actual, expected)

    try:
        capabilities = send("capabilities.get")
        assert {"grass.sample", "grass.enable_fixture", "render.set_parameter", "camera.set_pose"} <= set(capabilities["commands"])
        send("run.pause")
        assert not state()["grassEnabled"]
        send("grass.enable_fixture", enabled=True)
        send("render.set_parameter", index=38, value=2)
        send("replay.set_recording_enabled", enabled=True)
        send("window.resize", width=1280, height=800)
        send("run.step", count=1)
        first = probe()
        held = probe(512, 517)
        airborne = probe(517, 517)
        assert first["sceneObjectId"] == 8101 and first["compression"] > .9, first
        assert held["sceneObjectId"] == 8102 and held["compression"] == 1, held
        assert airborne["sceneObjectId"] == 0 and airborne["compression"] == 0, airborne
        observations["firstContact"] = dict(sphere=first, box=held, airborne=airborne)
        send("run.step", count=59)
        trail = probe()
        assert trail["tick"] == 60 and .1 < trail["compression"] < .9, trail
        send("run.step_frames", count=30)
        assert probe() == trail
        observations["pauseAndTrail"] = trail
        send("camera.set_pose", eye=[600,3,600], target=[602,0,610])
        state()
        assert probe() == trail
        send("camera.set_pose", eye=[512,2.5,506], target=[514,0,516])
        ui = state()
        x,y,w,h = ui["headerFourViewsBounds"]
        send("input.pointer_position", enabled=True, x=round(x+w/2), y=round(y+h/2))
        state()
        send("input.pointer_drag", button="left", x=round(x+w/2), y=round(y+h/2), deltaX=0, deltaY=0)
        assert state()["fourViews"]
        send("run.step_frames", count=30)
        assert probe() == trail
        send("capture.screenshot", path=str(session / "four-views.png"))
        send("input.pointer_drag", button="left", x=round(x+w/2), y=round(y+h/2), deltaX=0, deltaY=0)
        assert not state()["fourViews"]
        observations["cameraAndFourViews"] = "same stable identity, tick, and compression after pan/return and four views"

        send("capture.screenshot", path=str(session / "live-trail.png"))
        send("run.step", count=120)
        # The solver may retain a shorter window. The seek must use the same
        # presentation track for resolution and rendering, including first entry.
        assert send("replay.seek_frame", frame=59)["frame"] == 59
        state()
        historical = probe()
        assert historical["tick"] == 59, historical
        same_pressure(historical, trail)
        observations["recordedParity"] = historical
        send("capture.screenshot", path=str(session / "recorded-trail.png"))
        assert send("replay.seek_frame", frame=30)["frame"] == 30
        state()
        reverse = probe()
        assert reverse["compression"] > historical["compression"] and reverse["sceneObjectId"] == 8101, reverse
        assert send("replay.seek_frame", frame=59)["frame"] == 59
        state()
        same_pressure(probe(), historical)
        observations["reverse"] = reverse
        send("render.set_parameter", index=38, value=0)
        off = state()
        assert not off["grassEnabled"] and off["grassPatchCount"] == 0, off
        send("capture.screenshot", path=str(session / "off.png"))
        send("render.set_parameter", index=38, value=1)
        low = state()
        assert low["grassEnabled"] and low["grassPatchCount"] > 0, low
        send("render.set_parameter", index=38, value=2)
        assert state()["grassEnabled"]
        same_pressure(probe(), historical)
        send("replay.jump_to_end")
        state()
        send("run.step", count=420)
        recovered = probe()
        assert recovered["compression"] == 0 and recovered["sceneObjectId"] == 0, recovered
        held = probe(512, 517)
        assert held["sceneObjectId"] == 8102 and held["compression"] == 1, held
        observations["finiteRecovery"] = dict(recovered=recovered, held=held)
        tick = held["tick"]
        send("render.set_parameter", index=41, value=.3)
        state()
        resized = probe(512, 517)
        assert resized["tick"] == tick and resized["sceneObjectId"] == 8102 and resized["compression"] == 1, resized
        send("render.set_parameter", index=43, value=2)
        state()
        assert probe(512, 517)["tick"] == tick
        send("render.set_parameter", index=38, value=0)
        state()
        send("run.step", count=12)
        send("render.set_parameter", index=38, value=2)
        state()
        restored = probe(512, 517)
        assert restored["tick"] == tick + 12 and restored["sceneObjectId"] == 8102 and restored["compression"] == 1, restored
        observations["pausedSettingsAndOffHistory"] = dict(resized=resized, restored=restored)

        send("render.set_parameter", index=41, value=.22)
        send("render.set_parameter", index=43, value=3)
        recording = session / "grass.skreplay"
        send("replay.save", path=str(recording))
        send("replay.load", path=str(recording))
        assert send("replay.seek_frame", frame=59)["frame"] == 59
        state()
        loaded = probe()
        same_pressure(loaded, historical)
        observations["savedLoadedParity"] = loaded
        send("capture.screenshot", path=str(session / "loaded-trail.png"))
        send("replay.return_to_live")
        send("scene.load", name="catto_single_box.scene.json")
        assert not state()["grassEnabled"]
        send("scene.load_demo")
        assert state()["grassEnabled"]
        observations["scenePolicy"] = "fixture explicit, Catto off, generated Demo on"
        observations["elevatedVisibility"] = check_elevated_visibility(send, state, session)
        (session / "result.json").write_text(json.dumps(observations, indent=2)+"\n", encoding="utf-8")
        print("PASS: grass contact, pause, recovery, historical parity, reverse, quality, scene policy and elevated visibility")
    finally:
        send("session.stop")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, required=True)
    args = parser.parse_args()
    run(args.session.resolve())
