"""Load or measure every permanent hull-quality case through Skarness."""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path
from physics_ab_capture import CaptureSide, write_json

ROOT = Path(__file__).resolve().parents[1]


def add_contact(contacts, previous_features, row, key):
    c = contacts.setdefault(key, {'rows': 0, 'warmStartedRows': 0, 'unsupportedRows': 0,
                                 'unsupportedSlidingWithoutFrictionRows': 0,
                                 'normalImpulseSum': 0.0, 'tangentImpulseSum': 0.0,
                                 'persistentFeatureRows': 0, 'maxPenetration': 0.0})
    c['rows'] += 1
    c['warmStartedRows'] += int(row['warm_started'])
    c['unsupportedRows'] += int(not row['supports_sleep'])
    c['unsupportedSlidingWithoutFrictionRows'] += int(not row['supports_sleep'] and row['slip_speed'] > 0.1 and abs(row['tangent_impulse']) < 1e-9)
    c['normalImpulseSum'] += row['normal_impulse']
    c['tangentImpulseSum'] += abs(row['tangent_impulse'])
    c['maxPenetration'] = max(c['maxPenetration'], row['penetration'])
    feature = (key, row['body_a'], row['body_b'], row['feature_id'])
    c['persistentFeatureRows'] += int(previous_features.get(feature) == row['frame'] - 1)
    previous_features[feature] = row['frame']


def summarize(trace: Path):
    bodies = {}
    kinds = {}
    contacts = {}
    previous_features = {}
    decisions = {}
    energy = {"maximumKineticEnergy": 0.0, "finalKineticEnergy": 0.0, "massWeightedCorrectionSum": 0.0, "maximumMassWeightedCorrection": 0.0}
    with trace.open(encoding='utf-8') as stream:
        for line in stream:
            if not line.endswith('\n'): continue
            row = json.loads(line)
            kind = row.get('kind', '')
            kinds[kind] = kinds.get(kind, 0) + 1
            if kind == 'sleep_decision':
                counts = decisions.setdefault(row['body_id'], {})
                reason = str(row['reset_reason'])
                counts[reason] = counts.get(reason, 0) + 1
            if kind == 'frame':
                energy['maximumKineticEnergy'] = max(energy['maximumKineticEnergy'], row['total_energy'])
                energy['finalKineticEnergy'] = row['total_energy']
            # Solver diagnostics measure displacement before inverse-mass
            # application (mass times distance), not a body's travel distance.
            if kind == 'solver_stats':
                energy['massWeightedCorrectionSum'] += row['position_correction_total']
                energy['maximumMassWeightedCorrection'] = max(energy['maximumMassWeightedCorrection'], row['position_correction_max'])
            if kind == 'contact':
                # Attribute a contact to both participants. Summing these body
                # totals would double-count object pairs; they are per-body data.
                for key in {row['body_a'], row['body_b']} - {-1}:
                    add_contact(contacts, previous_features, row, key)
            if kind != 'body': continue

            identity = (row['body_id'], row['name'])
            b = bodies.setdefault(identity, {'id': identity[0], 'name': identity[1], 'first': row,
                                            'last': row, 'slipPath': 0.0, 'wakeTransitions': 0,
                                            'firstSleepFrame': None, 'lastMovingFrame': None,
                                            'maxAngularSpeed': 0.0, 'sleepCounterResets': 0, 'sleepInhibitedFrames': 0})
            previous = b['last']
            b['slipPath'] += math.hypot(row['pos'][0] - previous['pos'][0], row['pos'][2] - previous['pos'][2])
            b['wakeTransitions'] += int(previous['sleeping'] and not row['sleeping'])
            b['sleepCounterResets'] += int(row['sleep_counter'] < previous['sleep_counter'] and not row['sleeping'])
            b['sleepInhibitedFrames'] += int(row['sleep_inhibited'])
            if row['sleeping'] and b['firstSleepFrame'] is None: b['firstSleepFrame'] = row['frame']
            if row['speed'] > 0.01 or row['omega_mag'] > 0.01: b['lastMovingFrame'] = row['frame']
            b['maxAngularSpeed'] = max(b['maxAngularSpeed'], row['omega_mag'])
            b['last'] = row
    result = []
    for b in bodies.values():
        first, last = b.pop('first'), b.pop('last')
        b.update(initialPosition=first['pos'], finalPosition=last['pos'], finalSleeping=last['sleeping'],
                 finalSpeed=last['speed'], finalAngularSpeed=last['omega_mag'], samplesLastFrame=last['frame'],
                 finalOrientation=last['q'], initialOrientation=first['q'])
        b['contacts'] = contacts.get(b['id'], {})
        b['sleepResetReasons'] = decisions.get(b['id'])
        result.append(b)
    return {'bodies': result, 'recordKinds': kinds, 'energy': energy}


