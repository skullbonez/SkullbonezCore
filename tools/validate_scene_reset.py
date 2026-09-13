"""Prove authored restart preserves unsaved objects and only explicit Save writes scenes."""
from pathlib import Path
import argparse
import json
import time
from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]


def run(session):
    session.mkdir(parents=True, exist_ok=True)
    name = 'reset_save_' + str(time.time_ns())
    saved = REPO / 'SkullbonezData/scenes' / (name + '.scene.json')
    abandoned = REPO / 'SkullbonezData/scenes' / (name + '_abandoned.scene.json')
    assert not saved.exists() and not abandoned.exists()
    fixture = REPO / 'SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json'
    fixture_bytes = fixture.read_bytes()
    assert launch(session, REPO / 'Automation/SKULLBONEZ_CORE.exe', fixture, hidden=True,
                  fixed_step=True, layout_file=session/'layout.preferences', allocation_guard='gameplay') == 0
    connection = SkarnessConnection(session)
    latest = {}
    offset = 0

    def send(command, **args):
        reply = connection.wait(connection.send(command, args))
        assert reply.get('status') == 'applied', (command, reply)
        return reply

    def sample(label):
        nonlocal offset
        send('run.step_frames', count=4)
        with (session/'runtime.skarness.ndjson').open(encoding='utf-8') as trace:
            trace.seek(offset)
            for line in trace:
                event = json.loads(line)
                if 'topic' in event:
                    latest[event['topic']] = event['payload']
            offset = trace.tell()
        (session/(label+'.json')).write_text(json.dumps(latest, indent=2), encoding='utf-8')
        return latest['ui.presentation']

    def click(x, y):
        send('input.pointer_position', enabled=True, x=round(x), y=round(y))
        send('run.step_frames', count=3)
        time.sleep(.22)
        send('input.pointer_drag', button='left', x=round(x), y=round(y), deltaX=0, deltaY=0)

    def middle(bounds):
        x, y, width, height = bounds
        assert width > 0 and height > 0, bounds
        click(x+width/2, y+height/2)

    def key(code):
        send('input.set_key', key=code, down=True)
        send('run.step_frames', count=1)
        send('input.set_key', key=code, down=False)
        send('run.step_frames', count=3)

    def objects():
        rows = send('scene.object.list')['result'].get('objects', [])
        return [send('scene.object.resolve', sceneObjectId=row['sceneObjectId'])['result']['objects'][0] for row in rows]

    def editing(ui, enabled):
        if ui['layout'] != 'Editor':
            middle(ui['headerLayoutBounds'])
            ui = sample('editor-layout')
        if not ui['editorControlsBounds'][2]:
            middle(ui['editorTabBounds'])
            ui = sample('editor-panel')
        if ui['editorMode'] != enabled:
            x, y, _, _ = ui['editorControlsBounds']
            click(x+30, y+54)
            ui = sample('editor-mode')
        assert ui['editorMode'] == enabled
        return ui

    def place(ui, fraction):
        x, y, _, _ = ui['editorControlsBounds']
        click(x+16, y+508)
        ui = sample('box-placement')
        assert ui['editorPlacement']
        vx, vy, vw, vh = ui['viewport']
        click(vx+vw*fraction, vy+vh*.55)
        return sample('object-placed')

    def assert_restored(expected):
        actual = objects()
        assert len(actual) == len(expected), (actual, expected)
        for before, after in zip(expected, actual):
            for field in ('sceneObjectId', 'name', 'position', 'linearVelocity', 'angularVelocity', 'fixed'):
                assert after[field] == before[field], (field, before, after)

    saved_bytes = None
    try:
        capabilities = send('capabilities.get')
        (session/'capabilities.json').write_text(json.dumps(capabilities, indent=2))
        assert {'scene.create', 'scene.reset', 'scene.save', 'scene.object.resolve', 'input.set_key'} <= set(capabilities['commands'])
        send('state.subscribe', topics=[], detail='normal')
        send('scene.create', name=name)
        ui = sample('new-unsaved-scene')
        assert not saved.exists() and objects() == []
        assert Path(latest['scene.state']['scenePath']).name == saved.name
        ui = editing(ui, True)
        ui = place(ui, .44)
        ui = place(ui, .60)
        authored = objects()
        assert len(authored) == 2, authored
        assert not saved.exists()
        ui = editing(ui, False)
        send('run.step', count=120)
        ui = sample('simulated')
        assert any(a['position'] != b['position'] for a, b in zip(authored, objects()))
        generation = latest['session.state']['sceneGeneration']
        resets = latest['scene.state']['manualResetCount']
        key(ord('R'))
        ui = sample('keyboard-reset')
        assert_restored(authored)
        assert latest['session.state']['sceneGeneration'] == generation
        assert latest['scene.state']['manualResetCount'] == resets+1
        assert not saved.exists()
        ui = editing(ui, True)
        middle(ui['headerFourViewsBounds'])
        ui = sample('four-views')
        send('scene.object.select', scope='editor', sceneObjectId=authored[0]['sceneObjectId'])
        ui = sample('selected')
        camera = dict(latest['camera.state'])
        send('scene.reset')
        ui = sample('acknowledged-reset')
        assert_restored(authored)
        assert ui['editorMode'] and ui['fourViews']
        assert ui['editorSelectedObjectId'] == authored[0]['sceneObjectId']
        assert latest['camera.state'] == camera
        assert not saved.exists()
        send('capture.screenshot', path=str(session/'reset-four-views.png'))
        send('scene.save')
        ui = sample('explicit-save')
        assert saved.exists()
        saved_bytes = saved.read_bytes()
        document = json.loads(saved_bytes)
        assert len(document['objects']) == 2, document
        (session/'explicitly-saved.scene.json').write_bytes(saved_bytes)
        # A later unsaved placement must survive reset but not be written on
        # scene replacement or shutdown. Reopening restores exactly the save.
        middle(ui['headerFourViewsBounds'])
        ui = sample('single-view')
        ui = place(ui, .72)
        assert len(objects()) == 3
        key(ord('R'))
        ui = sample('saved-scene-unsaved-edit-reset')
        assert len(objects()) == 3 and saved.read_bytes() == saved_bytes
        send('scene.load', name=fixture.name)
        sample('switched-away')
        assert saved.read_bytes() == saved_bytes
        send('scene.load', name=saved.name)
        ui = sample('reopened-save')
        assert len(objects()) == 2
        send('scene.create', name=name+'_abandoned')
        ui = sample('second-unsaved-scene')
        ui = place(editing(ui, True), .5)
        assert len(objects()) == 1 and not abandoned.exists()
        send('scene.load', name=saved.name)
        sample('abandoned-draft')
        assert len(objects()) == 2 and not abandoned.exists()
        assert fixture.read_bytes() == fixture_bytes
    finally:
        try:
            send('session.stop')
        finally:
            connection.close()
        if saved.exists():
            assert saved.parent == (REPO/'SkullbonezData/scenes').resolve()
            if saved_bytes is not None:
                assert saved.read_bytes() == saved_bytes
            saved.unlink()
    assert not abandoned.exists() and fixture.read_bytes() == fixture_bytes
    deadline = time.monotonic()+15
    while time.monotonic() < deadline:
        shutdown = (session/'process.stdout.log').read_text(errors='replace')
        if '[allocation-guard] PASS:' in shutdown or '[allocation-guard] FAIL:' in shutdown:
            break
        time.sleep(.05)
    assert '[allocation-guard] PASS:' in shutdown, shutdown[-3000:]
    (session/'result.json').write_text(json.dumps({'passed': True, 'objectsRestored': 2, 'explicitSaveOnly': True, 'allocationGuard': 'pass'}, indent=2))
    print('PASS: R restores placed objects, camera and selection survive, drafts stay unsaved, explicit Save persists, and reload discards later unsaved edits')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', type=Path, required=True)
    run(parser.parse_args().session.resolve())
