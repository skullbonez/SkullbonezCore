"""Native grass slope, water, swept motion, overlap, capacity and Physics isolation proof.

Each case uses a generated fixture derived from the checked-in interaction scene.
Evidence and fixture copies stay under the requested fresh session directory.
"""
from __future__ import annotations
import argparse
import copy
import json
import math
import time
from replay_query import ReplayV2
from pathlib import Path
from skarness import SkarnessConnection, launch

ROOT = Path(__file__).resolve().parents[1]

def run(output: Path, case: str | None = None) -> None:
    assert not output.exists(), "Use a fresh evidence directory"
    output.mkdir(parents=True)
    base = json.loads((ROOT / "SkullbonezData/scenes/grass_interaction.scene.json").read_text())
    results = {}
    physics = {}
    for name in ("sleeping", "deleted", "teleport", "slope", "steep", "water", "fast", "rotating", "overlap", "overlap-reversed", "capacity", "terrain-edit", "branch", "prediction", "physics-off", "physics-on", "recording-gap", "non-lockstep", "retention-reset", "short-teleport", "hidden", "heightmap", "cinematic-shadows", "cinematic-relief"):
        if case and name != case: continue
        fixture = copy.deepcopy(base)
        sphere = fixture["objects"][0]
        steps = 2
        if name in ("sleeping","deleted","teleport","short-teleport"):
            sphere.update(type="ballState",velocity=[0,0,0],angularVelocity=[0,0,0],sleeping=name=="sleeping")
            fixture["objects"]=[sphere]
            steps=480 if name=="sleeping" else 2
        elif name in ("slope", "steep"):
            slope = .1 if name == "slope" else 1
            fixture["terrain"]["flatSlope"].update(baseY=-512*slope, slopeX=slope)
            sphere.update(fixed=True, velocity=[0,0,0])
        elif name == "water":
            fixture["simulation"]["world"]["fluidHeight"] = 10
        elif name == "fast":
            sphere.update(velocity=[1200,0,0], angularVelocity=[0,0,0])
        elif name == "rotating":
            sphere.update(type="boxState", halfExtents=[2,.4,.4], inertia=[.22,2.77,2.77],
                          position=[512,.4,512], velocity=[4,0,0], angularVelocity=[0,4,0])
            sphere.pop("radius")
            steps = 30
        elif name.startswith("overlap"):
            sphere.update(fixed=True, velocity=[0,0,0])
            second = copy.deepcopy(sphere)
            second.update(sceneObjectId=8104, name="overlap")
            fixture["objects"] = [sphere, second]
            if name.endswith("reversed"):fixture["objects"].reverse()
        elif name == "capacity":
            fixture["objects"] = [dict(type="ball", name=f"capacity_{i}", sceneObjectId=9000+i,
                position=[512,16,512], radius=16, mass=2, moment=204.8, fixed=True, restitution=0)
                for i in range(40)]
            steps = 1
        elif name == "terrain-edit":
            fixture["editor"] = {"editableScene": True}
            fixture["ui"] = {"visible": True, "minimized": False, "tab": "editor"}
            fixture["cameras"][0].update(position=[512,80,480],view=[512,0,512])
        elif name in ("branch", "prediction"):
            sphere.update(velocity=[120,0,0], angularVelocity=[0,0,0])
            steps = 60
        elif name.startswith("physics"):
            steps = 120
        session = output / name
        session.mkdir()
        if name == "heightmap":
            heightmap = session / "irregular.heightmap"
            side, spacing = 257, 4.1
            heights = [.18*math.sin((x*spacing-512)*.47)+.14*math.cos((z*spacing-512)*.39)
                       for x in range(side) for z in range(side)]
            heightmap.write_text("SKULLBONEZ_HEIGHTMAP 1\n257 4.1 15\n" + "\n".join(map(str, heights))+"\n")
            fixture["terrain"] = {"heightMap":str(heightmap.resolve())}
            sphere.update(fixed=True, velocity=[0,0,0])
        if name.startswith("cinematic-"):
            fixture["cinematic"].update(rendering=True, shadows=True,
                terrainReliefEnabled=True, terrainRelief=.4 if name=="cinematic-relief" else 0)
        if name == "non-lockstep":
            fixture["playback"]["fixedStep"]=False
            fixture["simulation"]["timeScale"]=10
            fixture["runtime"]["vsync"]=True
        path = session / "fixture.scene.json"
        path.write_text(json.dumps(fixture, indent=2)+"\n")
        assert launch(session, ROOT/"Automation/SKULLBONEZ_CORE.exe", path, hidden=True,
                      fixed_step=name!="non-lockstep", worker_threads=4, detail="summary") == 0
        connection = SkarnessConnection(session)
        def send(command, **arguments):
            response = connection.wait(connection.send(command,arguments))
            with (session/"commands.ndjson").open("a") as stream:
                stream.write(json.dumps(dict(command=command,arguments=arguments,response=response))+"\n")
            assert response.get("status") == "applied", response
            return response.get("result",response)
        def state():
            send("run.step_frames",count=3)
            latest = {}
            for line in (session/"runtime.skarness.ndjson").read_text().splitlines():
                row = json.loads(line)
                if "topic" in row:latest[row["topic"]] = row.get("payload",{})
            return latest["ui.presentation"]
        try:
            send("run.pause")
            send("capabilities.get")
            send("grass.enable_fixture",enabled=True)
            send("replay.set_recording_enabled",enabled=True)
            send("render.set_parameter",index=38,value=0 if name=="physics-off" else 2)
            if name == "hidden":
                send("editor.set_enabled",enabled=True)
                send("scene.object.set_visible",sceneObjectId=8101,visible=False)
                send("editor.set_enabled",enabled=False)
            send("run.step",count=steps)
            probe = send("grass.sample",x=512,z=512)
            body = send("scene.object.resolve",sceneObjectId=9000 if name=="capacity" else 8101)["objects"][0]
            if name in ("steep", "water"):
                assert probe["compression"] == 0 and probe["sceneObjectId"] == 0, probe
            elif name == "capacity":
                assert not probe["historyAvailable"], probe
            elif name == "fast":
                # The second tick moves roughly ten units. Every half-unit
                # root between its endpoints must retain this sphere's sweep.
                end = body["position"][0]
                assert end > 525, body
                probes = [send("grass.sample",x=x/2,z=512) for x in range(1048, int(end*2))]
                assert probes and all(p["sceneObjectId"]==8101 and p["compression"]>.1 for p in probes), probes
                probe = dict(center=probe,sweep=probes,body=body)
            elif name in ("slope", "rotating", "overlap", "overlap-reversed"):
                assert probe["sceneObjectId"] == 8101 and probe["compression"] > .5, probe
            if name == "hidden":
                assert probe["sceneObjectId"]==8101 and probe["compression"]>.1,probe
                send("run.step",count=3)
                send("replay.seek_frame",frame=1);state()
                retained=send("grass.sample",x=512,z=512)
                assert abs(retained["compression"]-probe["compression"])<1e-5 and retained["sceneObjectId"]==8101,(probe,retained)
                send("replay.return_to_live")
                send("replay.save",path=str(session/"hidden.skreplay"))
                send("replay.load",path=str(session/"hidden.skreplay"))
                send("replay.seek_frame",frame=1);state()
                loaded=send("grass.sample",x=512,z=512)
                assert loaded==retained,(loaded,retained)
                probe=dict(live=probe,retained=retained,loaded=loaded)
            if name == "sleeping":
                assert body["sleeping"] and probe["sceneObjectId"]==8101 and probe["compression"]>.5,(body,probe)
                send("run.step",count=120)
                assert send("grass.sample",x=512,z=512)["compression"]>.5
            if name == "deleted":
                send("editor.set_enabled",enabled=True)
                send("scene.object.select",scope="editor",sceneObjectId=8101)
                send("input.set_key",key=46,down=True);state()
                send("input.set_key",key=46,down=False);state()
                remaining=send("scene.object.list").get("objects",[])
                assert all(row["sceneObjectId"]!=8101 for row in remaining),remaining
                send("editor.set_enabled",enabled=False)
                send("run.step",count=480)
                expired=send("grass.sample",x=512,z=512)
                assert expired["compression"]==0,expired
                probe=dict(deleted=True,expired=expired)
            if name in ("teleport","short-teleport"):
                destination_x=518 if name=="short-teleport" else 552
                send("editor.set_enabled",enabled=True)
                send("scene.object.set_position",sceneObjectId=8101,position=[destination_x,1,512])
                send("editor.set_enabled",enabled=False)
                send("run.step",count=1)
                moved=send("scene.object.resolve",sceneObjectId=8101)["objects"][0]
                assert abs(moved["position"][0]-destination_x)<.01,moved
                across=[send("grass.sample",x=x,z=512) for x in range(515,destination_x-1,2)]
                assert all(row["compression"]==0 for row in across),across
                destination=send("grass.sample",x=destination_x,z=512)
                assert destination["sceneObjectId"]==8101 and destination["compression"]>.5,destination
                probe=dict(destination=destination,noConnectingSweep=across)
            if name == "recording-gap":
                send("run.step",count=58)
                send("replay.set_recording_enabled",enabled=False)
                send("run.step",count=60)
                send("replay.set_recording_enabled",enabled=True)
                send("run.step",count=2)
                send("replay.seek_frame",frame=60);state()
                missing=send("grass.sample",x=512,z=512)
                assert not missing["historyAvailable"] and missing["compression"]==0,missing
                send("replay.return_to_live");send("run.step",count=400)
                send("replay.seek_frame",frame=450);state()
                covered=send("grass.sample",x=512,z=512)
                assert covered["historyAvailable"],covered
                probe=dict(gapUnavailable=missing,finiteWindowRecovered=covered)
            if name == "retention-reset":
                send("run.step",count=60)
                send("replay.set_retention_seconds",seconds=45)
                send("run.step",count=2)
                reset_live=send("grass.sample",x=512,z=512)
                send("run.step",count=2);send("replay.seek_frame",frame=1);state()
                historical=send("grass.sample",x=512,z=512)
                assert historical["historyAvailable"] and abs(historical["compression"]-reset_live["compression"])<1e-5,(reset_live,historical)
                probe=dict(live=reset_live,historical=historical)
            if name == "non-lockstep":
                send("run.resume");time.sleep(.4);send("run.pause");state()
                send("replay.save",path=str(session/"multi-tick.skreplay"))
                artifact=ReplayV2(session/"multi-tick.skreplay")
                scene_frames=[row.scene_frame for row in artifact.solver_hashes]
                assert len(scene_frames)>5 and len(set(scene_frames))<len(scene_frames),scene_frames
                send("replay.seek_frame",frame=len(scene_frames)//2);state()
                probe=send("grass.sample",x=512,z=512)
                assert probe["historyAvailable"],probe
            if name == "terrain-edit":
                initial = state()
                send("replay.save",path=str(session/"before-edit.skreplay"))
                send("editor.set_terrain_brush",enabled=True)
                ui = state()
                x,y,w,h = ui["viewport"]
                center = dict(x=round(x+w/2),y=round(y+h/2))
                send("input.pointer_position",enabled=True,**center)
                send("input.pointer_drag",button="left",deltaX=0,deltaY=0,holdMilliseconds=350,**center)
                edited = state()
                assert edited["terrainRevision"] > initial["terrainRevision"], edited
                assert edited["terrainMaximumHeight"] > initial["terrainMaximumHeight"], edited
                send("editor.set_terrain_brush",enabled=False)
                send("editor.set_enabled",enabled=False)
                send("replay.load",path=str(session/"before-edit.skreplay"))
                send("replay.seek_frame",frame=0)
                state()
                unavailable = send("grass.sample",x=512,z=512)
                assert not unavailable["historyAvailable"] and unavailable["compression"] == 0, unavailable
                send("replay.return_to_live")
                send("scene.reset")
                reset = state()
                # Restart preserves authored editing state; reloading the fixture
                # replaces terrain and expires the generation-scoped opt-in.
                assert reset["terrainRevision"] == edited["terrainRevision"], reset
                send("scene.load",path="grass_interaction.scene.json")
                reloaded = state()
                assert reloaded["terrainRevision"] == 0 and not reloaded["grassEnabled"], reloaded
                probe = dict(editedRevision=edited["terrainRevision"],historical=unavailable,resetGenerationDisabled=True)
            if name == "branch":
                future_x = round(body["position"][0]*2)/2
                future = send("grass.sample", x=future_x, z=512)
                assert future["sceneObjectId"] == 8101 and future["compression"] > .5, (body,future)
                send("replay.seek_frame",frame=10)
                send("replay.restore_branch")
                send("run.step_frames",count=4)
                restored = send("scene.object.resolve",sceneObjectId=8101)["objects"][0]
                assert restored["position"][0] < future_x-5, (body,restored)
                cleared = send("grass.sample",x=future_x,z=512)
                assert cleared["compression"] == 0 and cleared["sceneObjectId"] == 0, cleared
                probe = dict(future=future,cleared=cleared,restored=restored)
            if name == "prediction":
                before = send("grass.sample",x=round(body["position"][0]*2)/2,z=512)
                send("prediction.select_target",sceneObjectId=8101)
                send("replay.set_prediction_horizon",seconds=1)
                send("replay.set_prediction_enabled",enabled=True)
                send("run.until",condition="prediction.complete",maxFrames=1000)
                send("replay.set_prediction_enabled",enabled=False)
                send("run.step_frames",count=4)
                after = send("grass.sample",x=round(body["position"][0]*2)/2,z=512)
                assert before == after, (before,after)
                probe = dict(before=before,after=after)
            if not name.startswith("physics"):
                send("capture.screenshot",path=str(session/"result.png"))
            ui = state()
            assert ui["grassCacheBytes"] <= 24*1024*1024, ui["grassCacheBytes"]
            assert ui["grassRootTests"] <= 131072, ui["grassRootTests"]
            if name == "capacity":assert ui["grassDroppedCells"] > 0, ui
            if name in ("heightmap", "cinematic-shadows"):
                assert ui["grassPatchCount"]>0,ui
                assert probe["historyAvailable"] and probe["compression"]>.1,probe
            if name == "cinematic-relief":assert not ui["grassEnabled"] and ui["grassPatchCount"]==0,ui
            results[name] = probe
        finally:
            send("session.stop")
        if name.startswith("physics"):
            rows = []
            for line in (session/"physics.physicsdiag.ndjson").read_text().splitlines():
                row = json.loads(line)
                if row.get("kind") in ("frame","body","contact","sleep_decision"):
                    row.pop("run",None)
                    rows.append(row)
            assert rows
            physics[name] = rows
    if "overlap" in results and "overlap-reversed" in results: assert results["overlap"] == results["overlap-reversed"]
    if "physics-off" in physics and "physics-on" in physics:
        assert physics["physics-off"] == physics["physics-on"], "Grass changed body/contact/sleep output"
        results["physicsIsolationRows"] = len(physics["physics-on"])
    (output/"result.json").write_text(json.dumps(results,indent=2)+"\n")
    print("PASS: native grass slopes, water, fast sphere, rotating box, overlap order, capacity and Physics isolation")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session",type=Path,required=True)
    parser.add_argument("--case")
    args = parser.parse_args()
    run(args.session.resolve(), args.case)
