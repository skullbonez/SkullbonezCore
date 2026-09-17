"""Check gravity-grid rendering, native toggle routing, and scene changes with 300 balls.

The extra balls are a local fixture; committed scene and renderer baselines stay untouched.
"""
from pathlib import Path
import argparse, json, copy, time, math
from skarness import launch, SkarnessConnection
root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--session', type=Path, default=root / 'TestOutput/skarness/gravity-grid')
args = parser.parse_args()
session = args.session.resolve()
session.mkdir(parents=True, exist_ok=True)
s = json.loads((root / 'SkullbonezData/scenes/space_field_200.scene.json').read_text())
for i in range(100):
    body = copy.deepcopy(s['objects'][i])
    body['name'] = f'field_{200 + i:03}'
    body['position'][0] += 15
    body['position'][2] += 25
    s['objects'].append(body)
s['simulation']['modelCapacity'] = 340
s['playback']['exitOnComplete'] = False
fixture = session / 'space300.scene.json'
fixture.write_text(json.dumps(s))
assert launch(session, root / 'Automation/SKULLBONEZ_CORE.exe', fixture, hidden=True, layout_file=session / 'layout.preferences', perf_log=session / 'perf.csv') == 0
c = SkarnessConnection(session)
latest = {}
offset = 0

def send(cmd, **args):
    r = c.wait(c.send(cmd, args))
    assert r.get('status') == 'applied', (cmd, r)
    return r

def sample(label):
    global offset
    end = time.monotonic() + 0.6
    while time.monotonic() < end:
        send('run.step_frames', count=3)
    with (session / 'runtime.skarness.ndjson').open('rb') as f:
        f.seek(offset)
        for line in f:
            if not line.endswith(b'\n'):
                break
            offset += len(line)
            e = json.loads(line)
            if 'topic' in e:
                latest[e['topic']] = e['payload']
    (session / (label + '.json')).write_text(json.dumps(latest, indent=2))
    return latest['ui.presentation']

def click(x, y):
    send('input.pointer_position', enabled=True, x=round(x), y=round(y))
    send('run.step_frames', count=2)
    send('input.pointer_drag', button='left', x=round(x), y=round(y), deltaX=0, deltaY=0)

def middle(b):
    click(b[0] + b[2] / 2, b[1] + b[3] / 2)

def reveal(ui, row):
    x, y, w, h = ui['toolsContentBounds']
    desired = max(0, row - h / 2)
    delta = round((ui['toolsScroll'] - desired) * 120 / 42)
    if delta:
        send('input.pointer_wheel', x=round(x + 60), y=round(y + h / 2), wheelDelta=delta)
    ui = sample('revealed')
    return (ui, ui['toolsContentBounds'][1] + row - ui['toolsScroll'] + 12)

def slider(ui, row, fraction, field, lo, hi, step):
    ui, py = reveal(ui, row)
    x, y, w, h = ui['toolsContentBounds']
    px = round(x + 118 + (w - 190) * fraction)
    expected = lo + math.floor((hi - lo) * min(1, max(0, (px - x - 118) / max(80, w - 190))) / step + 0.5) * step
    click(px, py)
    ui = sample(field)
    assert math.isclose(ui[field], expected, abs_tol=step * 0.01), (field, ui[field], expected)
    return ui
def options(ui):
    if not ui['toolsVisible']:
        middle(ui['replayDetailsBounds'])
        ui = sample('tools')
    for attempt in range(3):
        click(14 + (ui['window'][0] - 28) * 4.5 / 11, ui['drawerBounds'][1] + 66)
        ui = sample('options')
        if ui['activeTool'] == 4:
            return ui
    raise AssertionError(ui['activeTool'])


def toggle_snap(ui):
    ui = options(ui)
    ui, py = reveal(ui, 502)
    click(ui['toolsContentBounds'][0] + 100, py)
    return sample('snap-toggled')


