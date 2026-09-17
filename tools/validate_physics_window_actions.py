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
    mass_scene=ROOT/'SkullbonezData/scenes'/f'physics_window_validation_{session.name}.scene.json'
    assert not mass_scene.exists()
    mass_scene.write_bytes((ROOT/'SkullbonezData/scenes/grass_interaction.scene.json').read_bytes())
    defaults=session/'physics-defaults.cfg'
    original='format_version = 8\n# native preservation\nowner_unknown = keep\nscreen_x = 1234\npersistent_contact_solver_iterations = 12\n'
    defaults.write_text(original)
    key='SKULLBONEZ_PHYSICS_DEFAULTS_FILE'
    previous=os.environ.get(key)
    os.environ[key]=str(defaults)
    try:
        assert launch(session,ROOT/'Automation/SKULLBONEZ_CORE.exe',ROOT/'SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json',hidden=True,fixed_step=True,allocation_guard='gameplay')==0
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
        assert 'input.pointer_drag' in send('capabilities.get')['commands']
        send('run.pause');send('state.subscribe',topics=[],detail='normal')
        ui=state();center(ui['headerPhysicsBounds']);ui=state()
        assert ui['physicsPeer']
        x,y,w,h=ui['physicsHeaderBounds']
        # The global pause lock owns the native button; harness pause keeps
        # observation frames from advancing independently during the check.
        if not latest['scene.state']['pauseLocked']:click(x+w/6,y+35)
        ui=state();assert latest['scene.state']['pauseLocked']
        for hold in (0,250,700):
            before=ui['physicsCompletedSteps']
            click(x+w/2,y+35,hold)
            ui=state()
            assert ui['physicsCompletedSteps']==before+1,(before,ui['physicsCompletedSteps'])
            send('run.step_frames',count=10)
            assert state()['physicsCompletedSteps']==before+1
            checks.append(dict(singleTick=True,heldMilliseconds=hold,before=before,after=before+1))
        if ui['physicsSection']!=0:click(x+w/4,y+66)
        ui=state();startup=list(ui['physicsSettings']);scene_gravity=ui['physicsParameters'][4]
        startup_gravity=float(re.search(r'^gravity\s*=\s*([^\s#]+)',(ROOT/'SkullbonezData/engine.cfg').read_text(),re.M)[1])
        for scale,track in ((.1,0),(10,1)):
            px,py,pw=reveal(400);click(px+118+max(80,pw-190)*track,py)
            ui=state();assert math.isclose(latest['scene.state']['timeScale'],scale,abs_tol=.011),latest['scene.state']
            x,y,w,h=ui['physicsHeaderBounds'];before=ui['physicsCompletedSteps']
            click(x+w/2,y+35,700)
            ui=state();assert ui['physicsCompletedSteps']==before+1
            send('run.step_frames',count=10);assert state()['physicsCompletedSteps']==before+1
            checks.append(dict(singleTickAtScale=scale,heldMilliseconds=700))
        px,py,pw=reveal(268);click(px+pw-1,py)
        ui=state();assert ui['physicsParameters'][4]!=scene_gravity

        px,py,pw=reveal(24);click(px+pw-1,py)
        ui=state();assert ui['physicsSettings'][0]==32,ui['physicsSettings']
        assert defaults.read_text()==original
        px,py,pw=reveal(554);click(px+20,py)
        ui=state();saved=defaults.read_text()
        assert 'persistent_contact_solver_iterations = 32' in saved,saved
        assert 'gravity = -100' in saved,saved
        assert '# native preservation' in saved and 'owner_unknown = keep' in saved and 'screen_x = 1234' in saved
        px,py,pw=reveal(522);click(px+20,py)
        ui=state();assert ui['physicsSettings']==startup,(startup,ui['physicsSettings'])
        assert math.isclose(ui['physicsParameters'][4],startup_gravity,abs_tol=.001),(startup_gravity,ui['physicsParameters'])
        assert defaults.read_text()==saved
        checks.append(dict(explicitSave=True,restoreStartup=True,unrelatedPreserved=True))
        send('scene.load',path='catto_vertical_stack.scene.json')
        send('run.step',count=120)
        ui=state();x,y,w,h=ui['physicsHeaderBounds']
        click(x+3*w/4,y+66)
        ui=state();assert ui['physicsSection']==1
        overlay_bodies=send('scene.object.list')['objects']
        for index in range(10):
            px,py,pw=reveal(534+index*30)
            before=list(ui['physicsAdditionalLayers'])
            click(px+20,py,100)
            ui=state();expected=list(before);expected[index]=not expected[index]
            assert ui['physicsAdditionalLayers']==expected,(index,expected,ui['physicsAdditionalLayers'])
            counts=ui['physicsGeometryCounts']
            assert counts[0]<=73728 and counts[1]>=0,counts
            if index in (3,4,7,9): assert counts[0]>0,(index,counts)
            if index==8: assert counts[0]==0,counts
            if index in (1,2):assert counts[4]>0,counts
            assert send('scene.object.list')['objects']==overlay_bodies,'Display toggles changed authoritative bodies'
            checks.append(dict(overlay=index,geometry=counts))
            send('capture.screenshot',path=str(session/f'overlay-{index}.png'))
            click(px+20,py)
            ui=state();assert ui['physicsAdditionalLayers']==before
        send('scene.load',path='catto_bridge.scene.json')
        send('run.step',count=2)
        ui=state()
        px,py,pw=reveal(534+5*30);click(px+20,py)
        ui=state();assert ui['physicsGeometryCounts'][0]>0,ui['physicsGeometryCounts']
        checks.append(dict(jointGeometryPositive=True,geometry=ui['physicsGeometryCounts']))
        click(px+20,py)
        send('scene.load',path=mass_scene.name)
        send('scene.save')
        assert json.loads(mass_scene.read_text())['version']==5
        send('editor.set_enabled',enabled=True)
        send('scene.object.select',scope='editor',sceneObjectId=8101)
        ui=state();x,y,w,h=ui['physicsHeaderBounds'];click(x+w/4,y+93)
        ui=state();assert ui['physicsSection']==2
        original_body=send('scene.object.resolve',sceneObjectId=8101)['objects'][0]
        px,py,pw=reveal(12)
        # Move to roughly ten mass units on the logarithmic numerical control.
        click(px+118+max(80,pw-190)*4/9,py)
        edited=send('scene.object.resolve',sceneObjectId=8101)['objects'][0]
        assert edited['mass']!=original_body['mass'] and edited['mass']>0,edited
        ratio=edited['mass']/original_body['mass']
        assert math.isclose(edited['inverseMass'],1/edited['mass'],rel_tol=1e-5)
        for i in range(3):
            assert math.isclose(edited['inertia'][i],original_body['inertia'][i]*ratio,rel_tol=1e-5)
            assert math.isclose(edited['inverseInertia'][i],original_body['inverseInertia'][i]/ratio,rel_tol=1e-5)
        px,py,pw=reveal(52);click(px+20,py)
        undone=send('scene.object.resolve',sceneObjectId=8101)['objects'][0]
        assert undone['mass']==original_body['mass'] and undone['inertia']==original_body['inertia'],undone
        click(px+pw/2+20,py)
        redone=send('scene.object.resolve',sceneObjectId=8101)['objects'][0]
        assert redone['mass']==edited['mass'] and redone['inertia']==edited['inertia'],redone
        px,py,pw=reveal(84);click(px+20,py);state()
        authored=json.loads(mass_scene.read_text())
        saved_body=next(o for o in authored['objects'] if o.get('sceneObjectId')==8101)
        assert math.isclose(saved_body['mass'],edited['mass'],rel_tol=1e-5),saved_body
        send('scene.object.select',scope='editor',sceneObjectId=8102)
        fixed=send('scene.object.resolve',sceneObjectId=8102)['objects'][0]
        px,py,pw=reveal(12);click(px+pw-1,py)
        assert send('scene.object.resolve',sceneObjectId=8102)['objects'][0]['mass']==fixed['mass']
        checks.append(dict(massEdited=edited['mass'],inertiaScaled=True,undo=True,redo=True,saved=True,fixedRejected=True))
        send('scene.object.select',scope='editor',sceneObjectId=8101)
        send('editor.set_enabled',enabled=False)
        ui=state()
        before_impulse=send('scene.object.resolve',sceneObjectId=8101)['objects'][0]
        px,py,pw=reveal(512);click(px+20,py,700)
        queued=send('scene.object.resolve',sceneObjectId=8101)['objects'][0]
        assert queued['hasPendingImpulse'] and queued['pendingImpulse']==[0,10,0],queued
        assert queued['pendingImpulseOffset']==[0,0,0],queued
        assert queued['linearVelocity']==before_impulse['linearVelocity'],queued
        send('capture.screenshot',path=str(session/'impulse-preview.png'))
        send('run.step',count=1)
        after_impulse=send('scene.object.resolve',sceneObjectId=8101)['objects'][0]
        assert not after_impulse['hasPendingImpulse'],after_impulse
        expected_y=before_impulse['linearVelocity'][1]+10/edited['mass']-9.81/120
        assert math.isclose(after_impulse['linearVelocity'][1],expected_y,abs_tol=.02),(expected_y,after_impulse)
        send('run.step',count=1)
        second=send('scene.object.resolve',sceneObjectId=8101)['objects'][0]
        assert math.isclose(second['linearVelocity'][1],after_impulse['linearVelocity'][1]-9.81/120,abs_tol=.02),second
        checks.append(dict(pointImpulseHeldOnce=True,consumedOnce=True,centered=True))
        # A local offset changes angular motion without applying the impulse twice.
        px,py,pw=reveal(214+3*44);click(px+118+max(80,pw-190)*.6,py)
        before_offset=send('scene.object.resolve',sceneObjectId=8101)['objects'][0]
        px,py,pw=reveal(512);click(px+20,py,700)
        offset=send('scene.object.resolve',sceneObjectId=8101)['objects'][0]
        assert offset['hasPendingImpulse'] and any(abs(v)>.5 for v in offset['pendingImpulseOffset']),offset
        send('capture.screenshot',path=str(session/'off-center-preview.png'))
        send('run.step',count=1)
        rotated=send('scene.object.resolve',sceneObjectId=8101)['objects'][0]
        assert not rotated['hasPendingImpulse'] and rotated['angularVelocity']!=before_offset['angularVelocity'],rotated
        checks.append(dict(offCenterImpulse=True,offset=offset['pendingImpulseOffset'],angularVelocity=rotated['angularVelocity']))
        send('capture.screenshot',path=str(session/'actions.png'))
    finally:
        (session/'checks.json').write_text(json.dumps(checks,indent=2)+'\n')
        try:send('session.stop')
        finally:
            connection.close()
            (session/'mass-edited.scene.json').write_bytes(mass_scene.read_bytes())
            mass_scene.unlink()
    print('PASS: exact tick, explicit defaults, overlays, mass/inertia, undo/redo and authored save')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--session',type=Path,required=True)
    run(p.parse_args().session.resolve())
