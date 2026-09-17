"""Check showcase identity, reset and ordinary/cinematic isolation through Skarness."""

import argparse
import json
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
                  hidden=True, layout_file=session / 'layout.preferences') == 0
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
        result = {key: latest[key] for key in ('scene.state', 'camera.state', 'ui.presentation')}
        write_json(session / (label + '.json'), result)
        send('capture.screenshot', path=str(session / (label + '.png')))
        return result

    def load(name):
        send('scene.load', name=name)

    def verify_showcase(snapshot):
        scene = json.loads(SCENE.read_text(encoding='utf-8'))
        assert (ROOT / snapshot['scene.state']['scenePath']).resolve() == SCENE
        assert snapshot['scene.state']['objectCount'] == len(scene['objects'])
        assert not snapshot['scene.state']['physicsEnabled']
        assert snapshot['ui.presentation']['cinematicParameters'][2:6] == [22, 16, 14, 5]
        for expected in scene['objects'][:2]:
            actual = send('scene.object.resolve', sceneObjectId=expected['sceneObjectId'])['result']['objects'][0]
            assert actual['name'] == expected['name'] and actual['fixed']
            assert all(abs(a - b) < 0.0001 for a, b in zip(actual['position'], expected['position']))
        assert snapshot['camera.state']['renderEye'] == scene['cameras'][0]['position']

    try:
        capabilities = send('capabilities.get')
        write_json(session / 'capabilities.json', capabilities)
        assert {'scene.load', 'scene.reset', 'capture.screenshot', 'scene.object.resolve'} <= set(capabilities['commands'])
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
        send('run.resume')
        time.sleep(10.1)
        send('run.pause')
        verify_showcase(sample('final'))
        result = {'passed': True, 'worldPixelRmsAfterRoundTrip': comparisons,
                  'heroIds': [1101, 1102], 'sceneOnlyStyles': [22, 16, 14, 5]}
        write_json(session / 'result.json', result)
        print(json.dumps(result, indent=2), flush=True)
    finally:
        try:
            send('session.stop')
        finally:
            connection.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', type=Path, default=ROOT / 'TestOutput/skarness/split-future-final')
    args = parser.parse_args()
    run(args.session.resolve())
