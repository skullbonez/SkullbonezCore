"""Exercise editor initial velocities and frame-zero locking through Skarness input."""
from pathlib import Path
import argparse
import json
import math
import time
from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]

def run(session):
    session.mkdir(parents=True, exist_ok=True)
    scene = session / 'velocity.scene.json'
    seed = json.loads((REPO/'SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json').read_text())
    seed['editor'] = {'editableScene': True}
    seed['playback'] = {'frames':'unlimited','fixedStep':True}
    body = dict(seed['objects'][0])
    body.update(name='edited_box', position=[500,80,500], velocity=[6,0,0], angularVelocity=[0,0,0])
    fixed = dict(body)
    fixed.update(name='fixed_box', position=[540,80,500], fixed=True, velocity=[0,0,0])
    seed['objects'] = [body, fixed]
    seed['cameras'][0].update(position=[570,150,670], view=[500,80,500])
    scene.write_text(json.dumps(seed))
    assert launch(session,REPO/'Automation/SKULLBONEZ_CORE.exe',scene,hidden=True,fixed_step=True,allocation_guard='gameplay',layout_file=session/'layout.preferences') == 0
    connection = SkarnessConnection(session)
    latest = {}
    offset = 0
    def send(command, **args):
        reply = connection.wait(connection.send(command,args))
        assert reply.get('status') == 'applied', (command,reply)
        return reply
    def state(label):
        nonlocal offset
        send('run.step_frames',count=3)
        with (session/'runtime.skarness.ndjson').open() as f:
            f.seek(offset)
            for line in f:
                event=json.loads(line)
                if 'topic' in event: latest[event['topic']]=event['payload']
            offset=f.tell()
        (session/f'{label}.json').write_text(json.dumps(latest,indent=2))
        return latest['ui.presentation']
    def click(x,y):
        send('input.pointer_position',enabled=True,x=round(x),y=round(y))
        send('run.step_frames',count=2)
        time.sleep(.22)
        send('input.pointer_drag',button='left',x=round(x),y=round(y),deltaX=0,deltaY=0,moveClient=True)
    def middle(bounds):
        x,y,w,h=bounds; assert w>0 and h>0,bounds
        click(x+w/2,y+h/2)
    def row(ui,y):
        x,top,w,h=ui['editorControlsBounds']; click(x+30,top+y)
    def resolve(name='edited_box'):
        return send('scene.object.resolve',name=name)['result']['objects'][0]
    def frame():
        return latest['frame.clocks']['sceneFrame']
    def assert_authored_motion(expected):
        actual=resolve()
        # Restart clears solver diagnostics; compare authored state and identity.
        for key in ('sceneObjectId','name','position','linearVelocity','angularVelocity','fixed','sleeping'):
            assert actual[key]==expected[key],(key,actual,expected)
    def project(point,ui):
        camera=latest['replay.state']
        eye,center,up=(camera[k] for k in ('cameraPrimaryEye','cameraPrimaryView','cameraPrimaryUp'))
        def sub(a,b): return [x-y for x,y in zip(a,b)]
        def dot(a,b): return sum(x*y for x,y in zip(a,b))
        def unit(a): return [x/math.sqrt(dot(a,a)) for x in a]
        def cross(a,b): return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
        forward=unit(sub(center,eye)); right=unit(cross(forward,up)); vertical=cross(right,forward)
        vx,vy,vw,vh=ui['viewport']; d=sub(point,eye)
        scale=vh/2/math.tan(math.radians(45)/2)/dot(d,forward)
        return [vx+vw/2+dot(d,right)*scale,vy+vh/2-dot(d,vertical)*scale]
    try:
        catalog=send('capabilities.get'); (session/'capabilities.json').write_text(json.dumps(catalog,indent=2))
        assert {'scene.object.select','input.pointer_drag','scene.save'} <= set(catalog['commands'])
        send('state.subscribe',topics=[],detail='normal')
        send('run.step',count=60)
        ui=state('running')
        assert resolve()['position'][0]>500
        if ui['layout']!='Editor': middle(ui['headerLayoutBounds']); ui=state('layout')
        if not ui['editorControlsBounds'][2]: middle(ui['editorTabBounds']); ui=state('panel')
        row(ui,54); ui=state('frame-zero')
        assert ui['editorMode'] and frame()==0, latest.keys()
        assert resolve()['position']==[500,80,500] and resolve()['linearVelocity']==[6,0,0],resolve()
        row(ui,416); ui=state('velocity-enabled')
        assert ui['editorVelocityEdit'] and not ui['editorPlacement'],ui
        send('scene.object.select',scope='editor',name='edited_box')
        ui=state('selected')
        send('capture.screenshot',path=str(session/'selected.png'))
        for angular in (False,True):
            if angular: row(ui,450); ui=state('angular')
            for axis in range(3):
                before=resolve(); field='angularVelocity' if angular else 'linearVelocity'
                tip=[p+v*36/(5 if angular else 140) for p,v in zip(before['position'],before[field])]
                end=tip.copy(); end[axis]+=math.sqrt(108)+7
                a,b=project(tip,ui),project(end,ui)
                delta=[y-x for x,y in zip(a,b)]; size=math.hypot(*delta)
                xy=[round(x+d*.75) for x,d in zip(a,delta)]
                send('input.pointer_position',enabled=True,x=xy[0],y=xy[1])
                state('hover')
                send('input.pointer_drag',button='left',x=xy[0],y=xy[1],deltaX=round(delta[0]/size*16),deltaY=round(delta[1]/size*16),moveClient=True)
                ui=state(f'drag-{angular}-{axis}')
                after=resolve()
                assert abs(after[field][axis]-before[field][axis])>.01,(axis,before,after)
                assert after['position']==before['position'] and frame()==0,(after,frame())
                for other in range(3):
                    if other!=axis: assert after[field][other]==before[field][other],after
        authored=resolve()
        assert ui['editorSelectedObjectId']==authored['sceneObjectId']
        # Undo and redo the last completed velocity gesture through real keys.
        for key in (90,89):
            send('input.set_key',key=17,down=True)
            send('input.set_key',key=key,down=True)
            state(f'history-key-{key}')
            send('input.set_key',key=key,down=False)
            send('input.set_key',key=17,down=False)
            ui=state(f'history-release-{key}')
            if key==90: assert resolve()['angularVelocity']!=authored['angularVelocity']
        assert resolve()==authored
        # A new unsaved placement must survive later Edit/Run transitions.
        row(ui,208); ui=state('place-mode')
        vx,vy,vw,vh=ui['viewport']
        click(vx+vw*.25,vy+vh*.8)
        ui=state('placed-unsaved')
        placed_names=[o['name'] for o in send('scene.object.list')['result']['objects'] if o['name'] not in ('edited_box','fixed_box')]
        assert len(placed_names)==1,placed_names
        placed_name=placed_names[0]
        placed=resolve(placed_name)
        row(ui,54); ui=state('exit-with-unsaved-placement')
        send('run.step',count=10); ui=state('run-with-unsaved-placement')
        row(ui,54); ui=state('restore-unsaved-placement')
        assert frame()==0 and resolve(placed_name)['position']==placed['position']
        assert resolve()['linearVelocity']==authored['linearVelocity']
        row(ui,416); ui=state('velocity-mode-again')
        send('scene.object.select',scope='editor',name='edited_box')
        ui=state('selected-original-again')
        send('scene.object.select',scope='editor',name='fixed_box')
        ui=state('fixed-selected')
        fixed_before=resolve('fixed_box')
        a=project([553,80,500],ui)
        send('input.pointer_drag',button='left',x=round(a[0]),y=round(a[1]),deltaX=20,deltaY=0,moveClient=True)
        ui=state('fixed-drag')
        assert resolve('fixed_box')==fixed_before and ui['editorGestureAxis']==-1
        send('scene.object.select',scope='editor',name='edited_box')
        ui=state('selected-again')
        send('run.step',count=20); ui=state('step-blocked')
        assert frame()==0
        assert_authored_motion(authored)
        send('replay.scrub',normalized=1); ui=state('scrub-blocked')
        assert frame()==0
        assert_authored_motion(authored)
        send('capture.screenshot',path=str(session/'velocity-editor.png'))
        send('scene.save'); state('saved')
        saved=json.loads(scene.read_text()); saved_body=next(o for o in saved['objects'] if o['name']=='edited_box')
        assert saved_body['velocity']==authored['linearVelocity'],saved_body
        assert saved_body['angularVelocity']==authored['angularVelocity'],saved_body
        row(ui,54); ui=state('exit-editor')
        send('run.step',count=10); ui=state('run-modified')
        assert resolve()['position']!=authored['position']
        row(ui,54); ui=state('reenter-editor')
        assert frame()==0 and resolve()['position']==authored['position']
        assert resolve(placed_name)['position']==placed['position']
        assert resolve()['linearVelocity']==authored['linearVelocity'] and resolve()['angularVelocity']==authored['angularVelocity']
        send('scene.reset'); ui=state('reload')
        assert resolve()['linearVelocity']==authored['linearVelocity']
        # Edit also cancels an active replay experiment and restores the authored seed.
        if ui['editorMode']: row(ui,54); ui=state('exit-for-replay')
        send('run.step',count=10); ui=state('future-for-replay')
        send('prediction.select_target',name='edited_box')
        send('replay.set_prediction_horizon',seconds=1)
        send('replay.set_velocity_edit_enabled',enabled=True)
        send('replay.velocity_preview',linear=[80,0,0],angular=[0,0,0])
        send('replay.velocity_commit')
        ui=state('experiment-before-editor')
        assert latest['replay.state']['divergence']['active']
        row(ui,54); ui=state('editor-cancels-experiment')
        assert ui['editorMode'] and frame()==0
        assert not latest['replay.state']['divergence']['active']
        assert resolve()['position']==authored['position']
        assert resolve()['linearVelocity']==authored['linearVelocity']
        result={'passed':True,'authored':authored,'saved':saved_body}
    finally:
        try:
            send('session.stop')
        finally:
            connection.close()
    output=''
    for _ in range(40):
        output=(session/'process.stdout.log').read_text(errors='replace')
        if '[allocation-guard] PASS:' in output or '[allocation-guard] FAIL:' in output: break
        time.sleep(.05)
    assert '[allocation-guard] PASS:' in output,output[-2000:]
    report=(REPO/'dx12_validation.txt').read_text()
    (session/'dx12_validation.txt').write_text(report)
    assert report.strip().splitlines()[-1]=='0',report
    (session/'result.json').write_text(json.dumps(result,indent=2))
    print('PASS: editor velocity handles, frame-zero lock, authored restart and save/reload')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session',type=Path,default=REPO/'TestOutput/skarness/editor-velocity')
    run(parser.parse_args().session.resolve())
