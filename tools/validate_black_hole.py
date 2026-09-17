"""Verify black-hole attraction, identity and field snapping through Skarness."""

import argparse
import json
import math
import time
from pathlib import Path

from skarness import SkarnessConnection, launch

ROOT = Path(__file__).resolve().parents[1]
CORE_POSITION = [0.0, 100.0, 0.0]


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def verify_evidence(session):
    def read(name):
        return json.loads((session / name).read_text(encoding='utf-8'))

    before = read('bodies-before.json')
    after = read('bodies-one-tick.json')
    final = read('bodies-final.json')
    core = read('core-final.json')
    expected_ids = list(range(2, 302))
    for snapshot in (before, after, final):
        assert [body['sceneObjectId'] for body in snapshot] == expected_ids
    assert core['sceneObjectId'] == 1 and core['fixed']
    assert core['position'] == CORE_POSITION
    assert core['linearVelocity'] == [0.0, 0.0, 0.0]
    assert all(
        sum((b['linearVelocity'][k] - a['linearVelocity'][k])
            * (a['position'][k] - CORE_POSITION[k]) for k in range(3)) < 0
        for a, b in zip(before, after)
    ), 'every body must initially accelerate toward the core'
    assert all(
        math.dist(b['position'], CORE_POSITION) < math.dist(a['position'], CORE_POSITION)
        for a, b in zip(before, final)
    ), 'all 300 bodies must move closer to the core'
    generation = read('initial.json')['session.state']['sceneGeneration']
    for label in ('initial', 'first-physics-tick', 'advanced-2', 'advanced-6',
                  'advanced-14', 'advanced-22'):
        snapshot = read(label + '.json')
        assert snapshot['session.state']['sceneGeneration'] == generation
        ui = snapshot['ui.presentation']
        assert ui['gravityFieldSnapBalls'] and ui['gravityFieldColor'] == 1
        assert ui['gravityGridSourceCount'] == 301
        assert ui['gravityFieldSnappedCount'] == 300
        assert ui['gravityGridFirstSourceId'] == 1
        assert ui['gravityFieldFirstSnappedId'] == 2
    radii = sorted(math.dist(body['position'], CORE_POSITION) for body in final)
    result = {'passed': True, 'attractedBodies': 300, 'snappedBodies': 300,
              'finalRadius': {'minimum': radii[0], 'median': radii[149],
                              'p95': radii[284], 'maximum': radii[-1]}}
    write_json(session / 'result.json', result)
    print(json.dumps(result, indent=2), flush=True)


def run(session):
    # A perf log would enable benchmark mode, which restarts unlimited scenes.
    assert launch(session, ROOT / 'Automation/SKULLBONEZ_CORE.exe',
                  ROOT / 'SkullbonezData/scenes/space_black_hole_300.scene.json',
                  hidden=True, fixed_step=True,
                  layout_file=session / 'layout.preferences') == 0
    connection = SkarnessConnection(session)
    latest = {}
    offset = 0

    def send(command, **arguments):
        reply = connection.wait(connection.send(command, arguments))
        assert reply.get('status') == 'applied', (command, reply)
        return reply

    def sample(label):
        nonlocal offset
        send('run.step_frames', count=3)
        with (session / 'runtime.skarness.ndjson').open('rb') as stream:
            stream.seek(offset)
            for line in stream:
                if not line.endswith(b'\n'):
                    break
                offset += len(line)
                event = json.loads(line)
                if 'topic' in event:
                    latest[event['topic']] = event['payload']
        write_json(session / (label + '.json'), latest)

    def resolve(name):
        return send('scene.object.resolve', name=name)['result']['objects'][0]

    def bodies():
        return [resolve(f'infall_{index:03}') for index in range(300)]

    try:
        write_json(session / 'capabilities.json', send('capabilities.get'))
        send('state.subscribe', topics=[], detail='normal')
        sample('initial')
        send('capture.screenshot', path=str(session / 'initial.png'))
        write_json(session / 'bodies-before.json', bodies())
        # Three bounded steps produce at least one physics tick at 0.45 time scale.
        send('run.step', count=3)
        sample('first-physics-tick')
        write_json(session / 'bodies-one-tick.json', bodies())
        elapsed = 0
        for seconds in (2, 4, 8, 8):
            send('run.resume')
            time.sleep(seconds)
            send('run.pause')
            elapsed += seconds
            sample(f'advanced-{elapsed}')
            send('capture.screenshot', path=str(session / f'after-{elapsed}.png'))
        write_json(session / 'bodies-final.json', bodies())
        write_json(session / 'core-final.json', resolve('black_hole_core'))
        verify_evidence(session)
    finally:
        try:
            send('session.stop')
        finally:
            connection.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', type=Path,
                        default=ROOT / 'TestOutput/skarness/black-hole')
    parser.add_argument('--verify-existing', action='store_true',
                        help='Check preserved observations without launching the game.')
    args = parser.parse_args()
    if args.verify_existing:
        verify_evidence(args.session.resolve())
    else:
        run(args.session.resolve())


if __name__ == '__main__':
    main()
