"""Exercise velocity handles through held native pointer input and release."""
from __future__ import annotations

import argparse
import json
import math
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

    owner_topics = {}
    topic_offset = 0
    handle_samples = 0

    def topic_state(name: str) -> dict:
        nonlocal topic_offset
        path = session / 'runtime.skarness.ndjson'
        with path.open('rb') as stream:
            stream.seek(topic_offset)
            for line in stream:
                if not line.endswith(b'\n'):
                    break
                topic_offset += len(line)
                event = json.loads(line)
                if 'topic' in event:
                    owner_topics[event['topic']] = event['payload']
        assert name in owner_topics, f'{name} was not published'
        return owner_topics[name]

    def ui_state() -> dict:
        return topic_state('ui.presentation')

    def click_bounds(bounds) -> None:
        x, y, w, h = bounds
        assert w > 0 and h > 0, bounds
        send('input.pointer_drag', button='left', x=round(x+w/2), y=round(y+h/2), deltaX=0, deltaY=0, moveClient=True)
        send('run.step_frames', count=60)

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
        send('run.step_frames', count=120)
        initial = state()
        assert initial['predictionGeneration'] == stock['predictionGeneration']
        assert not initial['divergence']['active'] and initial['divergence']['allocatedOwnerBytes'] == 0
        assert initial['predictionComplete'] and not initial['causeLoading']
        save('enabled-without-edit', initial)
        capture('editable-handles')
        original = None
        def body_state():
            return send('scene.object.resolve', name='path_striker_02')['result']['objects'][0]

        def handle( angular, axis ):
            nonlocal handle_samples
            state()
            body = body_state()
            # A returning camera can still be tweening after scene cancellation.
            # Pick the handle where it is drawn, not at the destination camera.
            camera = topic_state('camera.state')
            eye, center, up = (camera[key] for key in ('renderEye', 'renderView', 'renderUp'))
            save(f'handle-camera-{handle_samples}', camera)
            handle_samples += 1
            def sub(a, b): return [x-y for x, y in zip(a, b)]
            def dot(a, b): return sum(x*y for x, y in zip(a, b))
            def unit(a): return [x/math.sqrt(dot(a, a)) for x in a]
            def cross(a, b): return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]
            forward = unit(sub(center, eye)); right = unit(cross(forward, up)); vertical = cross(right, forward)
            vx, vy, vw, vh = ui_state()['viewport']
            def project(point):
                delta = sub(point, eye)
                scale = vh/2 / math.tan(math.radians(45)/2) / dot(delta, forward)
                return [vx + vw/2 + dot(delta, right)*scale, vy + vh/2 - dot(delta, vertical)*scale]
            velocity = body['angularVelocity' if angular else 'linearVelocity']
            tip = [p+v*36/(5 if angular else 140) for p, v in zip(body['position'], velocity)]
            end = tip.copy(); end[axis] += math.sqrt(108)+7
            start, finish = project(tip), project(end)
            delta = [b-a for a, b in zip(start, finish)]
            length = math.hypot(*delta)
            assert length > 12, (axis, start, finish)
            return [round(a+d*.7) for a, d in zip(start, delta)], [round(d/length*28) for d in delta]

        (x, y), _ = handle(False, 0)
        send('input.pointer_drag', button='left', x=x, y=y, deltaX=0, deltaY=0, moveClient=True, holdMilliseconds=180)
        armed = state()
        assert not armed['divergence']['active'] and armed['divergence']['allocatedOwnerBytes'] == 0
        assert armed['predictionGeneration'] == stock['predictionGeneration'] and armed['predictionComplete']
        save('stationary-before-edit', armed)
        for index, (angular, axis) in enumerate(((False, 0), (True, 0), (True, 1), (True, 2))):
            if index == 1:
                click_bounds(ui_state()['headerLayoutBounds'])
                click_bounds(ui_state()['editorReplayTabBounds'])
                assert ui_state()['viewport'][0] > 100
                capture('replay-panel-open')
                send('input.pointer_drag', button='left', x=round(ui_state()['viewport'][0]+80), y=835, deltaX=0, deltaY=0, moveClient=True)
            if index == 2:
                click_bounds(ui_state()['causeTabBounds'])
                capture('both-panels-open')
                send('input.pointer_drag', button='right', x=1400, y=300, deltaX=-30, deltaY=10, moveClient=True)
            (x, y), (dx, dy) = handle(angular, axis)
            field = 'angularVelocity' if angular else 'linearVelocity'
            before = body_state()
            generation = state()['predictionGeneration'] if original is not None else 0
            # The first press must edit immediately; no preparatory click.
            request = connection.send('input.pointer_drag', dict(button='left', x=x, y=y,
                                      deltaX=dx, deltaY=dy, moveClient=True, holdAfterMoveMilliseconds=1500))
            time.sleep(0.3)
            held = reader.latest()['payload']
            changed = body_state()
            save(f'held-{index}', held)
            save(f'velocity-held-{index}', changed)
            assert changed[field][axis] > before[field][axis] + .01, (field, axis, before, changed, (x,y,dx,dy))
            for other in range(3):
                if other != axis: assert changed[field][other] == before[field][other]
            assert held['predictionGeneration'] == generation
            assert not held['predictionGenerationPermitted'] and not held['divergence']['redReady']
            if original is None:
                original = held['divergence']['blueBodies']
                assert original and any(row['id'] == 7 for row in original)
            assert held['divergence']['blueBodies'] == original
            capture(f'held-{index}')
            assert connection.wait(request)['status'] == 'applied'
            released = ready(lambda row: row['divergence']['redReady'])
            assert released['predictionGeneration'] == generation + 1, 'release must schedule exactly one build'
            save(f'released-{index}', released)
        (x, y), _ = handle(True, 2)
        generation = state()['predictionGeneration']
        send('input.pointer_drag', button='left', x=x, y=y, deltaX=0, deltaY=0, moveClient=True, holdMilliseconds=180)
        assert state()['predictionGeneration'] == generation, 'stationary click scheduled a prediction'
        stationary = ready(lambda row: row['divergence']['redReady'])
        assert stationary['predictionComplete'] and not stationary['predictionDirty']
        assert stationary['predictionGenerationPermitted']
        assert stationary['predictionGeneration'] == generation
        save('stationary-click-still-ready', stationary)
        send('replay.scrub', normalized=1.0)
        compared = state()
        blue = {row['id']: row for row in compared['divergence']['blueBodies']}
        red = {row['id']: row for row in compared['divergence']['redBodies']}
        assert blue.keys() == red.keys() and 7 in blue
        assert vector_distance(blue[7]['position'], red[7]['position']) > 1
        assert compared['divergence']['blueFrame'] == compared['divergence']['redFrame'] > 0
        save('divergent-futures', compared)
        capture('divergent-futures')
        send('input.pointer_drag', button='left', x=round(ui_state()['viewport'][0]+80), y=870, deltaX=0, deltaY=0, moveClient=True)
        lab = send('comparison.state')['result']['comparison']
        assert lab['active'] and lab['selected'] == 7 and all(lab['coverage']), lab
        send('comparison.seek', tick=180)
        lab = send('comparison.state')['result']['comparison']
        assert lab['distanceMetres'] > 0 or lab['angleDegrees'] > .1, lab
        save('solver-lab', lab)
        capture('solver-lab')
        send('comparison.play', direction=1)
        time.sleep(.15)
        assert send('comparison.state')['result']['comparison']['tick'] > 180
        send('input.set_key', key=27, down=True)
        send('run.step_frames', count=2)
        send('input.set_key', key=27, down=False)
        ready(lambda _: not ui_state()['panelsAnimating'])
        save('lab-exited-ui', ui_state())
        vx, vy, vw, vh = ui_state()['viewport']
        send('input.pointer_drag', button='left', x=round(vx+vw*.85), y=round(vy+vh*.2), deltaX=0, deltaY=0, moveClient=True)
        cancelled = state()
        assert not cancelled['divergence']['active'] and cancelled['divergence']['allocatedOwnerBytes'] == 0
        assert not cancelled['predictionEnabled'] and not cancelled['inspectionCameraActive']
        restored = body_state()
        assert restored['linearVelocity'] == [95, 0, 0] and restored['angularVelocity'] == [0, 0, 0], restored
        assert not send('comparison.state')['result']['comparison']['active']
        save('cancelled', cancelled)
        capture('cancelled-scene')
        # A long future leaves a real worker build to interrupt with the mouse.
        send('replay.set_prediction_horizon', seconds=120)
        send('prediction.select_target', name='path_striker_02')
        send('replay.set_prediction_enabled', enabled=True)
        ready(lambda row: row['predictionComplete'])
        send('replay.set_velocity_edit_enabled', enabled=True)
        assert not state()['divergence']['active']
        (x, y), (dx, dy) = handle(False, 0)
        send('input.pointer_drag', button='left', x=x, y=y, deltaX=dx, deltaY=dy, moveClient=True)
        building = state()
        assert building['predictionBuilding'] and not building['divergence']['redReady'], building
        save('interrupted-build', building)
        (x, y), (dx, dy) = handle(False, 0)
        request = connection.send('input.pointer_drag', dict(button='left', x=x, y=y,
                                  deltaX=-dx, deltaY=-dy, moveClient=True, holdAfterMoveMilliseconds=1000))
        time.sleep(.25)
        interrupted = state()
        assert not interrupted['predictionBuilding'] and not interrupted['predictionGenerationPermitted']
        assert interrupted['predictionGeneration'] == building['predictionGeneration']
        save('editing-interrupted-build', interrupted)
        assert connection.wait(request)['status'] == 'applied'
        restarted = ready(lambda row: row['divergence']['redReady'])
        assert restarted['predictionGeneration'] == building['predictionGeneration'] + 1
        save('restarted-build', restarted)
        # A press without movement must resume the interrupted generation too.
        (x, y), (dx, dy) = handle(False, 0)
        send('input.pointer_drag', button='left', x=x, y=y, deltaX=dx, deltaY=dy, moveClient=True)
        building = state()
        assert building['predictionBuilding']
        before_stationary = body_state()['linearVelocity']
        (x, y), _ = handle(False, 0)
        request = connection.send('input.pointer_drag', dict(button='left', x=x, y=y,
                                  deltaX=0, deltaY=0, moveClient=True, holdMilliseconds=1000))
        time.sleep(.25)
        interrupted = state()
        assert not interrupted['predictionBuilding'] and not interrupted['predictionGenerationPermitted']
        assert interrupted['predictionGeneration'] == building['predictionGeneration']
        assert connection.wait(request)['status'] == 'applied'
        restarted = ready(lambda row: row['divergence']['redReady'])
        assert restarted['predictionGeneration'] == building['predictionGeneration'] + 1
        assert body_state()['linearVelocity'] == before_stationary
        save('stationary-release-resumes-build', restarted)
        vx, vy, vw, vh = ui_state()['viewport']
        send('input.pointer_drag', button='left', x=round(vx+vw*.85), y=round(vy+vh*.2), deltaX=0, deltaY=0, moveClient=True)
        assert not state()['divergence']['active']
        send('scene.load', name='at_rest.scene.json')
        send('prediction.select_target', name='ball_a')
        send('replay.set_prediction_enabled', enabled=True)
        resumed = ready(lambda row: row['predictionComplete'])
        assert not resumed['divergence']['active']
        save('raw-terrain-scene-replacement', resumed)
        # A quick release must survive building a previously absent original.
        send('scene.load', name='interaction_replay_prediction_harness.scene.json')
        send('prediction.select_target', name='path_striker_02')
        send('replay.set_prediction_enabled', enabled=False)
        send('replay.set_prediction_horizon', seconds=3)
        untouched = state()
        send('replay.set_velocity_edit_enabled', enabled=True)
        send('run.step_frames', count=30)
        armed = state()
        assert armed['predictionGeneration'] == untouched['predictionGeneration']
        assert not armed['predictionEnabled'] and not armed['divergence']['active']
        before_cold_edit = body_state()['linearVelocity'][0]
        (x, y), (dx, dy) = handle(False, 0)
        send('input.pointer_drag', button='left', x=x, y=y, deltaX=dx, deltaY=dy, moveClient=True)
        cold_edit = ready(lambda row: row['divergence']['redReady'])
        assert body_state()['linearVelocity'][0] > before_cold_edit + .01
        assert cold_edit['predictionGeneration'] == 1 and cold_edit['divergence']['blueFrameCount'] == 361
        assert next(row for row in cold_edit['divergence']['blueBodies'] if row['id'] == 7)['velocity'][0] == before_cold_edit
        save('released-before-original-ready', cold_edit)
        vx, vy, vw, vh = ui_state()['viewport']
        send('input.pointer_drag', button='left', x=round(vx+vw*.85), y=round(vy+vh*.2), deltaX=0, deltaY=0, moveClient=True)
        assert not state()['divergence']['active']
        send('replay.set_velocity_edit_enabled', enabled=True)
        send('input.pointer_drag', button='left', x=round(vx+vw*.85), y=round(vy+vh*.2), deltaX=0, deltaY=0, moveClient=True)
        assert not state()['divergence']['active'] and state()['divergence']['allocatedOwnerBytes'] == 0
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
    print('PASS: projected XYZ edits, interrupted builds, release prediction, paired Solver Lab and scene cancellation')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', type=Path, required=True)
    args = parser.parse_args()
    run(args.session.resolve())
