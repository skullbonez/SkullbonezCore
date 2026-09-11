"""Exercise velocity handles through held native pointer input and release."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import time

from PIL import Image
from skarness import SkarnessConnection, launch
from validate_skarness_prediction_matrix import ReplayStateReader, vector_distance

REPO = Path(__file__).resolve().parents[1]


def run(session: Path) -> None:
    scene = REPO / 'SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json'
    assert launch(session, REPO / 'Automation/SKULLBONEZ_CORE.exe', scene, hidden=True,
                  allocation_guard='gameplay', layout_file=session / 'layout.preferences') == 0
    connection = SkarnessConnection(session)
    reader = ReplayStateReader(session / 'runtime.skarness.ndjson')

    def send(command: str, **arguments: object) -> dict:
        result = connection.wait(connection.send(command, arguments))
        assert result.get('status') == 'applied', (command, result)
        return result

    def state() -> dict:
        send('run.step_frames', count=2)
        return reader.latest()['payload']

    def ready(predicate) -> dict:
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            observed = state()
            if predicate(observed):
                return observed
        raise AssertionError(observed)

    def save(label: str, observed: dict) -> None:
        (session / f'{label}.json').write_text(json.dumps(observed, indent=2))

    def capture(label: str) -> None:
        path = session / f'{label}.png'
        send('capture.screenshot', path=str(path))
        with Image.open(path) as picture:
            picture.save(session / f'{label}-view.png')

    try:
        catalog = send('capabilities.get')['catalog']
        assert {'window.resize', 'input.pointer_drag', 'scene.object.resolve'} <= {row['name'] for row in catalog}
        send('state.subscribe', topics=[], detail='normal')
        send('window.resize', width=1784, height=961)
        capture('fixture')
        send('replay.set_prediction_horizon', seconds=3)
        send('prediction.select_target', name='path_striker_02')
        send('replay.set_prediction_enabled', enabled=True)
        stock = ready(lambda row: row['predictionComplete'])
        assert stock['divergence']['allocatedOwnerBytes'] == 0
        send('replay.set_velocity_edit_enabled', enabled=True)
        initial = ready(lambda row: row['divergence']['active'])
        send('run.step_frames', count=120)
        initial = state()
        assert initial['predictionGeneration'] == 0
        assert not initial['predictionGenerationPermitted'] and not initial['divergence']['redReady']
        save('enabled-without-edit', initial)
        capture('editable-handles')
        original = initial['divergence']['blueBodies']
        # Coordinates are inside the visible X arrow and X angular ring in this
        # fixed scene/camera/client fixture, outside the overlapping body/rings.
        for index, (x, y, dx, dy, field) in enumerate(((1070, 620, 65, -10, 'linearVelocity'),
                                                       (830, 520, 35, 15, 'angularVelocity'))):
            before = send('scene.object.resolve', name='path_striker_02')['result']['objects'][0]
            generation = state()['predictionGeneration']
            send('input.pointer_drag', button='left', x=x, y=y, deltaX=0, deltaY=0,
                 moveClient=True, holdMilliseconds=180)
            assert state()['predictionGeneration'] == generation, 'stationary click scheduled a prediction'
            request = connection.send('input.pointer_drag', dict(button='left', x=x, y=y,
                                      deltaX=dx, deltaY=dy, moveClient=True, holdAfterMoveMilliseconds=1500))
            time.sleep(0.3)
            held = reader.latest()['payload']
            changed = send('scene.object.resolve', name='path_striker_02')['result']['objects'][0]
            save(f'held-{index}', held)
            save(f'velocity-held-{index}', changed)
            assert vector_distance(before[field], changed[field]) > 0.01, (field, before, changed)
            assert held['predictionGeneration'] == generation
            assert not held['predictionGenerationPermitted'] and not held['divergence']['redReady']
            assert held['divergence']['blueBodies'] == original
            capture(f'held-{index}')
            if index == 0:
                with Image.open(session / f'held-{index}-view.png') as picture:
                    yellow = sum(r > 160 and g > 160 and b < 150
                                 for r, g, b in picture.convert('RGB').crop((950, 590, 1250, 646)).getdata())
                    assert yellow > 40, 'active velocity arrow was not rendered'
            assert connection.wait(request)['status'] == 'applied'
            released = ready(lambda row: row['divergence']['redReady'])
            assert released['predictionGeneration'] == generation + 1, 'release must schedule exactly one build'
            send('run.step_frames', count=30)
            assert state()['predictionGeneration'] == generation + 1
            save(f'released-{index}', released)
        send('replay.scrub', normalized=1.0)
        compared = state()
        blue = {row['id']: row for row in compared['divergence']['blueBodies']}
        red = {row['id']: row for row in compared['divergence']['redBodies']}
        assert blue.keys() == red.keys() and 7 in blue
        assert vector_distance(blue[7]['position'], red[7]['position']) > 1
        assert compared['divergence']['blueFrame'] == compared['divergence']['redFrame'] > 0
        save('divergent-futures', compared)
        capture('divergent-futures')
        # Switching from an analytic plane to RAW terrain must drain the initial
        # mesh upload before editing preparation replaces its vertex buffer.
        send('scene.load', name='at_rest.scene.json')
        send('prediction.select_target', name='ball_a')
        send('replay.set_prediction_enabled', enabled=True)
        resumed = ready(lambda row: row['predictionComplete'])
        assert not resumed['divergence']['active']
        save('raw-terrain-scene-replacement', resumed)
    finally:
        try:
            send('session.stop')
        finally:
            connection.close()
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        log = (session / 'process.stdout.log').read_text(errors='replace')
        if '[allocation-guard] PASS:' in log:
            break
        time.sleep(0.05)
    assert 'gameplay_violations=0' in log and 'policy_violations=0' in log
    print('PASS: visible linear/angular handles, no-op clicks, held edits, one build per release and divergent futures')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', type=Path, required=True)
    args = parser.parse_args()
    run(args.session.resolve())
