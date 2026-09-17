"""Check Split Future replay UI and R after both bodies have slept."""

import json
import sys
import time
from pathlib import Path

from skarness import SkarnessConnection, launch

ROOT = Path(__file__).resolve().parents[1]
HERO_IDS = (1101, 1102)


def run(session):
    session.mkdir(parents=True, exist_ok=True)
    layout = session / 'layout.preferences'
    layout.write_text(
        'version 6\nlayout 1\nleft 320\nright 320\ndrawer 340\ndiagnostics 400\n'
        'folded 7\ntool 0\nleftFolded 0\nrightFolded 0\ntheme 0\n'
        'replayFolded 0\ntoolsOpen 0\n'
    )
    assert launch(
        session, ROOT / 'Automation/SKULLBONEZ_CORE.exe',
        ROOT / 'SkullbonezData/scenes/split_future.scene.json',
        hidden=True, fixed_step=True, layout_file=layout,
    ) == 0
    connection = SkarnessConnection(session)
    latest = {}
    offset = 0

    def send(command, **args):
        reply = connection.wait(connection.send(command, args))
        assert reply.get('status') == 'applied', (command, reply)
        return reply

    def bodies():
        return [send('scene.object.resolve', sceneObjectId=identity)['result']['objects'][0]
                for identity in HERO_IDS]

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
        (session / (label + '.json')).write_text(json.dumps(latest, indent=2))
        return latest['ui.presentation']

    def key(code):
        send('input.set_key', key=code, down=True)
        send('run.step_frames', count=1)
        send('input.set_key', key=code, down=False)

    def capture(name):
        # Show the transport, then leave it before its tooltip obscures the scene.
        send('input.pointer_position', enabled=True, x=800, y=980)
        send('run.step_frames', count=30)
        send('input.pointer_position', enabled=True, x=1400, y=500)
        send('run.step_frames', count=2)
        send('capture.screenshot', path=str(session / name))

    try:
        capabilities = send('capabilities.get')
        (session / 'capabilities.json').write_text(json.dumps(capabilities, indent=2))
        send('state.subscribe', topics=[], detail='normal')
        send('window.resize', width=1600, height=1000)
        ui = sample('startup')
        assert ui['layout'] == 'Editor'
        assert ui['replayControlsBounds'][2] > 0
        assert ui['transportBounds'][2] > 0
        assert latest['replay.timeline']['presentation']['enabled']
        # Inspect intentionally waits for Space; use Scene for continuous playback.
        if ui['cameraMode'] == 2:
            key(70)
            ui = sample('scene-mode')
        assert ui['cameraMode'] == 1, ui['cameraMode']
        capture('01-replay-controls.png')
        send('window.resize', width=640, height=480)
        for batch in range(12):
            send('run.step', count=600)
            settled = bodies()
            if all(body['sleepStateAvailable'] and body['sleeping'] for body in settled):
                break
        assert all(body['sleepStateAvailable'] and body['sleeping'] for body in settled), settled
        print('Both bodies asleep after', 600 * (batch + 1), 'ticks', flush=True)
        (session / 'settled.json').write_text(json.dumps(settled, indent=2))

        key(82)
        reset = bodies()
        assert all(not body['sleeping'] for body in reset), reset
        assert reset[0]['position'] == [575., 82., 600.]
        assert reset[0]['linearVelocity'] == [10., 0., 1.]
        assert reset[1]['position'] == [667., 70., 625.]
        # No forced Physics step: this proves the normal playback policy advances.
        send('run.resume')
        time.sleep(1.5)
        falling = bodies()
        assert all(after['position'][1] < before['position'][1] - 1
                   for before, after in zip(reset, falling)), (reset, falling)
        send('run.pause')
        send('window.resize', width=1600, height=1000)
        sample('falling-after-R')
        assert latest['replay.timeline']['presentation']['sampleCount'] > 1
        capture('02-falling-after-R.png')

        send('replay.jump_to_start')
        sample('replay-start')
        assert latest['replay.timeline']['scrubber']['position'] == 0.0
        assert latest['input.state']['scrubPaused']
        capture('03-replay-start.png')
        send('replay.jump_to_end')
        sample('replay-end')
        assert latest['replay.timeline']['scrubber']['position'] == 1.0
        assert not latest['input.state']['scrubPaused']
        (session / 'result.json').write_text(json.dumps(
            {'passed': True, 'reset': reset, 'falling': falling}, indent=2,
        ))
        print('PASS: replay controls, sleeping-body R reset, restored initial motion and natural playback', flush=True)
    finally:
        try:
            send('session.stop')
        finally:
            connection.close()


if __name__ == '__main__':
    run(Path(sys.argv[1]).resolve())
