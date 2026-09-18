"""Check Split Future falling bodies, interaction, reset and rendering isolation."""

import argparse
import json
import math
import time
from pathlib import Path

from PIL import Image, ImageChops, ImageStat
from skarness import SkarnessConnection, launch

ROOT = Path(__file__).resolve().parents[1]
SCENE = ROOT / 'SkullbonezData/scenes/split_future.scene.json'


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def compare_world(before, after):
    # The small scene/frame status badge reports generation transitions. Compare
    # world pixels beneath it and the entire left region, not that changing text.
    a, b = Image.open(before).convert('RGB'), Image.open(after).convert('RGB')
    assert a.size == b.size == (1600, 1000)
    diff = ImageChops.difference(a, b)
    diff.paste((0, 0, 0), (1420, 35, 1600, 110))
    rms = max(ImageStat.Stat(diff).rms)
    diff.save(after.with_name(after.stem + '-difference.png'))
    assert rms < 0.3, f'World pixels changed after showcase: RMS {rms}'
    return rms


def run(session):
    assert launch(session, ROOT / 'Automation/SKULLBONEZ_CORE.exe',
                  ROOT / 'SkullbonezData/scenes/at_rest.scene.json',
                  hidden=True, fixed_step=True, layout_file=session / 'layout.preferences') == 0
    connection = SkarnessConnection(session)
    latest, offset = {}, 0

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
        result = {key: latest[key] for key in ('scene.state', 'camera.state', 'ui.presentation', 'selection.state')}
        write_json(session / (label + '.json'), result)
        send('capture.screenshot', path=str(session / (label + '.png')))
        return result

    def load(name):
        send('scene.load', name=name)

    def verify_showcase(snapshot, extra_objects=0):
        scene = json.loads(SCENE.read_text(encoding='utf-8'))
        assert (ROOT / snapshot['scene.state']['scenePath']).resolve() == SCENE
        assert snapshot['scene.state']['objectCount'] == len(scene['objects']) + extra_objects
        assert snapshot['scene.state']['physicsEnabled']
        assert snapshot['ui.presentation']['cinematicParameters'][2:6] == [22, 16, 14, 5]
        for expected in scene['objects'][:2]:
            actual = send('scene.object.resolve', sceneObjectId=expected['sceneObjectId'])['result']['objects'][0]
            assert actual['name'] == expected['name'] and not actual['fixed']
            assert all(abs(a - b) < 0.0001 for a, b in zip(actual['position'], expected['position']))
        assert snapshot['camera.state']['renderEye'] == scene['cameras'][0]['position']

    try:
        capabilities = send('capabilities.get')
        write_json(session / 'capabilities.json', capabilities)
        assert {'scene.load', 'scene.reset', 'capture.screenshot', 'scene.object.resolve', 'scene.object.select',
                'input.set_key', 'input.pointer_drag', 'run.step'} <= set(capabilities['commands'])
        send('state.subscribe', topics=[], detail='normal')
        send('window.resize', width=1600, height=1000)
        send('ui.animation_clock', seconds=0, enabled=True)
        comparisons = {}
        for label, control in [('ordinary', 'at_rest.scene.json'),
                               ('cinematic', 'concept_01_golden_hour_realism.scene.json')]:
            load(control)
            before = sample(label + '-before')
            load(SCENE.name)
            showcase = sample(label + '-showcase')
            verify_showcase(showcase)
            send('scene.reset')
            verify_showcase(sample(label + '-reset'))
            load(control)
            after = sample(label + '-after')
            for key in ('cinematicParameters', 'cinematicFeatures', 'ordinaryRenderParameters', 'cinematicShadows'):
                assert before['ui.presentation'][key] == after['ui.presentation'][key], (label, key)
            assert before['camera.state'] == after['camera.state']
            comparisons[label] = compare_world(session / (label + '-before.png'), session / (label + '-after.png'))
        load(SCENE.name)
        verify_showcase(sample('initial'))

        def bodies(label):
            result = [send('scene.object.resolve', sceneObjectId=identity)['result']['objects'][0]
                      for identity in (1101, 1102)]
            write_json(session / (label + '-bodies.json'), result)
            return result

        initial = bodies('initial')
        send('run.step', count=120)
        falling = bodies('falling')
        for before, after in zip(initial, falling):
            assert after['sceneObjectId'] == before['sceneObjectId']
            assert after['position'][1] < before['position'][1] - 5, after
            assert after['linearVelocity'][1] < -5, after
            assert abs(after['position'][0] - before['position'][0]) > 1, after
        sample('falling')
        send('run.step', count=180)
        bounced = bodies('bounced')
        assert bounced[0]['linearVelocity'][1] > 1, bounced[0]
        sample('bounced')
        send('run.step', count=180)
        landed = bodies('landed')
        for before, after, floor in zip(initial, landed, (29.0, 23.0)):
            assert floor < after['position'][1] < before['position'][1] - 10, after
        sample('landed')

        send('scene.object.select', scope='inspect', sceneObjectId=1101)
        selected = sample('selected')
        assert selected['selection.state']['pathTargetId'] == 1101
        send('scene.object.clear_selection', scope='inspect')

        def key(code):
            send('input.set_key', key=code, down=True)
            send('run.step_frames', count=2)
            send('input.set_key', key=code, down=False)
            send('run.step_frames', count=2)

        # Exercise player input, not a test-only object-spawn command: N opens
        # Launcher, M selects projectile mode, and the normal left click fires.
        key(ord('N'))
        key(ord('M'))
        catalog_before = send('scene.object.list')['result']['objects']
        send('input.pointer_drag', button='left', x=800, y=500, deltaX=0, deltaY=0)
        catalog_after = send('scene.object.list')['result']['objects']
        old_ids = {obj['sceneObjectId'] for obj in catalog_before}
        spawned = [obj for obj in catalog_after if obj['sceneObjectId'] not in old_ids]
        assert len(spawned) == 1, catalog_after
        identity = spawned[0]['sceneObjectId']
        projectile_before = send('scene.object.resolve', sceneObjectId=identity)['result']['objects'][0]
        send('run.step', count=30)
        projectile_after = send('scene.object.resolve', sceneObjectId=identity)['result']['objects'][0]
        assert not projectile_after['fixed']
        assert sum((a-b)**2 for a,b in zip(projectile_after['position'], projectile_before['position'])) > 1
        write_json(session / 'projectile.json', [projectile_before, projectile_after])
        sample('launched')
        key(ord('N'))
        send('scene.reset')
        verify_showcase(sample('reset-after-interaction'), extra_objects=1)
        # A scene reset restores poses and retains user-created objects. Reload
        # clears those objects before checking a pointer-driven pickup in isolation.
        load('at_rest.scene.json')
        load(SCENE.name)
        verify_showcase(sample('reload-clears-projectile'))
        mode = sample('before-manipulator')
        x, y, width, height = mode['ui.presentation']['cameraPopupBounds']
        send('input.pointer_drag', button='left', x=round(x+width/2), y=20, deltaX=0, deltaY=0)
        send('ui.animation_clock', seconds=1, enabled=True)
        opened = sample('camera-menu')
        assert opened['ui.presentation']['cameraPopupOpen']
        x, y, width, height = opened['ui.presentation']['cameraPopupBounds']
        send('input.pointer_drag', button='left', x=round(x+width/2),
             y=round(y+height*5.5/7), deltaX=0, deltaY=0)
        send('ui.animation_clock', seconds=2, enabled=True)
        mode = sample('manipulator-mode')
        assert mode['ui.presentation']['cameraMode'] == 5
        eye = mode['camera.state']['renderEye']
        view = mode['camera.state']['renderView']
        forward = [v-e for v,e in zip(view, eye)]
        length = math.sqrt(sum(v*v for v in forward))
        forward = [v/length for v in forward]
        # This fixture's authored camera has no roll and looks along the Z axis.
        assert abs(forward[0]) < 0.0001
        before_drag = bodies('before-drag')[0]
        relative = [p-e for p,e in zip(before_drag['position'], eye)]
        depth = sum(p*f for p,f in zip(relative, forward))
        up_distance = -forward[2]*relative[1] + forward[1]*relative[2]
        sx, sy = mode['ui.presentation']['projectionScale']
        x = round(800 + relative[0]*sx/depth*800)
        y = round(500 - up_distance*sy/depth*500)
        send('run.resume')
        send('input.pointer_drag', button='left', x=x, y=y, deltaX=160, deltaY=-100,
             moveClient=True, rawInput=False, movementFrames=30,
             holdMilliseconds=100, holdAfterMoveMilliseconds=500)
        send('run.pause')
        after_drag = bodies('after-drag')[0]
        assert after_drag['sceneObjectId'] == 1101
        assert after_drag['position'][0] > before_drag['position'][0] + 12, after_drag
        sample('dragged')
        load('at_rest.scene.json')
        load(SCENE.name)
        verify_showcase(sample('ready'))
        send('run.resume')
        time.sleep(10.1)
        send('run.pause')
        sample('final')
        for actual, floor in zip(bodies('final'), (29.0, 23.0)):
            assert actual['position'][1] > floor, actual
        result = {'passed': True, 'worldPixelRmsAfterRoundTrip': comparisons,
                  'heroIds': [1101, 1102], 'sceneOnlyStyles': [22, 16, 14, 5],
                  'physicsDropAndContact': True, 'selectionId': 1101, 'pointerDraggedBodyId': 1101,
                  'launchedProjectileId': identity, 'resetRestoresAuthoredBodies': True}
        write_json(session / 'result.json', result)
        print(json.dumps(result, indent=2), flush=True)
    finally:
        try:
            send('session.stop')
        finally:
            connection.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', type=Path, default=ROOT / 'TestOutput/skarness/split-future-interactive')
    args = parser.parse_args()
    run(args.session.resolve())
