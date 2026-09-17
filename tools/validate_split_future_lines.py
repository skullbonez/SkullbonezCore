"""Native regression for Split Future path identity and box corner lights.

The line stream encodes point accents with coincident endpoints. Verify every
accent belongs to a true box corner; a sphere ring sample must not get a light.
Screenshots cover resting poses, camera rotation and viewport resizing.
"""
import argparse
import json
from pathlib import Path
from skarness import SkarnessConnection, launch
root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--session', type=Path, default=root / 'TestOutput/skarness/split-future-lines')
session = parser.parse_args().session.resolve()

def check_corner_lights(packet):
    values = packet['combinedLines']['values']
    points = []
    degree = {}
    for i in range(0, len(values), 12):
        a = tuple(values[i:i + 3])
        b = tuple(values[i + 6:i + 9])
        if a == b:
            points.append(a)
        else:
            degree[a] = degree.get(a, 0) + 1
            degree[b] = degree.get(b, 0) + 1
    assert points and len(points) % 8 == 0, 'Missing complete eight-corner box accents'
    assert len(set(points)) == len(points), 'Duplicate corner accents'
    assert all((degree.get(p) == 3 for p in points)), 'Light on a non-corner or sphere sample'
    return len(points)
assert launch(session, root / 'Automation/SKULLBONEZ_CORE.exe', root / 'SkullbonezData/scenes/split_future.scene.json', hidden=True, fixed_step=True) == 0
c = SkarnessConnection(session)

def send(cmd, **args):
    r = c.wait(c.send(cmd, args))
    assert r.get('status') == 'applied', (cmd, r)
    return r
latest = {}
offset = 0

def snapshot():
    global offset
    send('run.step_frames', count=4)
    with (session / 'runtime.skarness.ndjson').open('rb') as f:
        f.seek(offset)
        for line in f:
            if not line.endswith(b'\n'):
                break
            offset += len(line)
            row = json.loads(line)
            if 'topic' in row:
                latest[row['topic']] = row['payload']
    return latest
try:
    caps = send('capabilities.get')
    (session / 'capabilities.json').write_text(json.dumps(caps, indent=2))
    send('state.subscribe', topics=[], detail='normal')
    send('window.resize', width=1600, height=1000)
    send('ui.animation_clock', seconds=0, enabled=True)
    send('replay.set_prediction_horizon', seconds=10)
    results = []
    for identity, label in [(1101, 'ball'), (1102, 'box')]:
        send('prediction.select_target', sceneObjectId=identity)
        send('replay.set_prediction_enabled', enabled=True)
        send('run.until', condition='prediction.causal_rendered', maxFrames=3000)
        send('prediction.reveal_advance', frames=3000)
        send('state.subscribe', topics=[], detail='full')
        state = snapshot()
        send('state.subscribe', topics=[], detail='normal')
        packet = state['replay.visual_packet']
        submission = state['replay.render_submission']
        assert state['selection.state']['pathTargetId'] == packet['header']['targetId'] == submission['targetId'] == identity
        assert packet['header']['predictionComplete'] and packet['hasGeometry']
        assert packet['renderGeometry']['spanTelemetryMatches'] and submission['noReserveGrowth']
        lights = check_corner_lights(packet)
        (session / (label + '-packet.json')).write_text(json.dumps(packet, indent=2))
        send('capture.screenshot', path=str(session / (label + '.png')))
        results.append({'targetId': identity, 'geometryBytes': packet['renderGeometry']['geometryBytes'], 'hash': packet['renderGeometry']['submissionHash'], 'complete': True, 'cornerLights': lights})
    send('scene.object.select', scope='inspect', sceneObjectId=1101)
    send('prediction.select_target', sceneObjectId=1101)
    send('replay.set_prediction_horizon', seconds=51)
    send('run.until', condition='prediction.causal_rendered', maxFrames=3000)
    send('prediction.reveal_advance', frames=10000)
    send('state.subscribe', topics=[], detail='full')
    state = snapshot()
    send('state.subscribe', topics=[], detail='normal')
    send('capture.screenshot', path=str(session / 'resting-ghosts.png'))
    (session / 'resting-packet.json').write_text(json.dumps(state['replay.visual_packet']))
    packet = state['replay.visual_packet']
    colors = packet['combinedLines'].get('values', [])
    resting = sum((1 for i in range(3, len(colors), 6) if abs(colors[i] - 0.58) < 0.001 and abs(colors[i + 1] - 0.58) < 0.001))
    assert resting > 0, 'Missing resting markers'
    resting_lights = check_corner_lights(packet)
    send('input.pointer_drag', button='right', x=1100, y=500, deltaX=-85, deltaY=50)
    send('run.step_frames', count=4)
    send('capture.screenshot', path=str(session / 'orbit.png'))
    send('window.resize', width=1000, height=700)
    send('run.step_frames', count=4)
    send('capture.screenshot', path=str(session / 'resized.png'))
    result = {'passed': True, 'selectionAndSubmission': results, 'resized': [1000, 700], 'restingMarkerVertices': resting, 'restingCornerLights': resting_lights}
    (session / 'result.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))
finally:
    send('session.stop')
    c.close()
