"""Verify diagnostic controls and inspection retention in both native layouts."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
from PIL import Image
from skarness import SkarnessConnection, launch
REPO=Path(__file__).resolve().parents[1]

def run(session: Path):
    session=session.resolve()
    assert launch(session, REPO/"Automation/SKULLBONEZ_CORE.exe", REPO/"SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json", hidden=True)==0
    c=SkarnessConnection(session); latest={}; offset=0
    def send(command, **args):
        r=c.wait(c.send(command,args)); assert r.get("status")=="applied",r; return r
    def sample(label):
        nonlocal offset
        send("run.step_frames",count=3)
        with (session/"runtime.skarness.ndjson").open() as f:
            f.seek(offset)
            for line in f:
                r=json.loads(line)
                if "topic" in r: latest[r["topic"]]=r["payload"]
            offset=f.tell()
        ui=latest["ui.presentation"]
        (session/(label+".json")).write_text(json.dumps(ui,indent=2)); return ui
    def click(x,y): send("input.pointer_drag",button="left",x=int(x),y=int(y),deltaX=0,deltaY=0)
    def top(ui): return ui["viewport"][1]+ui["viewport"][3]+(28 if ui["layout"]=="Editor" else 0)
    def tab(ui,index):
        click(14+(ui["window"][0]-28)*(index+.5)/11,top(ui)+66)
        ui=sample("tab-"+str(index)); assert ui["activeTool"]==index; return ui
    def scroll(ui,delta):
        x,y,w,h=ui["toolsContentBounds"]
        send("input.pointer_wheel",x=int(x+w/2),y=int(y+h/2),wheelDelta=delta)
        return sample("scroll")
    def capture(label):
        p=session/(label+".png");send("capture.screenshot",path=str(p))
        with Image.open(p) as img: img.save(session/(label+"-view.png"))
    try:
        assert "input.pointer_drag" in send("capabilities.get")["commands"]
        send("state.subscribe",topics=[],detail="normal")
        ui=sample("initial")
        for code in (0x74,0x75):
            send("input.set_key",key=code,down=True);send("run.step_frames",count=2);send("input.set_key",key=code,down=False)
        ui=sample("histories-enabled")
        capture("warm-capture-markers")
        ui=sample("capture-markers-published")
        for layout in ("Canvas","Editor"):
            if ui["layout"]!=layout: click(ui["window"][0]-110,20);ui=sample(layout)
            if not ui["toolsVisible"]:click(ui["window"][0]-30,20);ui=sample("open")
            if layout == "Editor":
                ui=tab(ui,10)
                # An open camera popup consumes a Details click without opening Profiler.
                x,y,w,h=ui["cameraPopupBounds"]
                click(x+w/2,20);ui=sample("details-dismiss-popup-open")
                assert ui["cameraPopupOpen"]
                click(ui["window"][0]/2-44,ui["window"][1]-140+15)
                ui=sample("details-dismiss-popup-only")
                assert not ui["cameraPopupOpen"] and ui["activeTool"]==10 and ui["toolsVisible"]
            ui=tab(ui,0);ui=scroll(ui,12000)
            x,y,w,h=ui["toolsContentBounds"];original=ui["workerThreads"]
            for px,expected in ((x,0),(x+w-1,ui["maxWorkerThreads"])):
                click(px,y+69);ui=sample(layout+"-workers-"+str(expected));assert ui["workerThreads"]==expected,ui
            click(x+w*.37,y+69);ui=sample(layout+"-workers-interior")
            assert 0<ui["workerThreads"]<ui["maxWorkerThreads"]
            restore=ui["workerThreads"]
            click(x+70,y+23);ui=sample("workers-off");assert ui["workerThreads"]==0
            click(x+70,y+23);ui=sample("workers-restore");assert ui["workerThreads"]==restore
            before=ui["profilerExpansionHash"]
            click(x+25,y+143);ui=sample(layout+"-marker-fold");assert ui["profilerExpansionHash"]!=before
            # The hierarchy is dynamic; use the same published bounds as drawing.
            for attempt in range(100):
                x,y,w,h=ui["toolsContentBounds"]
                px,py,pw,ph=ui["profilerDrawExpanderBounds"]
                assert pw>0,ui
                if y+128 <= py and py+ph <= y+h: break
                ui=scroll(ui,-30 if py+ph>y+h else 30)
            assert y+128<=py and py+ph<=y+h,(py,ui)
            draw_hash=ui["drawExpansionHash"]
            click(px+pw/2,py+ph/2);ui=sample(layout+"-draw-fold")
            assert ui["drawExpansionHash"]!=draw_hash,ui
            capture(layout+"-draw-hierarchy")
            retained=(ui["profilerExpansionHash"],ui["drawExpansionHash"])
            click(ui["window"][0]-30,20);ui=sample("closed")
            click(ui["window"][0]-30,20);ui=sample("reopened")
            assert (ui["profilerExpansionHash"],ui["drawExpansionHash"])==retained
            ui=scroll(ui,12000)
            click(x+25,y+143);ui=sample("marker-restored")
            ui=tab(ui,10);ui=scroll(ui,12000)
            x,y,w,h=ui["toolsContentBounds"]
            bw=(w-40)/3
            for i,(seconds,budget) in enumerate(((60,256),(45,128),(20,64))):
                click(x+14+i*(bw+6)+bw/2,y+47);ui=sample(layout+"-preset-"+str(i))
                assert (ui["replayMemoryPreset"],ui["replayRetentionSeconds"],ui["replayBudgetMiB"])==(i,seconds,budget)
            for row,key,low,high in ((87,"replayRetentionSeconds",20,600),(125,"replayBudgetMiB",32,512)):
                capture(layout+"-before-"+key)
                for px,value in ((x+132,low),(x+w-80,high)):
                    click(px,y+row);ui=sample(layout+"-"+key+"-"+str(value));assert ui[key]==value,ui
                click(x+w*.37,y+row);ui=sample(layout+"-"+key+"-interior");assert low<ui[key]<high
            capture(layout+"-memory-policy")
            ui=scroll(ui,-12000);capture(layout+"-memory-tables")
            assert ui["memorySamples"]>0
        print("PASS: native worker endpoints/interior/restore, marker and draw hierarchy retention, all Memory presets and sliders in both layouts")
    finally:
        try:send("session.stop")
        finally:c.close()
if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("--session",type=Path,required=True);run(p.parse_args().session)
