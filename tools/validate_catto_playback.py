"""Check Catto playback through normal resume, reset and scene switching.

Forced physics steps bypass Inspect mode, so they cannot detect an authored
snapshot accidentally starting paused. This test waits for normal render turns
and then requires identity-bound physical motion.
"""
from __future__ import annotations

import argparse
import json
import math
import time
from pathlib import Path

from skarness import SkarnessConnection, launch

ROOT = Path(__file__).resolve().parents[1]


def run(session):
    assert not session.exists(), 'Use a fresh session directory'
    scene = ROOT / 'SkullbonezData/scenes/catto_double_domino.scene.json'
    authored = json.loads(scene.read_text())
    dominoes = [value for value in authored['objects'] if value['name'].startswith('domino_')]
    first = dominoes[0]
    assert launch(session, ROOT / 'Automation/SKULLBONEZ_CORE.exe', scene,
                  hidden=True, fixed_step=True, worker_threads=4) == 0
    connection = SkarnessConnection(session)
    results = []

    def send(command, **arguments):
        response = connection.wait(connection.send(command, arguments))
        with (session / 'commands.ndjson').open('a', encoding='utf-8') as trace:
            trace.write(json.dumps(dict(command=command, arguments=arguments, response=response))+'\n')
        assert response.get('status') == 'applied', response
        return response

    def resolve(expected):
        actual = send('scene.object.resolve', sceneObjectId=expected['sceneObjectId'])['result']['objects'][0]
        assert actual['name'] == expected['name'] and actual['sceneObjectId'] == expected['sceneObjectId'], actual
        assert all(math.isfinite(v) for field in ('position', 'linearVelocity', 'angularVelocity') for v in actual[field])
        return actual

    def live_turns(count, filename):
        send('run.resume')
        first_frame = None
        deadline = time.monotonic()+20
        while time.monotonic() < deadline:
            event = connection.read_event()
            if event.get('topic') != 'scene.state' or event.get('paused') or not event.get('payload'):
                continue
            state = event['payload']
            assert Path(state['scenePath']).name == filename, state
            assert state['pauseLocked'] is False and state['physicsEnabled'] is True, state
            first_frame = event['sceneFrame'] if first_frame is None else first_frame
            if event['sceneFrame'] >= first_frame+count:
                send('run.pause')
                return dict(firstFrame=first_frame, lastFrame=event['sceneFrame'], scene=state)
        raise AssertionError(('Normal playback did not advance', filename, first_frame))

    def check_domino(label):
        before = resolve(first)
        assert max(abs(a-b) for a, b in zip(before['position'], first['position'])) < 0.01, before
        assert before['linearVelocity'][0] > 3 and before['angularVelocity'][2] < -4, before
        frames = live_turns(180, scene.name)
        after = resolve(first)
        neighbour = resolve(dominoes[1])
        # The narrow block must tip toward its neighbour, not merely report
        # advancing UI frames while Inspect leaves its initial velocities frozen.
        assert after['position'][0] > before['position'][0]+1, after
        assert after['position'][1] < before['position'][1]-0.6, after
        assert neighbour['position'][1] < dominoes[1]['position'][1]-0.3, neighbour
        send('run.step_frames', count=2)
        send('capture.screenshot', path=str(session / (label+'.png')))
        results.append(dict(case=label, frames=frames, before=before, after=after, neighbour=neighbour))
        print('PASS:', label, flush=True)

    try:
        capabilities = send('capabilities.get')
        assert {'state.subscribe', 'run.resume', 'run.pause', 'run.step_frames', 'scene.reset', 'scene.load',
                'scene.object.resolve', 'window.resize', 'replay.set_recording_enabled', 'capture.screenshot', 'session.stop'} <= set(capabilities['commands'])
        send('state.subscribe', topics=['scene.state'], detail='summary')
        send('replay.set_recording_enabled', enabled=False)
        send('window.resize', width=1280, height=800)
        check_domino('startup-topples')
        send('scene.reset')
        check_domino('reset-topples')
        send('scene.load', name='catto_single_box.scene.json')
        send('replay.set_recording_enabled', enabled=False)
        box_scene = json.loads((ROOT/'SkullbonezData/scenes/catto_single_box.scene.json').read_text())
        box = next(v for v in box_scene['objects'] if v['name'] == 'falling_box')
        before = resolve(box)
        frames = live_turns(90, 'catto_single_box.scene.json')
        after = resolve(box)
        assert after['position'][1] < before['position'][1]-1, after
        results.append(dict(case='next-scene-runs', frames=frames, before=before, after=after))
        send('scene.load', name=scene.name)
        send('replay.set_recording_enabled', enabled=False)
        check_domino('return-topples')
        send('state.subscribe', topics=[], detail='summary')
        (session/'results.json').write_text(json.dumps(dict(ok=True, cases=results), indent=2)+'\n')
    finally:
        try:
            send('session.stop')
        finally:
            connection.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', required=True, type=Path)
    args = parser.parse_args()
    run(args.session.resolve())
