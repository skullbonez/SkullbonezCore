"""Exercise four native viewports, plane panning, restored cameras and prediction draws."""
from __future__ import annotations
import argparse
import json
import math
import time
from pathlib import Path
from PIL import Image
from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]


def run(session: Path) -> None:
    session.mkdir(parents=True, exist_ok=True)
    # A high ball remains visible/pickable after the orthographic eye
    # zooms below it. The lower ball gives the fitted volume real depth.
    fixture = json.loads((REPO/'SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json').read_text())
    fixture['editor'] = {'editableScene': True}
    fixture['playback'] = {'frames': 'unlimited', 'fixedStep': True}
    fixture['objects'] = [dict(type='ball', name='upper_ball', position=[500,200,500], radius=8, mass=8, moment=12, restitution=.9, velocity=[0,0,0]),
                          dict(type='ball', name='lower_ball', position=[500,0,500], radius=8, mass=8, moment=12, restitution=.9, velocity=[0,0,0])]
    fixture['cameras'][0].update(position=[500,300,700], view=[500,100,500])
    fixture_path = session/'orthographic-depth.scene.json'
    fixture_path.write_text(json.dumps(fixture))
    assert launch(session, REPO/'Automation/SKULLBONEZ_CORE.exe',
                  fixture_path, hidden=True,
                  worker_threads=4, layout_file=session/'layout.preferences', allocation_guard='gameplay') == 0
    connection = SkarnessConnection(session)
    latest = {}
    offset = 0
    checks = []

    def send(command, **args):
        result = connection.wait(connection.send(command, args))
        assert result.get('status') == 'applied', (command, result)
        return result

    def sample():
        nonlocal offset
        send('run.step_frames', count=4)
        with (session/'runtime.skarness.ndjson').open('rb') as stream:
            stream.seek(offset)
            for line in stream:
                if not line.endswith(b'\n'): break
                offset += len(line)
                row = json.loads(line)
                if 'topic' in row: latest[row['topic']] = row['payload']
        return latest['ui.presentation']

    def click(bounds):
        x, y, w, h = bounds
        send('input.pointer_position', enabled=True, x=round(x+w/2), y=round(y+h/2))
        sample()
        send('input.pointer_drag', button='left', x=round(x+w/2), y=round(y+h/2), deltaX=0, deltaY=0)
        return sample()

    def pose():
        return {k: latest['camera.state'][k] for k in ('renderEye', 'renderView', 'renderUp')}

    def exercise(label):
        ui = sample()
        saved = pose()
        ui = click(ui['headerFourViewsBounds'])
        assert ui['fourViews'], ui
        assert pose() == saved, (saved, pose())
        for pane, normal in enumerate((1, 0, 2)):
            x, y, w, h = ui['editorPaneBounds'][pane]
            px, py = round(x+w*.65), round(y+h*.4)
            send('input.pointer_position', enabled=True, x=px, y=py)
            ui = sample()
            assert ui['activeEditorPane'] == pane, ui['activeEditorPane']
            before = pose()
            others = ui['editorPaneEyes']
            send('input.pointer_drag', button='right', x=px, y=py, deltaX=45, deltaY=27)
            ui = sample()
            after = pose()
            assert after['renderEye'] != before['renderEye'], (label, pane, 'pan did not move', before, after)
            assert after['renderEye'][normal] == before['renderEye'][normal], (label, pane, before, after)
            assert after['renderUp'] == before['renderUp']
            assert math.isclose(math.dist(after['renderEye'], after['renderView']), math.dist(before['renderEye'], before['renderView']), rel_tol=1e-5)
            for other in range(4):
                if other != pane: assert others[other] == ui['editorPaneEyes'][other], (pane, other)
            send('input.pointer_wheel', x=px, y=py, wheelDelta=120)
            ui = sample()
            zoomed = pose()
            assert zoomed['renderView'] == after['renderView']
            assert math.dist(zoomed['renderEye'], zoomed['renderView']) < math.dist(after['renderEye'], after['renderView'])
            checks.append(label+'-pan-zoom-'+str(pane))
        screenshot = session/(label+'-four.png')
        send('capture.screenshot', path=str(screenshot))
        (session/(label+'-four.json')).write_text(json.dumps(latest, indent=2))
        if label == 'lab':
            for pane in (0, 2):
                x, y, w, h = ui['editorPaneBounds'][pane]
                eye, focus = ui['editorPaneEyes'][pane], ui['editorPaneFocus'][pane]
                distance = math.dist(eye, focus)
                image_width = int(w)//2
                lens_y = 1/math.tan(math.pi/8)
                # Tick one moves the 7-unit-radius striker less than two units.
                dx = 390-focus[0]
                dy = 500-focus[2] if pane == 0 else -(28-focus[1])
                for side in (0, 1):
                    send('comparison.select', sceneObjectId=2)
                    px = x+side*image_width+image_width/2+dx*lens_y*h/(2*distance)
                    py = y+h/2+dy*lens_y*h/(2*distance)
                    ui = click((px, py, 0, 0))
                    assert send('comparison.state')['result']['comparison']['selected'] == 1, (pane, side, px, py)
            checks.append('lab-native-picking-both-tracks')
        if label == 'space':
            x, y, w, h = ui['editorPaneBounds'][0]
            before = ui['editorPaneEyes']
            send('input.pointer_drag', button='right', x=round(x+w*.85), y=round(y+h*.4), deltaX=round(w*.4), deltaY=10, moveClient=True)
            ui = sample()
            assert ui['editorPaneEyes'][0] != before[0]
            assert ui['editorPaneEyes'][1:] == before[1:]
            checks.append('captured-pan-crosses-divider')
        ui = click(ui['headerFourViewsBounds'])
        assert not ui['fourViews']
        assert pose() == saved, (label, saved, pose())
        checks.append(label+'-restore')

    try:
        capabilities = set(send('capabilities.get')['commands'])
        assert {'input.pointer_drag', 'input.pointer_wheel', 'replay.velocity_commit'} <= capabilities
        send('state.subscribe', topics=[], detail='summary')
        send('run.pause')
        ui = sample()
        if ui['layout'] != 'Editor': ui = click(ui['headerLayoutBounds'])
        if not ui['fourViews']: ui = click(ui['headerFourViewsBounds'])
        x,y,w,h = ui['editorPaneBounds'][0]
        for _ in range(20):
            send('input.pointer_wheel', x=round(x+w/2), y=round(y+h/2), wheelDelta=120)
            ui = sample()
            if ui['editorPaneEyes'][0][1] < 170: break
        assert ui['editorPaneEyes'][0][1] < 190, ui['editorPaneEyes'][0]
        send('capture.screenshot', path=str(session/'top-close-ball.png'))
        with Image.open(session/'top-close-ball.png').convert('RGB') as image:
            crop = image.crop((round(x+w*.4),round(y+h*.4),round(x+w*.6),round(y+h*.6)))
            assert sum(max(c)-min(c)>30 and max(c)>100 for c in crop.getdata()) > 100, 'Elevated ball clipped from Top'
        send('input.pointer_drag',button='left',x=round(x+w/2),y=round(y+h/2),deltaX=0,deltaY=0)
        sample()
        assert latest['ui.presentation']['editorSelectedObjectId'] == 1, latest['ui.presentation']['editorSelectedObjectId']
        checks.append('top-zoom-keeps-elevated-ball-visible-and-pickable')
        send('scene.load', name='prediction_ragdoll_wall_200.scene.json')
        ui = sample()
        if ui['layout'] != 'Editor': ui = click(ui['headerLayoutBounds'])
        if not ui['fourViews']: ui = click(ui['headerFourViewsBounds'])
        wall = send('scene.object.resolve', name='prediction_wall_brick_r03_c10')['result']['objects'][0]
        catcher = send('scene.object.resolve', name='prediction_striker_catcher_wall')['result']['objects'][0]
        x, y, w, h = ui['editorPaneBounds'][1]
        px, py = round(x+w/2), round(y+h/2)
        # Begin outside the catcher, then cross it while retaining a visible,
        # pickable destruction-wall brick farther along the same viewing ray.
        send('input.pointer_wheel', x=px, y=py, wheelDelta=-720)
        ui = sample()

        def pick_wall():
            eye, focus = ui['editorPaneEyes'][1], ui['editorPaneFocus'][1]
            scale = h / (2*math.dist(eye, focus)*math.tan(math.pi/8))
            position = wall['position']
            return click((x+w/2-(position[2]-focus[2])*scale,
                          y+h/2-(position[1]-focus[1])*scale, 0, 0))

        assert ui['editorPaneEyes'][1][0] > catcher['position'][0]+2
        ui = pick_wall()
        assert ui['editorSelectedObjectId'] == catcher['sceneObjectId'], ui['editorSelectedObjectId']
        for _ in range(8):
            send('input.pointer_wheel', x=px, y=py, wheelDelta=120)
            ui = sample()
            if ui['editorPaneEyes'][1][0] < catcher['position'][0]-2: break
        assert wall['position'][0]+2 < ui['editorPaneEyes'][1][0] < catcher['position'][0]-2
        ui = pick_wall()
        assert ui['editorSelectedObjectId'] == wall['sceneObjectId'], (ui['editorSelectedObjectId'], wall)
        send('capture.screenshot', path=str(session/'wall-past-catcher.png'))
        with Image.open(session/'wall-past-catcher.png').convert('RGB') as image:
            for pane in (1, 2):
                sx, sy, sw, sh = ui['editorPaneBounds'][pane]
                pixels = list(image.crop((round(sx+sw*.2), round(sy+sh*.15), round(sx+sw*.75), round(sy+sh*.3))).getdata())
                assert sum(max(c)>30 for c in pixels) > len(pixels)*.9, ('missing sky', pane)
        (session/'wall-past-catcher.json').write_text(json.dumps(latest, indent=2))
        checks.append('side-zoom-crosses-catcher-and-picks-destruction-wall-with-sky')
        send('scene.load', name='space_field_200.scene.json')
        ui = sample()
        if ui['layout'] != 'Editor': ui = click(ui['headerLayoutBounds'])
        send('run.step_frames', count=60)
        exercise('space')
        send('replay.set_prediction_horizon', seconds=20)
        send('replay.set_prediction_detail', highDetail=True)
        send('replay.set_reveal_speed', rate=1000)
        send('prediction.select_target', name='field_000')
        send('replay.set_prediction_enabled', enabled=True)
        ui = click(sample()['headerFourViewsBounds'])
        for label in ('original', 'modified'):
            if label == 'modified':
                send('replay.set_velocity_edit_enabled', enabled=True)
                send('replay.velocity_preview', linear=[-0.01, 0, 0.196], angular=[0, 0, 0])
                send('replay.velocity_commit')
            deadline = time.monotonic()+90
            while time.monotonic() < deadline:
                ui = sample()
                replay = latest['replay.state']
                if replay['predictionComplete'] and not replay['causeLoading'] and latest['replay.visual_packet']['header']['revealFrame'] >= 2400 and latest['replay.visual_packet']['header']['publishedFrameCount'] >= 2401: break
            else: raise AssertionError('Prediction did not finish')
            assert replay['pathTargetId'] == replay['publishedPredictionTargetId'] == replay['submittedPredictionTargetId'] == 1
            assert replay['trajectorySubmitted'] and replay['submittedGeometryBytes'] > 0, replay
            assert all(ui['editorPaneOverlayRendered']), ui['editorPaneOverlayRendered']
            if label == 'modified': assert latest['replay.visual_packet']['activePath']['allRed']
            if label == 'modified':
                original_hash = latest['replay.visual_packet']['originalPath']['geometryHash']
                x, y, w, h = ui['editorCanvasBounds']
                for dx, dy in ((68, h-68), (36, h-41), (96, h-41), (68, h-14)):
                    ui = click((x+dx, y+dy, 0, 0))
                    assert latest['replay.state']['divergence']['active']
                    assert latest['replay.state']['pathTargetId'] == 1
                    assert latest['replay.visual_packet']['originalPath']['geometryHash'] == original_hash
                checks.append('modified-retained-through-gizmo')
            send('capture.screenshot', path=str(session/(label+'-four.png')))
            with Image.open(session/(label+'-four.png')).convert('RGB') as image:
                for pane, (x, y, w, h) in enumerate(ui['editorPaneBounds']):
                    # Exclude all chrome, labels, camera controls and the action
                    # panel. White path pixels distinguish Original from balls;
                    # magenta/red pixels identify Modified's branch geometry.
                    pixels = image.crop((round(x+w*.25), round(y+45), round(x+w*.8), round(y+h-50))).getdata()
                    count = sum(min(c) > 150 if label == 'original' else c[0] > 140 and c[0] > c[1]*1.5 and c[2] > 70 for c in pixels)
                    assert count > (1000 if label == 'original' else 500), (label, pane, count)
            (session/(label+'-four.json')).write_text(json.dumps(latest, indent=2))
            checks.append(label+'-all-pane-submission')
        click(ui['headerFourViewsBounds'])
        click(sample()['headerWorkspaceBounds'])
        deadline = time.monotonic()+120
        while time.monotonic() < deadline:
            ui = sample()
            if send('comparison.state').get('result', {}).get('comparison', {}).get('active'): break
        else: raise AssertionError('Lab did not load')
        exercise('lab')
        # Both workspaces remain split while their selected panes and poses
        # round-trip through the shared registered camera slot.
        ui = click(sample()['headerFourViewsBounds'])
        ui = click(ui['headerWorkspaceBounds'])
        if not ui['fourViews']: ui = click(ui['headerFourViewsBounds'])
        for pane in range(4):
            x, y, w, h = ui['editorPaneBounds'][pane]
            send('input.pointer_position', enabled=True, x=round(x+w*.65), y=round(y+h*.4))
            ui = sample()
            saved_panes = (ui['editorPaneEyes'], ui['editorPaneFocus'])
            ui = click(ui['headerWorkspaceBounds'])
            assert ui['fourViews'] and ui['workspace'] == 'Solver Lab'
            ui = click(ui['headerWorkspaceBounds'])
            assert ui['fourViews'] and ui['activeEditorPane'] == pane
            assert saved_panes == (ui['editorPaneEyes'], ui['editorPaneFocus'])
        checks.append('retained-workspace-panes')
        ui = click(ui['headerLayoutBounds'])
        assert ui['layout'] == 'Canvas'
        x, y, w, h = ui['editorCanvasBounds']
        ui = click((x+38, y+h-88, 60, 24))
        assert ui['activeEditorPane'] == 0, 'Canvas gizmo did not select Top'
        send('window.resize', width=641, height=481)
        ui = sample()
        assert ui['fourViews'] and len({tuple(p[2:]) for p in ui['editorPaneBounds']}) == 1
        send('capture.screenshot', path=str(session/'compact-four.png'))
        checks.append('canvas-gizmo-odd-resize')
        send('window.resize', width=1784, height=961)
        sample()
        send('scene.load', name='aaa_ragdoll_sunset_showcase.scene.json')
        sample()
        exercise('cinematic')
    finally:
        try: send('session.stop')
        finally: connection.close()
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        shutdown = (session/'process.stdout.log').read_text(errors='replace')
        if '[allocation-guard] PASS:' in shutdown or '[allocation-guard] FAIL:' in shutdown: break
        time.sleep(.05)
    assert '[allocation-guard] PASS:' in shutdown, shutdown[-3000:]
    assert 'gameplay_violations=0' in shutdown and 'policy_violations=0' in shutdown
    (session/'result.json').write_text(json.dumps({'passed': True, 'checks': checks, 'allocationGuard': 'pass'}, indent=2))
    print('PASS: '+', '.join(checks))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', type=Path, required=True)
    run(parser.parse_args().session.resolve())
