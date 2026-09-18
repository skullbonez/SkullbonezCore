"""Capture identical SMAA off/on frames and check its graph, edges and resize path."""
import argparse
import json
import os
from pathlib import Path

from PIL import Image, ImageChops, ImageStat
from skarness import SkarnessConnection, launch

ROOT = Path(__file__).resolve().parents[1]


def capture(session, mode):
    previous = os.environ.get('SKULLBONEZ_SMAA')
    os.environ['SKULLBONEZ_SMAA'] = mode
    try:
        assert launch(session, ROOT/'Automation/SKULLBONEZ_CORE.exe',
                      ROOT/'SkullbonezData/scenes/split_future.scene.json',
                      hidden=True, fixed_step=True) == 0
    finally:
        if previous is None:
            os.environ.pop('SKULLBONEZ_SMAA', None)
        else:
            os.environ['SKULLBONEZ_SMAA'] = previous
    connection = SkarnessConnection(session)

    def send(command, **args):
        reply = connection.wait(connection.send(command, args))
        assert reply.get('status') == 'applied', (command, reply)
        return reply

    try:
        capabilities = send('capabilities.get')
        (session/'capabilities.json').write_text(json.dumps(capabilities, indent=2))
        send('state.subscribe', topics=[], detail='normal')
        send('ui.animation_clock', seconds=0, enabled=True)
        for label, width, height, ticks in [('initial', 1600, 1000, 0),
                                             ('moving', 1600, 1000, 80),
                                             ('resized', 1000, 700, 0)]:
            send('window.resize', width=width, height=height)
            if ticks:
                send('run.step', count=ticks)
            send('run.step_frames', count=3)
            send('capture.screenshot', path=str(session/(label+'.png')))
            graph = (ROOT/'Debug/dx12_cinematic_post_graph.txt').read_text()
            (session/(label+'-graph.txt')).write_text(graph)
            passes = ['ToneMapPass', 'SmaaEdgePass', 'SmaaWeightPass', 'SmaaBlendPass']
            if mode == 'on':
                offsets = [graph.index(name+' queue=') for name in passes]
                assert offsets == sorted(offsets)
                assert f'format=RGBA8 size={width}x{height}' in graph
                assert 'materialization_failed=false' in graph
            else:
                assert 'SmaaEdgePass' not in graph
            image = Image.open(session/(label+'.png')).convert('RGB')
            assert image.size == (width, height)
            assert max(ImageStat.Stat(image).stddev) > 25, 'Missing world image'
    finally:
        send('session.stop')
        connection.close()


def run(session):
    for mode in ('off', 'on'):
        capture(session/mode, mode)
    a = Image.open(session/'off/initial.png').convert('RGB')
    b = Image.open(session/'on/initial.png').convert('RGB')
    difference = ImageChops.difference(a, b)
    difference.save(session/'difference.png')
    # This authored camera places the slanted box edge here. Require actual
    # coverage changes at the silhouette while retaining interior coating detail.
    edge = ImageStat.Stat(difference.crop((880, 255, 1180, 300)))
    interior = ImageStat.Stat(difference.crop((930, 340, 970, 460)))
    assert max(edge.rms) > 1, 'SMAA did not affect the box silhouette'
    assert max(interior.mean) < 1.5, 'SMAA blurred the coating away from edges'
    result = {'passed': True, 'edgeRms': edge.rms,
              'interiorMeanAbsoluteDifference': interior.mean,
              'passes': ['edge detection', 'blend weights', 'neighborhood blend'],
              'sizes': [[1600, 1000], [1000, 700]]}
    (session/'result.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', type=Path, required=True)
    run(parser.parse_args().session.resolve())
