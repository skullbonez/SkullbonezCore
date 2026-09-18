"""Verify native camera gizmo views, plane panning, zoom and workspace retention."""
from __future__ import annotations
import argparse
import json
import math
import time
from PIL import Image
from pathlib import Path
from native_ui_comparison import current_wall_comparison
from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]


def run(session: Path) -> None:
    comparison_fixture = current_wall_comparison()
    assert launch(session, REPO / 'Automation/SKULLBONEZ_CORE.exe',
                  REPO / 'SkullbonezData/scenes/space_field_200.scene.json', hidden=True,
                  layout_file=session / 'layout.preferences') == 0
    connection = SkarnessConnection(session)
    latest = {}
    offset = 0
    checks = []
    before_click = {}
    tween_samples = []

    def send(command, **args):
        result = connection.wait(connection.send(command, args))
        assert result.get('status') == 'applied', (command, result)
        return result

    def sample(label):
        nonlocal offset
        send('run.step_frames', count=4)
        with (session / 'runtime.skarness.ndjson').open('rb') as stream:
            stream.seek(offset)
            for line in stream:
                if not line.endswith(b'\n'):
                    break
                offset += len(line)
                event = json.loads(line)
                if 'topic' in event:
                    latest[event['topic']] = event['payload']
                if event.get('topic') == 'camera.state' and event['payload'].get('tweenActive', False):
                    tween_samples.append(event['payload'])
        result = {k: latest[k] for k in ('ui.presentation', 'camera.state', 'scene.state')}
        (session / (label + '.json')).write_text(json.dumps(result, indent=2))
        return latest['ui.presentation']

    def click(x, y):
        send('input.pointer_position', enabled=True, x=round(x), y=round(y))
        send('run.step_frames', count=2)
        send('input.pointer_drag', button='left', x=round(x), y=round(y), deltaX=0, deltaY=0)

    def middle(bounds):
        x, y, w, h = bounds
        click(x+w/2, y+h/2)

    def preset(axis):
        nonlocal before_click
        # Dock and transport transitions temporarily own their moving bounds.
        # Target the settled viewport just as the visible button is presented.
        settle = time.monotonic() + .25
        while time.monotonic() < settle:
            send('run.step_frames', count=3)
        ui = sample('before-preset')
        x, y, w, h = ui['viewport']
        centers = [(68, h-22), (68, h-76), (36, h-49), (96, h-49)]
        dx, dy = centers[axis]
        send('input.pointer_position', enabled=True, x=round(x+dx), y=round(y+dy))
        settle = time.monotonic() + .25
        while time.monotonic() < settle:
            send('run.step_frames', count=3)
        sample('preset-hover')
        before_click = pose()
        previous_axis = latest['ui.presentation']['editorView']
        first_tween = len(tween_samples)
        send('input.pointer_drag', button='left', x=round(x+dx), y=round(y+dy), deltaX=0, deltaY=0)
        ui = sample('preset-' + str(axis))
        if ui['editorView'] != axis:
            send('capture.screenshot', path=str(session/'failed-preset.png'))
            raise AssertionError((axis, ui['editorView'], ui['viewport']))
        deadline = time.monotonic() + 3
        while latest['camera.state']['tweenActive'] and time.monotonic() < deadline:
            ui = sample('preset-' + str(axis))
        assert not latest['camera.state']['tweenActive'], 'Editor camera transition did not settle'
        if previous_axis != axis:
            assert any(0 < state['tweenProgress'] < 1 for state in tween_samples[first_tween:]), (axis, 'No intermediate editor pose rendered')
        return ui

    def pose():
        return {k: latest['camera.state'][k] for k in ('renderEye', 'renderView', 'renderUp')}

    def assert_playback_preserved(label):
        ui = sample(label+'-playback-start')
        if ui['layout'] != 'Editor':
            middle(ui['headerLayoutBounds'])
            sample(label+'-editor-layout')

        def solver_frame():
            return latest['replay.timeline']['solver']['nextFrame']

        def check_advance(paused, action):
            before = solver_frame()
            # The harness pause is independent of the user's pause. Release it
            # so captured solver frames prove whether live Physics actually ran.
            send('run.resume')
            time.sleep(.2)
            send('run.pause')
            sample(label+'-'+action)
            after = solver_frame()
            assert (after == before if paused else after > before), (label, action, paused, before, after)
            assert latest['input.state']['predictionEnabled'] == paused
            assert latest['replay.timeline']['scrubber']['liveAdvanceHeld'] == paused
            checks.append(label+'-'+action+('-paused' if paused else '-running'))

        for paused in (False, True):
            if paused:
                send('input.set_prediction_key', down=True)
                sample(label+'-pause-key')
                send('input.set_prediction_key', down=False)
                ui = sample(label+'-pause-release')
                if ui['toolsVisible']:
                    middle(ui['replayDetailsBounds'])
                    sample(label+'-tools-closed')
            check_advance(paused, 'before-views')
            for axis in (1, 2, 3, 0):
                preset(axis)
                check_advance(paused, 'view-'+str(axis))
            for enabled in (True, False):
                middle(latest['ui.presentation']['headerFourViewsBounds'])
                ui = sample(label+'-four-views')
                assert ui['fourViews'] == enabled
                check_advance(paused, 'four-views-'+str(enabled))
        send('input.set_prediction_key', down=True)
        sample(label+'-resume-key')
        send('input.set_prediction_key', down=False)
        sample(label+'-resume-release')

    def assert_axis(axis):
        p = pose()
        delta = [a-b for a,b in zip(p['renderEye'], p['renderView'])]
        component = {1:1, 2:0, 3:2}[axis]
        assert delta[component] > 0 and all(abs(v)<1e-4 for i,v in enumerate(delta) if i!=component), p
        assert p['renderUp'] == ([0,0,-1] if axis==1 else [0,1,0]), p

    def exercise(label):
        ui = sample(label+'-initial')
        if ui['layout'] != 'Editor':
            middle(ui['headerLayoutBounds'])
            send('run.step_frames', count=60)
        ui = preset(0)
        perspective = pose()
        assert math.dist(perspective['renderEye'], perspective['renderView']) > 0.001, perspective
        for axis in (1,2,3):
            ui = preset(axis)
            perspective = dict(before_click)
            assert_axis(axis)
            fixed = pose()
            x,y,w,h = ui['viewport']
            # Move off the gizmo before pressing: UI capture uses the hovered surface.
            send('input.pointer_position', enabled=True, x=round(x+w*.7), y=round(y+h*.4))
            sample(label+'-pan-hover-'+str(axis))
            send('input.pointer_drag', button='right', x=round(x+w*.7), y=round(y+h*.4), deltaX=80, deltaY=45, moveClient=True, holdMilliseconds=40)
            sample(label+'-panned-'+str(axis))
            assert_axis(axis)
            assert pose()['renderEye'] != fixed['renderEye']
            fixed = pose()
            send('input.pointer_drag', button='middle', x=round(x+w*.7), y=round(y+h*.4), deltaX=50, deltaY=35)
            send('input.set_movement', w=True, a=True, s=False, d=False)
            send('run.step_frames', count=20)
            send('input.set_movement', w=False, a=False, s=False, d=False)
            sample(label+'-locked-'+str(axis))
            assert pose() == fixed, (fixed, pose())
            send('input.pointer_wheel', x=round(x+w*.7), y=round(y+h*.4), wheelDelta=120)
            sample(label+'-zoom-'+str(axis))
            assert_axis(axis)
            assert pose()['renderView'] == fixed['renderView']
            assert math.dist(pose()['renderEye'],pose()['renderView']) < math.dist(fixed['renderEye'],fixed['renderView'])
            screenshot = session/(label+'-'+str(axis)+'.png')
            send('capture.screenshot', path=str(screenshot))
            # Pose alone cannot prove the widget survives the paired-view render.
            offsets = {1:(38, h-88,60), 2:(8,h-61,56), 3:(68,h-61,56)}
            left,top,width=offsets[axis]
            channel={1:1,2:0,3:2}[axis]
            with Image.open(screenshot).convert('RGB') as image:
                pixels=[image.getpixel((round(x+left+i),round(y+top+j))) for i in range(3,int(width)-3) for j in range(24)]
                colored=sum(pixel[channel]>100 and pixel[channel]>min(pixel)+35 for pixel in pixels)
                assert colored>20, (label,axis,'gizmo covered or not drawn',colored)
            preset(0)
            assert pose() == perspective, (perspective, pose())
            checks.append(label+'-'+str(axis))

    try:
        assert {'input.pointer_drag','input.pointer_wheel','input.set_movement','input.set_prediction_key','run.resume','run.pause','comparison.load'} <= set(send('capabilities.get')['commands'])
        send('state.subscribe', topics=[], detail='normal')
        assert_playback_preserved('space')
        exercise('space')
        preset(1)
        scene_pose = pose()
        ui = sample('scene-retained')
        # The workspace button does not load a recording into an empty Lab.
        send('comparison.load', path=str(comparison_fixture))
        sample('lab-loaded-fixture')
        assert send('comparison.state')['result']['comparison']['active']
        exercise('lab')
        middle(sample('lab-loaded')['headerWorkspaceBounds'])
        sample('new-scene')
        preset(1)
        scene_pose=pose()
        middle(sample('new-scene-retained')['headerWorkspaceBounds'])
        sample('lab-again')
        preset(2)
        lab_pose=pose()
        middle(sample('lab-retained')['headerWorkspaceBounds'])
        sample('scene-return')
        assert latest['ui.presentation']['editorView']==1 and pose()==scene_pose
        middle(latest['ui.presentation']['headerWorkspaceBounds'])
        sample('lab-return')
        assert latest['ui.presentation']['editorView']==2 and pose()==lab_pose
        middle(latest['ui.presentation']['headerWorkspaceBounds'])
        sample('scene-return-again')
        send('scene.load', name='at_rest.scene.json')
        exercise('rest')
        ui=sample('rest-editor-controls')
        if ui['editorControlsBounds'][2]<=0:
            middle(ui['editorTabBounds'])
            ui=sample('rest-editor-open')
        x,y,w,h=ui['editorControlsBounds']
        click(x+35,y+54)
        ui=sample('rest-edit-mode')
        assert ui['editorMode']
        middle(ui['headerLayoutBounds'])
        ui=sample('rest-edit-canvas')
        assert ui['layout']=='Canvas' and ui['editorMode']
        preset(1)
        assert_axis(1)
        send('window.resize',width=640,height=480)
        sample('compact-canvas')
        preset(3)
        assert_axis(3)
        send('capture.screenshot',path=str(session/'compact-edit-canvas.png'))
        send('window.resize',width=1784,height=961)
        ui=sample('leave-edit-canvas')
        middle(ui['headerLayoutBounds'])
        ui=sample('leave-edit-controls')
        if ui['editorControlsBounds'][2]<=0:
            middle(ui['editorTabBounds'])
            ui=sample('leave-edit-open')
        x,y,w,h=ui['editorControlsBounds']
        click(x+35,y+54)
        ui=sample('leave-edit-mode')
        assert not ui['editorMode']
        middle(ui['headerLayoutBounds'])
        ui=sample('leave-editor-surface')
        assert ui['editorView']==0
        send('scene.load_demo')
        assert_playback_preserved('demo')
        exercise('demo')
        assert any(0 < state['tweenProgress'] < 1 for state in tween_samples), 'No intermediate camera pose was rendered'
        (session/'tweens.json').write_text(json.dumps(tween_samples,indent=2))
        (session/'result.json').write_text(json.dumps({'passed':True,'checks':checks,'tweenSamples':len(tween_samples)},indent=2))
        print('PASS: native axis selection, plane pan, zoom, perspective restore, workspace retention: '+str(len(checks))+' views')
    finally:
        try:
            send('session.stop')
        finally:
            connection.close()

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', type=Path, required=True)
    run(parser.parse_args().session.resolve())
