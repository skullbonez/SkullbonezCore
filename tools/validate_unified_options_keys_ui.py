"""Exercise Options and Keys through native UI with a bounded generated scene."""
from __future__ import annotations
import argparse
import json
import time
import math
from pathlib import Path
import re
from skarness import SkarnessConnection, launch
REPO=Path(__file__).resolve().parents[1]


def run(session: Path) -> None:
    assert launch(session,REPO/'Automation/SKULLBONEZ_CORE.exe',None,hidden=True,model_capacity=512)==0
    connection=SkarnessConnection(session)
    latest,checks={},[]
    offset=0
    def send(command: str,**args: object) -> dict:
        result=connection.wait(connection.send(command,args))
        assert result.get('status')=='applied',result
        return result
    def sample() -> dict:
        nonlocal offset
        deadline = time.monotonic() + 0.25
        while time.monotonic() < deadline:
            send("run.step_frames", count=3)
        send('run.step_frames',count=2)
        with (session/'runtime.skarness.ndjson').open() as stream:
            stream.seek(offset)
            for line in stream:
                event=json.loads(line)
                if 'topic' in event: latest[event['topic']]=event['payload']
            offset=stream.tell()
        return latest['ui.presentation']
    def click(x: float,y: float,hold: int=0) -> None:
        send("input.pointer_position", enabled=True, x=int(x), y=int(y))
        send("run.step_frames", count=2)
        time.sleep(0.22)
        send("run.step_frames", count=2)
        send('input.pointer_drag',button='left',x=int(x),y=int(y),deltaX=0,deltaY=0,holdMilliseconds=hold)
    def middle(bounds: list) -> None:
        x, y, width, height = bounds
        assert width > 0 and height > 0, bounds
        click(x + width / 2, y + height / 2)
    def tab(ui: dict,index: int) -> dict:
        if not ui['toolsVisible']:
            middle(ui['replayDetailsBounds'])
            ui=sample()
        top=ui['viewport'][1]+ui['viewport'][3]+(28 if ui['layout']=='Editor' else 0)
        click(14+(ui['window'][0]-28)*(index+.5)/11,top+66)
        ui=sample()
        assert ui['activeTool']==index
        return ui
    def reveal(ui: dict,row: float) -> tuple[dict,float]:
        x,y,w,h=ui['toolsContentBounds']
        desired=max(0,row-h/2)
        delta=round((ui['toolsScroll']-desired)*120/42)
        if delta:
            send('input.pointer_wheel',x=int(x+60),y=int(y+h/2),wheelDelta=delta)
            ui=sample()
        py=y+row-ui['toolsScroll']+12
        assert y<=py<y+h
        return ui,py
    def slider(ui: dict,index: int,row: float,name: str,lo: float,hi: float,step: float,value_index: int) -> dict:
        for fraction in (0,1,.37):
            ui=tab(ui,index)
            ui,py=reveal(ui,row)
            x,y,w,h=ui['toolsContentBounds']
            px=int(x+1 if fraction==0 else x+w-1 if fraction==1 else x+118+(w-190)*fraction)
            t=min(1,max(0,(px-x-118)/max(80,w-190)))
            expected=min(hi,max(lo,lo+math.floor((hi-lo)*t/step+.5)*step))
            click(px,py)
            ui=sample()
            actual=latest['scene.state']['timeScale'] if value_index<0 else ui['sceneControlValues'][value_index]
            assert math.isclose(actual,expected,rel_tol=2e-5,abs_tol=max(1e-6,step*.01)),(ui['layout'],name,fraction,expected,actual)
            checks.append(dict(layout=ui['layout'],control=name,fraction=fraction,expected=expected,actual=actual))
        print('PASS '+ui['layout']+'/'+name,flush=True)
        return ui
    try:
        assert 'input.pointer_drag' in send('capabilities.get')['commands']
        send('state.subscribe',topics=[],detail='normal')
        ui=sample()
        assert ui['modelCapacity']==512
        for layout in ('Editor',):
            if ui['layout']!=layout:
                middle(ui['headerLayoutBounds'])
                ui=sample()
            ui=tab(ui,4)
            x,y,w,h=ui['toolsContentBounds']
            for index in range(6):
                ui,py=reveal(ui,42+(index//2)*30)
                px=x+(max(148,w*.46)+18 if index%2 else 0)+30
                before=list(ui['optionsToggles'])
                click(px,py,70)
                ui=sample()
                expected=list(before)
                expected[index]=not expected[index]
                assert ui['optionsToggles']==expected,(layout,index,before,ui['optionsToggles'])
                click(px,py)
                ui=sample()
                assert ui['optionsToggles']==before,(layout,index,before,ui['optionsToggles'])
                checks.append(dict(layout=layout,toggle=index,heldClick=True,restored=True))
            ui=slider(ui,4,168,'timeScale',.1,10,.05,-1)
            ui=slider(ui,4,216,'modelCount',0,512,1,5)
            ui=slider(ui,7,42,'seed',1,999999,1,0)
            # Each count has the remaining-capacity limit from the other count.
            ui=slider(ui,7,130,'balls',0,512-int(ui['sceneControlValues'][2]),1,1)
            ui=slider(ui,7,170,'boxes',0,512-int(ui['sceneControlValues'][1]),1,2)
            ui=slider(ui,7,252,'fluidHeight',-100,200,1,3)
            ui=slider(ui,7,292,'fluidDensity',0,5,.05,4)
            send('capture.screenshot',path=str((session/(layout+'-keys.png')).resolve()))
        print(f'PASS: {len(checks)} Options/Keys checks',flush=True)
    finally:
        (session/'options-keys-checks.json').write_text(json.dumps(checks,indent=2))
        try: send('session.stop')
        finally: connection.close()

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session',type=Path,default=REPO/'TestOutput/skarness/unified-options-keys')
    run(parser.parse_args().session)
