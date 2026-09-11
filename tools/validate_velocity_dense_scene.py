"""Verify the 200-box, 20-second velocity experiment completes and can be used."""
from pathlib import Path
import json, math, time
REPO = Path(__file__).resolve().parents[1]
from skarness import launch, SkarnessConnection
from validate_skarness_prediction_matrix import ReplayStateReader
import argparse
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--session', type=Path, required=True)
p = parser.parse_args().session.resolve()
assert launch(p, REPO / 'Automation/SKULLBONEZ_CORE.exe', REPO / 'SkullbonezData/scenes/prediction_ragdoll_wall_200.scene.json', hidden=True, layout_file=p / 'layout.preferences', worker_threads=4) == 0
c = SkarnessConnection(p)
reader = ReplayStateReader(p / 'runtime.skarness.ndjson')

def send(cmd, **args):
    r = c.wait(c.send(cmd, args))
    assert r['status'] == 'applied', (cmd, r)
    return r

def state():
    send('run.step_frames', count=2)
    return reader.latest()['payload']

def ready(test):
    end = time.monotonic() + 90
    while time.monotonic() < end:
        q = state()
        if test(q):
            return q
    (p / 'timeout.json').write_text(json.dumps(q, indent=2))
    raise AssertionError(compact(q))

def compact(q):
    return {k: q[k] for k in ('predictionComplete', 'predictionBuilding', 'predictionGeneration', 'predictionGenerationPermitted', 'causeLoading')} | {'redReady': q['divergence']['redReady']}

def topic(name):
    path = p / 'runtime.skarness.ndjson'
    with path.open('rb') as f:
        f.seek(max(0, path.stat().st_size - 1048576))
        lines = f.read().splitlines()[1:]
    for line in reversed(lines):
        try:
            r = json.loads(line)
        except json.JSONDecodeError:
            continue
        if r.get('topic') == name:
            return r['payload']
    raise AssertionError(name)

def save(name, q):
    (p / f'{name}.json').write_text(json.dumps(q, indent=2))

def click(x, y):
    send('input.pointer_drag', button='left', x=round(x), y=round(y), deltaX=0, deltaY=0, moveClient=True)
    state()

def screenshot(name):
    from PIL import Image
    path = p / f'{name}.png'
    send('capture.screenshot', path=str(path))
    with Image.open(path) as im:
        im.save(p / f'{name}-view.png')
try:
    catalog = send('capabilities.get')['catalog']
    assert {'comparison.state', 'replay.velocity_preview', 'replay.velocity_commit'} <= {r['name'] for r in catalog}
    send('state.subscribe', topics=[], detail='normal')
    send('replay.set_prediction_horizon', seconds=20)
    send('prediction.select_target', name='prediction_striker_ball')
    send('replay.set_prediction_enabled', enabled=True)
    ready(lambda q: q['predictionComplete'])
    send('replay.set_velocity_edit_enabled', enabled=True)
    initial = ready(lambda q: q['divergence']['active'])
    assert not initial['predictionGenerationPermitted'] and initial['predictionGeneration'] == 0
    for key in ('headerLayoutBounds', 'editorReplayTabBounds', 'causeTabBounds'):
        x, y, w, h = topic('ui.presentation')[key]
        click(x + w / 2, y + h / 2)
        send('run.step_frames', count=60)
    send('replay.velocity_preview', linear=[130, -1, 0], angular=[0, 0, -14])
    send('run.step_frames', count=30)
    assert state()['predictionGeneration'] == 0
    send('replay.velocity_commit')
    q = ready(lambda q: q['divergence']['redReady'] and (not q['causeLoading']))
    assert q['predictionComplete'] and (not q['predictionDirty'])
    assert q['predictionGeneration'] == 1 and q['publishedPredictionFrames'] == 2401
    assert q['pathTargetId'] == q['publishedPredictionTargetId'] == q['submittedPredictionTargetId'] == 1
    save('completed-modified', q)
    screenshot('completed-modified')
    for speed in (120, 130):
        send('replay.velocity_preview', linear=[speed, -1, 0], angular=[0, 0, -14])
        send('replay.velocity_commit')
        q = ready(lambda q: q['divergence']['redReady'] and (not q['causeLoading']))
        assert q['predictionComplete'] and (not q['predictionDirty'])
        save(f'repeated-edit-{speed}', q)
    eye, center, up = (q[k] for k in ('cameraPrimaryEye', 'cameraPrimaryView', 'cameraPrimaryUp'))

    def sub(a, b):
        return [x - y for x, y in zip(a, b)]

    def dot(a, b):
        return sum((x * y for x, y in zip(a, b)))

    def unit(a):
        return [x / math.sqrt(dot(a, a)) for x in a]

    def cross(a, b):
        return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
    forward = unit(sub(center, eye))
    right = unit(cross(forward, up))
    vertical = cross(right, forward)
    body = send('scene.object.resolve', name='prediction_striker_ball')['result']['objects'][0]
    point = [p + v * 36 / 140 for p, v in zip(body['position'], body['linearVelocity'])]
    point[0] += 14 * 0.7
    d = sub(point, eye)
    vx, vy, vw, vh = topic('ui.presentation')['viewport']
    scale = vh / 2 / math.tan(math.radians(45) / 2) / dot(d, forward)
    generation = q['predictionGeneration']
    click(vx + vw / 2 + dot(d, right) * scale, vy + vh / 2 - dot(d, vertical) * scale)
    q = ready(lambda q: q['divergence']['redReady'])
    assert q['predictionGeneration'] == generation
    save('stationary-click-ready', q)
    send('replay.scrub', normalized=1.0)
    q = state()
    d = q['divergence']
    assert d['redFrame'] == d['blueFrame'] and d['redFrame'] >= 2400
    blue = {b['id']: b for b in d['blueBodies']}
    red = {b['id']: b for b in d['redBodies']}
    assert blue.keys() == red.keys() and len(blue) >= 200
    assert blue[1]['position'] != red[1]['position']
    save('divergent-futures', q)
    ui = topic('ui.presentation')
    vx, vy, vw, vh = ui['viewport']
    click(vx + 70, ui['transportBounds'][1] - 61)
    lab = send('comparison.state')['result']['comparison']
    assert lab['active'] and lab['selected'] == 1 and all(lab['coverage']), lab
    send('comparison.seek', tick=1200)
    lab = send('comparison.state')['result']['comparison']
    assert lab['distanceMetres'] > 0 or lab['angleDegrees'] > 0.1, lab
    save('solver-lab', lab)
    screenshot('solver-lab')
    send('input.set_key', key=27, down=True)
    state()
    send('input.set_key', key=27, down=False)
    ready(lambda q: not topic('ui.presentation')['panelsAnimating'])
    ui = topic('ui.presentation')
    vx, vy, vw, vh = ui['viewport']
    click(vx + 70, ui['transportBounds'][1] - 25)
    accepted = ready(lambda q: not q['divergence']['active'])
    assert accepted['divergence']['allocatedOwnerBytes'] == 0
    body = send('scene.object.resolve', name='prediction_striker_ball')['result']['objects'][0]
    assert abs(body['linearVelocity'][0] - 130) < 0.001, body
    save('accepted-modified', accepted)
    print('PASS: 200-box modified result completes, opens in Solver Lab, and accepts Modified', flush=True)
finally:
    send('session.stop')
    c.close()