def stack_metrics(bodies):
    """Measure each authored tower without declaring a collapsed sleeping pile stable."""
    groups = {}
    for body in bodies:
        name = body['name']
        if name.startswith('tower_'):
            _, lane, level, *_ = name.split('_')
            groups.setdefault('tower_' + lane, []).append((int(level), body))
        elif name.rsplit('_', 1)[-1].isdigit():
            lane, level = name.rsplit('_', 1)
            groups.setdefault(lane, []).append((int(level), body))
    result = []
    for name, members in groups.items():
        members.sort(key=lambda value: value[0])
        base, top = members[0][1], members[-1][1]
        initial_span = top['initialPosition'][1] - base['initialPosition'][1]
        final_span = top['finalPosition'][1] - base['finalPosition'][1]
        result.append({'name': name, 'members': [b['sceneObjectId'] for _, b in members],
                       'levels': len(members), 'initialCentreHeightSpan': initial_span,
                       'finalCentreHeightSpan': final_span,
                       'heightRetention': final_span / initial_span if initial_span > 0 else None,
                       'allSleeping': all(b['finalSleeping'] for _, b in members),
                       'layerOrderRetained': all(a[1]['finalPosition'][1] < b[1]['finalPosition'][1]
                                                 for a, b in zip(members, members[1:])),
                       'maximumHorizontalDrift': max(math.hypot(b['finalPosition'][0] - b['initialPosition'][0],
                                                               b['finalPosition'][2] - b['initialPosition'][2])
                                                     for _, b in members)})
    return result


def run(output: Path, smoke: bool, selected: str | None, executable: Path):
    matrix = json.loads((ROOT / 'SkullbonezData/scenes/convex_quality_matrix.json').read_text())
    cases = [c for c in matrix['cases'] if selected is None or selected in c['scene']]
    assert cases
    output.mkdir(parents=True, exist_ok=False)
    results = []
    for case in cases:
        scene = ROOT / case['scene']
        directory = output / scene.stem
        side = CaptureSide(executable, directory, scene)
        try:
            capabilities = side.start(worker_threads=4, allocation_guard='gameplay')
            assert {'run.pause', 'run.step', 'scene.object.resolve', 'capture.screenshot'}.issubset(capabilities)
            side.command('state.subscribe', {'topics': [], 'detail': 'summary'})
            side.command('run.pause')
            side.command('replay.set_prediction_enabled', {'enabled': False})
            side.command('replay.set_recording_enabled', {'enabled': False})
            side.command('scene.reset')
            authored = json.loads(scene.read_text())
            identities = {}
            for obj in authored['objects']:
                receipt = side.command('scene.object.resolve', {'sceneObjectId': obj['sceneObjectId']})
                # Persist the owning live identity, rather than accepting geometry
                # from a previous scene generation as a successful scene load.
                write_json(directory / f"body-{obj['sceneObjectId']}.json", receipt)
                live = receipt['result']['objects']
                assert len(live) == 1 and live[0]['sceneObjectId'] == obj['sceneObjectId'] and live[0]['name'] == obj['name'], receipt
                identities[live[0]['modelRow']] = {'name': obj['name'], 'sceneObjectId': obj['sceneObjectId']}
            ticks = 2 if smoke else case['ticks']
            for first in range(0, ticks, 120): side.command('run.step', {'count': min(120, ticks - first)})
            side.command('capture.screenshot', {'path': str((directory / 'final.png').resolve())})
        finally:
            side.stop()
        metrics = summarize(directory / 'physics.physicsdiag.ndjson')
        assert len(metrics['bodies']) == len(case['bodies']), (scene, metrics)
        assert {b['id'] for b in metrics['bodies']} == set(identities)
        for b in metrics['bodies']:
            b.update(identities[b['id']])
        assert {b['name'] for b in metrics['bodies']} == set(case['bodies'])
        metrics['stacks'] = stack_metrics(metrics['bodies']) if case['family'] == 'stack' else []
        write_json(directory / 'metrics.json', metrics)
        results.append({'scene': case['scene'], 'ticks': ticks, 'metrics': metrics})
        write_json(output / 'results.json', results)
        print('PASS', scene.name, 'ticks', ticks, 'bodies', len(metrics['bodies']), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--smoke', action='store_true')
    parser.add_argument('--case')
    parser.add_argument('--exe', type=Path, default=ROOT / 'Automation/SKULLBONEZ_CORE.exe')
    args = parser.parse_args()
    run(args.output.resolve(), args.smoke, args.case, args.exe.resolve())


if __name__ == '__main__': main()
