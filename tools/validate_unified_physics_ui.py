"""Verify native Physics dock toggles, sliders and pipeline navigation."""
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
        x, y, w, h = ui['physicsControlsBounds']
        desired = max(0, row - h / 2)
        delta = round((ui['physicsScroll'] - desired) * 120 / 34)
        if delta:
            send('input.pointer_wheel', x=int(x+60), y=int(y+h/2), wheelDelta=delta)
            ui = sample()
        py = y + row - ui['physicsScroll'] + 12
        assert y <= py < y+h
        return ui, py
    metadata = (REPO / 'SkullbonezSource/Runtime/Interaction/OperatorEditorExchange.h').read_text()
    constants = {name: float(value) for name, value in re.findall(r'constexpr float (UI_\w+) = ([-\d.]+)f;', metadata)}
    sliders = [('PHYSICS_ALPHA',1,374), ('CONTACT_LINGER',1,418), ('RAY_IMPULSE',0,312),
               ('LAUNCHER_PROJECTILE_SPEED',0,356), ('WORLD_GRAVITY',0,268), ('FRICTION_COEFF',0,482),
               ('FRICTION_COEFF',0,526), ('ROLLING_FRICTION_COEFF',0,570), ('TORNADO_RADIUS',0,1080),
               ('TORNADO_HEIGHT',0,1124), ('TORNADO_INWARD',0,1168), ('TORNADO_SWIRL',0,1212), ('TORNADO_LIFT',0,1256)]
    positions = [(1,20),(1,50),(1,80),(1,110),(1,140),(1,170),(0,614),(1,200),(1,230),(0,1046),(1,260),(1,290),(1,320)]
    groups_open=False
    def section(ui, index):
        if ui['physicsSection'] != index:
            x,y,w,h = ui['physicsHeaderBounds']
            click(x+(index%2+.5)*w/2, y+66+27*(index//2))
            ui=sample()
        assert ui['physicsSection']==index
        return ui
    try:
        assert 'input.pointer_drag' in send('capabilities.get')['commands']
        send('state.subscribe', topics=[], detail='normal')
        ui = sample()
        for layout in ('Canvas', 'Editor'):
            if ui['layout'] != layout:
                middle(ui['headerLayoutBounds'])
                ui = sample()
            middle(ui['headerPhysicsBounds'])
            ui=sample()
            assert ui['physicsPeer'] and ui['causeControlsBounds'][2]==0
            x,y,w,h = ui['physicsControlsBounds']
            if not groups_open:
                ui=section(ui,0)
                ui,py=reveal(ui,444);click(x+20,py);ui=sample()
                ui,py=reveal(ui,1012);click(x+20,py);ui=sample()
                groups_open=True
            for index,(tab,row) in enumerate(positions):
                ui = section(ui,tab)
                ui,py = reveal(ui,row)
                px = x+30
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
            ui=section(ui,1)
            ui,py=reveal(ui,466)
            stage=ui['physicsPipelineStage']
            count=ui['physicsPipelineStages']
            click(x+w-13,py,70)
            ui=sample()
            assert ui['physicsPipelineStage']==(stage+1)%count
            click(x+w-45,py)
            ui=sample()
            assert ui['physicsPipelineStage']==stage
            checks.append(dict(layout=layout,pipelineNext=True,pipelinePrevious=True))
            for index,(name,tab,row) in enumerate(sliders):
                ui=section(ui,tab)
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
            ui=section(ui,0)
            ranges=[(1,32),(0,.1),(0,1),(0,1),(0,.1),(0,1),(0,20),(0,2),(0,20),(0,5),(0,5),(1,600),(0,1)]
            for index,(lo,hi) in enumerate(ranges):
                row=24+index*44 if index<4 else 200 if index==12 else 650+(index-4)*44
                ui,py=reveal(ui,row)
                step=1 if index in (0,11,12) else .001
                for fraction in (0,1,.37):
                    px=int(x+1 if fraction==0 else x+w-1 if fraction==1 else x+118+(w-190)*fraction)
                    t=min(1,max(0,(px-x-118)/max(80,w-190)))
                    expected=min(hi,max(lo,lo+math.floor((hi-lo)*t/step+.5)*step))
                    click(px,py)
                    ui=sample()
                    actual=ui['physicsSettings'][index]
                    assert math.isclose(actual,expected,rel_tol=2e-5,abs_tol=max(1e-6,step*.01)),(layout,'solver',index,fraction,expected,actual)
                    checks.append(dict(layout=layout,solverSetting=index,expected=expected,actual=actual))
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