def assert_snapped(ui):
    assert ui['gravityFieldSnapBalls'] and ui['gravityFieldSnappedCount'] == 300, ui
    assert ui['gravityFieldFirstSnappedId'] == ui['gravityGridFirstSourceId'] == 1
    position = ui['gravityFieldFirstSnappedPosition']
    assert position[::2] == ui['gravityGridFirstSourcePosition'][::2]
    assert math.isclose(position[1] - ui['gravityFieldFirstSnappedRadius'], ui['gravityFieldFirstSnappedSurfaceHeight'], abs_tol=0.0001)


try:
    caps = send('capabilities.get')
    (session / 'capabilities.json').write_text(json.dumps(caps, indent=2))
    send('state.subscribe', topics=[], detail='normal')
    ui = sample('default')
    assert ui['gravityGridSourceCount'] == 300, ui
    assert ui['gravityGridVertexCount'] > 0
    assert not ui['gravityFieldSnapBalls'] and ui['gravityFieldSnappedCount'] == 0
    send('capture.screenshot', path=str(session / 'default.png'))
    initial_depth = ui['gravityGridMinimumHeight']
    send('replay.set_prediction_enabled', enabled=False)
    send('run.resume')
    time.sleep(1)
    send('run.pause')
    ui = sample('moving')
    assert ui['gravityGridMinimumHeight'] != initial_depth
    assert ui['gravityGridSourceCount'] == 300
    original_physics = send('scene.object.resolve', name='field_000')['result']
    ui = toggle_snap(ui)
    assert_snapped(ui)
    assert send('scene.object.resolve', name='field_000')['result'] == original_physics
    send('capture.screenshot', path=str(session / 'snap-on.png'))
    ui = toggle_snap(ui)
    assert not ui['gravityFieldSnapBalls'] and ui['gravityFieldSnappedCount'] == 0
    ui = toggle_snap(ui)
    assert_snapped(ui)
    middle(ui['replayDetailsBounds'])
    ui = sample('snap-clean')
    send('replay.set_prediction_enabled', enabled=True)
    send('replay.set_prediction_horizon', seconds=2.0)
    send('prediction.select_target', name='field_000')
    send('run.until', condition='prediction.complete', maxFrames=3000)
    ui = sample('prediction-ready')
    seen = {}
    for label, value in [('past', 0.0), ('middle', 0.25), ('future', 1.0), ('past-again', 0.0), ('future-again', 1.0)]:
        send('replay.scrub', normalized=value)
        ui = sample(label)
        assert_snapped(ui)
        seen[label] = (ui['gravityGridFirstSourceId'], ui['gravityGridFirstSourcePosition'], ui['gravityGridMinimumHeight'], ui['gravityFieldFirstSnappedPosition'])
        assert seen[label][0] == 1
        if 'future' in label:
            assert latest['replay.state']['presentedReplayFrameSource'] == 2
            assert latest['replay.state']['presentedReplayFrame'] > 0
            assert latest['replay.state']['publishedPredictionTargetId'] == seen[label][0]
        send('capture.screenshot', path=str(session / (label + '.png')))
    assert seen['past'] != seen['future']
    assert seen['past'] == seen['past-again']
    assert seen['future'] == seen['future-again']
    send('replay.set_prediction_enabled', enabled=False)
    send('replay.jump_to_end')
    ui = sample('after-timeline')
    if not ui['toolsVisible']:
        middle(ui['replayDetailsBounds'])
        ui = sample('tools')
    for attempt in range(3):
        click(14 + (ui['window'][0] - 28) * 4.5 / 11, ui['drawerBounds'][1] + 66)
        ui = sample('options')
        if ui['activeTool'] == 4:
            break
    assert ui['activeTool'] == 4, (ui['activeTool'], ui['pointerClientPosition'])
    ui, py = reveal(ui, 338)
    x, y, w, h = ui['toolsContentBounds']
    click(x + 100, py)
    ui = sample('off')
    send('capture.screenshot', path=str(session / 'off.png'))
    assert_snapped(ui)
    assert ui['gravityGridVertexCount'] == 0, {k: ui[k] for k in ['activeTool', 'toolsScroll', 'toolsContentBounds', 'optionsToggles', 'pointerClientPosition']}
    click(x + 100, py)
    ui = sample('on')
    assert ui['gravityGridSourceCount'] == 300 and ui['gravityGridVertexCount'] > 0
    send('capture.screenshot', path=str(session / 'toggle-on.png'))
    previous_y = ui['gravityFieldFirstSnappedPosition'][1]
    previous_height = ui['gravityFieldHeight']
    ui = slider(ui, 372, 0.53, 'gravityFieldHeight', -1000, 1000, 1)
    assert_snapped(ui)
    assert math.isclose(ui['gravityFieldFirstSnappedPosition'][1] - previous_y, ui['gravityFieldHeight'] - previous_height, abs_tol=0.0001)
    ui = slider(ui, 420, 0.4, 'gravityFieldOpacity', 0, 1, 0.01)
    for color in [1, 2, 0, 1]:
        ui, py = reveal(ui, 468)
        x, y, w, h = ui['toolsContentBounds']
        click(x + color * 142 + 68, py)
        ui = sample('color-' + str(color))
        assert ui['gravityFieldColor'] == color
        send('capture.screenshot', path=str(session / ('color-' + str(color) + '.png')))
    settings = {k: ui[k] for k in ['gravityFieldHeight', 'gravityFieldOpacity', 'gravityFieldColor', 'gravityFieldSnapBalls']}
    send('scene.save')
    sample('saved')
    saved = json.loads(fixture.read_text())['debug']['gravityField']
    assert saved == {'height': settings['gravityFieldHeight'], 'opacity': settings['gravityFieldOpacity'], 'color': 'orange', 'snapBalls': True}, saved
    send('scene.reset')
    ui = sample('reloaded')
    assert all((ui[k] == v for k, v in settings.items())), (settings, ui)
    middle(ui['replayDetailsBounds'])
    ui = sample('clean')
    middle(ui['headerFourViewsBounds'])
    ui = sample('four')
    assert ui['fourViews']
    send('capture.screenshot', path=str(session / 'four.png'))
    send('scene.load', name='at_rest.scene.json')
    ui = sample('ordinary')
    assert ui['gravityGridVertexCount'] == 0
    send('scene.load', name='space_field_200.scene.json')
    ui = sample('space200')
    assert ui['gravityGridSourceCount'] == 200
    assert not ui['gravityFieldSnapBalls'] and ui['gravityFieldSnappedCount'] == 0
    (session / 'result.json').write_text(json.dumps({'passed': True, 'sources': [300, 200], 'checks': ['default-on', 'snap-ball-bottoms', 'snap-off-restores', 'snap-saves-with-level', 'native-toggle-off-on', 'four-views', 'moving-wells', 'history-prediction-repeatability', 'saved-height-opacity-colour', 'ordinary-hidden', 'scene-retention']}))

finally:
    try:
        send('session.stop')
    finally:
        c.close()

# Prove level persistence in a new process, rather than only observing settings
# retained by the current scene-reset path.
session = session / 'fresh-reload'
assert launch(session, root / 'Automation/SKULLBONEZ_CORE.exe', fixture, hidden=True, layout_file=session / 'layout.preferences', perf_log=session / 'perf.csv') == 0
c = SkarnessConnection(session)
latest = {}
offset = 0
try:
    send('capabilities.get')
    send('state.subscribe', topics=[], detail='normal')
    ui = sample('loaded')
    assert all(ui[k] == v for k, v in settings.items()), (settings, ui)
    assert_snapped(ui)
    send('capture.screenshot', path=str(session / 'saved-snap.png'))
    print('PASS gravity-grid native validation including fresh saved-level reload', flush=True)
finally:
    try:
        send('session.stop')
    finally:
        c.close()
