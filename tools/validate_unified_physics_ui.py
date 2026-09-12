"""Verify native Physics tab toggles, sliders and pipeline navigation."""
from __future__ import annotations
import argparse
import json
import time
import math
from pathlib import Path
import re
from skarness import SkarnessConnection, launch
REPO = Path(__file__).resolve().parents[1]


def run(session: Path) -> None:
    assert launch(session, REPO / 'Automation/SKULLBONEZ_CORE.exe',
                  REPO / 'SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json', hidden=True) == 0
    connection = SkarnessConnection(session)
    latest, checks = {}, []
    offset = 0
    def send(command: str, **args: object) -> dict:
        result = connection.wait(connection.send(command, args))
        assert result.get('status') == 'applied', result
        return result
    def sample() -> dict:
        nonlocal offset
        deadline = time.monotonic() + 0.25
        while time.monotonic() < deadline:
            send("run.step_frames", count=3)
        send('run.step_frames', count=2)
        with (session / 'runtime.skarness.ndjson').open() as stream:
            stream.seek(offset)
            for line in stream:
                event = json.loads(line)
                if 'topic' in event:
                    latest[event['topic']] = event['payload']
            offset = stream.tell()
        return latest['ui.presentation']
    def click(x: float, y: float, hold: int = 0) -> None:
        send("input.pointer_position", enabled=True, x=int(x), y=int(y))
        send("run.step_frames", count=2)
        time.sleep(0.22)
        send("run.step_frames", count=2)
        send('input.pointer_drag', button='left', x=int(x), y=int(y), deltaX=0, deltaY=0, holdMilliseconds=hold)
    def middle(bounds: list) -> None:
        x, y, width, height = bounds
        assert width > 0 and height > 0, bounds
        click(x + width / 2, y + height / 2)
    def reveal(ui: dict, row: float) -> tuple[dict, float]:
        x, y, w, h = ui['toolsContentBounds']
        desired = max(0, row - h / 2)
        delta = round((ui['toolsScroll'] - desired) * 120 / 42)
        if delta:
            send('input.pointer_wheel', x=int(x+60), y=int(y+h/2), wheelDelta=delta)
            ui = sample()
        py = y + row - ui['toolsScroll'] + 12
        assert y <= py < y+h
        return ui, py
    metadata = (REPO / 'SkullbonezSource/Runtime/Interaction/OperatorEditorExchange.h').read_text()
    constants = {name: float(value) for name, value in re.findall(r'constexpr float (UI_\w+) = ([-\d.]+)f;', metadata)}
    sliders = [('PHYSICS_ALPHA',302), ('CONTACT_LINGER',350), ('RAY_IMPULSE',414),
               ('LAUNCHER_PROJECTILE_SPEED',454), ('WORLD_GRAVITY',526), ('FRICTION_COEFF',612),
               ('FRICTION_COEFF',652), ('ROLLING_FRICTION_COEFF',692), ('TORNADO_RADIUS',778),
               ('TORNADO_HEIGHT',818), ('TORNADO_INWARD',858), ('TORNADO_SWIRL',898), ('TORNADO_LIFT',938)]
    positions = [(0,0),(0,1),(1,1),(2,1),(1,0),(2,0),(3,1),(3,0),(4,0),(4,1),(5,0),(5,1),(6,1)]
    try:
        assert 'input.pointer_drag' in send('capabilities.get')['commands']
        send('state.subscribe', topics=[], detail='normal')
        ui = sample()
        for layout in ('Editor',):
            if ui['layout'] != layout:
                middle(ui['headerLayoutBounds'])
                ui = sample()
            if not ui['toolsVisible']:
                middle(ui['replayDetailsBounds'])
                ui = sample()
            top = ui['viewport'][1]+ui['viewport'][3]+(28 if layout=='Editor' else 0)
            click(14+(ui['window'][0]-28)*3.5/11,top+66)
            ui = sample()
            assert ui['activeTool']==3
            x,y,w,h = ui['toolsContentBounds']
            for index,(row,column) in enumerate(positions):
                ui,py = reveal(ui,42+row*30)
                px = x+(max(148,w*.46)+18 if column else 0)+30
                before = list(ui['physicsToggles'])
                click(px,py,70)
                ui=sample()
                expected=list(before)
                expected[index]=not expected[index]
                if index==9:
                    # Existing auto-visual policy makes the shell follow the field.
                    expected[10]=expected[9]
                assert ui['physicsToggles']==expected,(layout,index,before,ui['physicsToggles'])
                click(px,py)
                ui=sample()
                restored=list(before)
                if index==9:
                    restored[10]=restored[9]
                assert ui['physicsToggles']==restored,(layout,index,restored,ui['physicsToggles'])
                checks.append(dict(layout=layout,toggle=index,heldClick=True,restored=True))
            ui,py=reveal(ui,254)
            stage=ui['physicsPipelineStage']
            count=ui['physicsPipelineStages']
            click(x+w-13,py,70)
            ui=sample()
            assert ui['physicsPipelineStage']==(stage+1)%count
            click(x+w-45,py)
            ui=sample()
            assert ui['physicsPipelineStage']==stage
            checks.append(dict(layout=layout,pipelineNext=True,pipelinePrevious=True))
            for index,(name,row) in enumerate(sliders):
                ui,py=reveal(ui,row)
                lo,hi,step=(constants['UI_'+name+'_'+suffix] for suffix in ('MIN','MAX','STEP'))
                for fraction in (0,1,.37):
                    px=int(x+1 if fraction==0 else x+w-1 if fraction==1 else x+118+(w-190)*fraction)
                    t=min(1,max(0,(px-x-118)/max(80,w-190)))
                    expected=min(hi,max(lo,lo+math.floor((hi-lo)*t/step+.5)*step))
                    if name=='WORLD_GRAVITY':
                        expected=-expected
                    click(px,py)
                    ui=sample()
                    actual=ui['physicsParameters'][index]
                    assert math.isclose(actual,expected,rel_tol=2e-5,abs_tol=max(1e-6,step*.01)),(layout,name,index,fraction,expected,actual)
                    checks.append(dict(layout=layout,parameter=name,index=index,fraction=fraction,expected=expected,actual=actual))
                print(f'PASS {layout}/{name}/{index}',flush=True)
            send('capture.screenshot',path=str((session/(layout+'-physics.png')).resolve()))
        print(f'PASS: {len(checks)} Physics toggle and slider checks',flush=True)
    finally:
        (session/'physics-checks.json').write_text(json.dumps(checks,indent=2))
        try:
            send('session.stop')
        finally:
            connection.close()

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session',type=Path,default=REPO/'TestOutput/skarness/unified-physics-ui')
    run(parser.parse_args().session)
