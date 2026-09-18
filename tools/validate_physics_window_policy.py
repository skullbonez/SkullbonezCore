"""Native Physics window exact tick and explicit settings persistence acceptance."""
from __future__ import annotations
import argparse
import json
import math
import os
import re
import time
from pathlib import Path
from skarness import SkarnessConnection, launch
ROOT=Path(__file__).resolve().parents[1]

def run(session: Path):
    assert not session.exists()
    session.mkdir(parents=True)
    defaults=session/'physics-defaults.cfg'
    original='format_version = 8\n# native preservation\nowner_unknown = keep\nscreen_x = 1234\npersistent_contact_solver_iterations = 12\n'
    defaults.write_text(original)
    key='SKULLBONEZ_PHYSICS_DEFAULTS_FILE'
    previous=os.environ.get(key)
    os.environ[key]=str(defaults)
    try:
        assert launch(session,ROOT/'Automation/SKULLBONEZ_CORE.exe',ROOT/'SkullbonezData/scenes/prediction_ragdoll_wall_200.scene.json',hidden=True,fixed_step=True,allocation_guard='gameplay')==0
    finally:
        if previous is None: os.environ.pop(key,None)
        else: os.environ[key]=previous
    connection=SkarnessConnection(session)
    latest={};offset=0;checks=[]
    def send(command,**args):
        result=connection.wait(connection.send(command,args))
        with (session/'commands.ndjson').open('a') as f:f.write(json.dumps(dict(command=command,args=args,result=result))+'\n')
        assert result.get('status')=='applied',result
        return result.get('result',result)
    def state():
        nonlocal offset
        send('run.step_frames',count=3)
        with (session/'runtime.skarness.ndjson').open() as f:
            f.seek(offset)
            for line in f:
                event=json.loads(line)
                if 'topic' in event:latest[event['topic']]=event['payload']
            offset=f.tell()
        return latest['ui.presentation']
    def click(x,y,hold=0):
        send('input.pointer_position',enabled=True,x=round(x),y=round(y))
        state();time.sleep(.22);state()
        send('input.pointer_drag',button='left',x=round(x),y=round(y),deltaX=0,deltaY=0,holdMilliseconds=hold)
    def center(bounds):
        x,y,w,h=bounds
        click(x+w/2,y+h/2)
    def reveal(row):
        ui=state();x,y,w,h=ui['physicsControlsBounds']
        desired=max(0,row-h/2)
        send('input.pointer_wheel',x=round(x+30),y=round(y+h/2),wheelDelta=round((ui['physicsScroll']-desired)*120/34))
        ui=state()
        return x,y+row-ui['physicsScroll']+12,w
    try:
        assert 'replay.set_prediction_enabled' in send('capabilities.get')['commands']
        send('run.pause');send('state.subscribe',topics=[],detail='normal')
        send('run.step',count=2)
        ui=state();center(ui['headerPhysicsBounds']);ui=state()
        x,y,w,h=ui['physicsHeaderBounds']
        if ui['physicsSection']!=0:click(x+w/4,y+66)
        px,py,pw=reveal(24)
        send('input.pointer_position',enabled=True,x=round(px+pw-1),y=round(py))
        state();time.sleep(.22);state()
        send('replay.set_prediction_detail',highDetail=True)
        send('replay.set_prediction_horizon',seconds=120)
        send('prediction.select_target',name='prediction_striker_ball')
        send('replay.set_prediction_enabled',enabled=True)
        ui=state();old=dict(latest['replay.prediction.controls'])
        assert old['building'],old
        send('input.pointer_drag',button='left',x=round(px+pw-1),y=round(py),deltaX=0,deltaY=0)
        ui=state();assert ui['physicsSettings'][0]==32,ui['physicsSettings']
        changed=dict(latest['replay.prediction.controls'])
        assert not changed['complete'] or changed['generation']!=old['generation'],(old,changed)
        send('replay.set_prediction_enabled',enabled=False)
        checks.append(dict(editDuringWorker=True,before=old,after=changed))
        send('run.step',count=120)
        ui=state();recorded=list(ui['physicsSettings'])
        recording=session/'policy.skreplay';send('replay.save',path=str(recording))
        px,py,pw=reveal(522);click(px+20,py)
        ui=state();assert ui['physicsSettings'][0]!=32,ui['physicsSettings']
        send('replay.load',path=str(recording))
        send('replay.seek_frame',frame=59)
        send('replay.restore_branch')
        ui=state();assert ui['physicsSettings']==recorded,(recorded,ui['physicsSettings'])
        send('run.step',count=1)
        ui=state();assert ui['physicsSettings']==recorded
        checks.append(dict(loadedRestoreOwnsSettings=True,settings=recorded))
        send('capture.screenshot',path=str(session/'restored-policy.png'))
    finally:
        (session/'checks.json').write_text(json.dumps(checks,indent=2)+'\n')
        try:send('session.stop')
        finally:
            connection.close()
    # Shutdown writes the allocation guard result after acknowledging stop.
    for _ in range(100):
        log=(session/'process.stdout.log').read_text(errors='replace')
        if '[allocation-guard] mode=' in log:break
        time.sleep(.05)
    assert 'gameplay_violations=0' in log,log[-8000:]
    print('PASS: active prediction edit joins safely; saved replay restores its own Physics policy')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--session',type=Path,required=True)
    run(p.parse_args().session.resolve())
