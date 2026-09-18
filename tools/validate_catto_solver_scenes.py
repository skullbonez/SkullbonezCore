"""Validate Catto authored geometry and load/advance its scenes through Skarness.

This checks scene integrity and runnable finite simulation, not solver quality
or equivalence to Catto's 2D results. Collapsing stress cases remain evidence.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
from pathlib import Path

from generate_catto_solver_scenes import build
from skarness import SkarnessConnection, launch

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / 'SkullbonezData/scenes/catto_solver_manifest.json'


def write(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False)+'\n', encoding='utf-8')


def check_files():
    files, manifest = build()
    for path, expected in files.items():
        assert path.read_text(encoding='utf-8-sig') == expected, path
    for case in manifest['cases']:
        data = json.loads((ROOT / case['scene']).read_text())
        assert data['playback']['pauseSnapshotState'] is False
        bodies = data['objects']
        assert data['simulation'].get('modelCapacity', 4000) >= case['bodies']
        names = {v['name'] for v in bodies}
        assert len(names) == len(bodies) == len({v['sceneObjectId'] for v in bodies}) == case['bodies']
        assert all(v['mass'] > 0 and all(math.isfinite(x) for x in v['position']) for v in bodies)
        assert all(j['bodyA'] in names and j['bodyB'] in names and j['bodyA'] != j['bodyB']
                   for j in data.get('ragdollJoints', []))
    # Independent source facts catch accidental fixture weakening in regeneration.
    by_slug = {Path(c['scene']).name: c for c in manifest['cases']}
    assert by_slug['catto_high_mass_ratio_1.scene.json']['dynamicBodies'] == 165
    assert by_slug['catto_vertical_stack.scene.json']['dynamicBodies'] == 15
    assert by_slug['catto_pyramid_20.scene.json']['dynamicBodies'] == 210
    assert by_slug['catto_pyramid_100.scene.json']['dynamicBodies'] == 5050
    assert by_slug['catto_arch.scene.json']['dynamicBodies'] == 21
    assert by_slug['catto_bridge.scene.json']['joints'] == 161
    assert by_slug['catto_ball_and_chain.scene.json']['joints'] == 41
    assert by_slug['catto_stretched_chain.scene.json']['joints'] == 40
    assert by_slug['catto_confined.scene.json']['dynamicBodies'] == 625
    for number in (2, 3):
        data = json.loads((ROOT / f'SkullbonezData/scenes/catto_high_mass_ratio_{number}.scene.json').read_text())
        bodies = {v['name']: v for v in data['objects']}
        assert bodies['heavy_box']['mass']/bodies['support_left']['mass'] == 400
    assert len(manifest['cases']) == 21 and len(manifest['deferred']) == 6
    return manifest



def read_scene_states(session):
    states = {}
    # Full snapshots can be large. Stream once and decode only the owning topic;
    # offline verification starts after state streaming has been disabled.
    with (session / 'runtime.skarness.ndjson').open(encoding='utf-8') as trace:
        for line in trace:
            if '"scene.state"' not in line or not line.rstrip().endswith('}'):
                continue
            event = json.loads(line)
            if event.get('topic') != 'scene.state':
                continue
            value = event['payload']
            if 'scenePath' in value:
                states[value['scenePath'].replace('\\', '/').lower()] = value
    return states


def observed_scene(states, path, body_count):
    expected = path.resolve()
    for key in (expected.as_posix().lower(), expected.relative_to(ROOT).as_posix().lower()):
        if key in states:
            value = states[key]
            assert value['objectCount'] == value['physicsBodyCount'] == body_count, value
            return value
    raise AssertionError(('Missing scene.state evidence', path))


def run(session, selected, stress, ticks):
    manifest = check_files()
    cases = [c for c in manifest['cases'] if (stress or not c['stress']) and
             (not selected or selected in c['scene'])]
    assert cases, 'No matching cases'
    assert not session.exists(), 'Use a fresh session directory to preserve evidence'
    executable = ROOT / 'Automation/SKULLBONEZ_CORE.exe'
    assert launch(session, executable, ROOT / cases[0]['scene'], hidden=True, fixed_step=True,
                  worker_threads=4) == 0
    connection = SkarnessConnection(session)
    results = []

    def send(command, **arguments):
        reply = connection.wait(connection.send(command, arguments))
        with (session / 'commands.ndjson').open('a', encoding='utf-8') as trace:
            trace.write(json.dumps(dict(command=command, arguments=arguments, response=reply), allow_nan=False)+'\n')
        assert reply.get('status') == 'applied', (command, reply)
        return reply

    def resolve(expected):
        value = send('scene.object.resolve', sceneObjectId=expected['sceneObjectId'])['result']['objects'][0]
        assert value['sceneObjectId'] == expected['sceneObjectId'] and value['name'] == expected['name']
        assert value['fixed'] == expected['fixed']
        assert all(math.isfinite(x) for key in ('position', 'linearVelocity', 'angularVelocity') for x in value[key])
        return value

    try:
        capabilities = send('capabilities.get')
        write(session / 'capabilities.json', capabilities)
        assert {'scene.load', 'scene.object.list', 'scene.object.resolve', 'run.step', 'run.step_frames', 'capture.screenshot',
                'state.subscribe', 'replay.set_recording_enabled', 'session.stop'} <= set(capabilities['commands'])
        send('state.subscribe', topics=['scene.state'], detail='summary')
        send('window.resize', width=1280, height=800)
        for case in cases:
            path = ROOT / case['scene']
            label = path.name.removesuffix('.scene.json')
            authored = json.loads(path.read_text())
            send('scene.load', name=path.name)
            # Dense contact fixtures exceed the unrelated replay snapshot cap.
            send('replay.set_recording_enabled', enabled=False)
            # The nonblocking native pipe cannot publish the 5,084-row catalog
            # in one response. Resolve each known identity through its bounded API.
            if case['stress']:
                catalog = [resolve(value) for value in authored['objects']]
            else:
                catalog = send('scene.object.list')['result']['objects']
            assert len(catalog) == case['bodies'], (label, len(catalog), case['bodies'])
            assert {(v['sceneObjectId'], v['name']) for v in catalog} == {
                (v['sceneObjectId'], v['name']) for v in authored['objects']}, label
            dynamic = [v for v in authored['objects'] if not v['fixed']]
            sample_ids = {0, len(dynamic)//2, len(dynamic)-1}
            sample_ids |= {i for i, v in enumerate(dynamic) if v['name'] in ('heavy_box', 'heavy_ball', 'falling_box')}
            samples = [dynamic[i] for i in sorted(sample_ids)]
            before = [resolve(v) for v in samples]
            for expected, actual in zip(samples, before):
                assert max(abs(a-b) for a, b in zip(expected['position'], actual['position'])) < 0.08, (label, expected, actual)
            send('run.step_frames', count=2)
            send('capture.screenshot', path=str(session / (label+'-initial.png')))
            # Small chunks keep evidence/control responsive on the large-pyramid case.
            remaining = ticks
            while remaining:
                count = min(remaining, 30)
                send('run.step', count=count)
                remaining -= count
            after = [resolve(v) for v in samples]
            if label == 'catto_single_box' and ticks >= 60:
                assert after[0]['position'][1] < before[0]['position'][1]-1, after
                assert after[0]['position'][1] > 159, after
            send('run.step_frames', count=2)
            send('capture.screenshot', path=str(session / (label+'-after.png')))
            result = dict(scene=case['scene'], bodies=len(catalog), authoredJoints=case['joints'],
                          ticks=ticks, sceneSha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                          samplesBefore=before, samplesAfter=after,
                          maximumSampleDisplacement=max(math.dist(a['position'], b['position']) for a, b in zip(before, after)))
            results.append(result)
            write(session / 'results.json', dict(ok=False, completed=results))
            print(f'PASS {label}: {len(catalog)} bodies, {ticks} ticks', flush=True)
        # Stop state streaming before offline trace parsing; an idle client can
        # fill the native nonblocking pipe while reading a large trace.
        send('state.subscribe', topics=[], detail='summary')
        states = read_scene_states(session)
        for result in results:
            result['observedScene'] = observed_scene(states, ROOT / result['scene'], result['bodies'])
        write(session / 'results.json', dict(ok=True, executableSha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                                           sourceRevision=manifest['sourceRevision'], completed=results,
                                           scope='Catalog identities, sampled initial poses/finite motion, screenshots. Not a solver stability certification.'))
    finally:
        active_failure = sys.exc_info()[0] is not None
        try:
            send('session.stop')
        except (OSError, RuntimeError) as error:
            write(session / 'shutdown-error.json', dict(error=str(error)))
            if not active_failure:
                raise
        finally:
            connection.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--session', type=Path)
    parser.add_argument('--select')
    parser.add_argument('--include-stress', action='store_true')
    parser.add_argument('--ticks', type=int, default=120)
    args = parser.parse_args()
    assert args.ticks > 0
    if args.check:
        manifest = check_files()
        print(f'PASS: {len(manifest["cases"])} authored scenes, source counts, joint references and mass ratios')
    else:
        assert args.session, '--session is required for native validation'
        run(args.session.resolve(), args.select, args.include_stress, args.ticks)


if __name__ == '__main__':
    main()
