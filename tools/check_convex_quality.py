"""Assert exact CCD outcomes from the permanent identity-bound hull scene captures."""
from __future__ import annotations
import argparse
import copy
import json
import math
from pathlib import Path


def first_tick(directory):
    rows = []
    with (directory / 'physics.physicsdiag.ndjson').open(encoding='utf-8') as stream:
        for line in stream:
            row = json.loads(line)
            if row.get('frame', 0) > 0: break
            rows.append(row)
    identities = {}
    for path in directory.glob('body-*.json'):
        for obj in json.loads(path.read_text())['result']['objects']:
            identities[obj['modelRow']] = obj
    return rows, identities


def check_ccd(rows, identities, near_miss):
    stages = next(r for r in rows if r['kind'] == 'pipeline_stages')
    states = {r['body_id']: r for r in rows if r['kind'] == 'body'}
    movers = {row: obj for row, obj in identities.items() if obj['name'].startswith('mover_')}
    assert len(movers) == 3
    assert all(states[row]['shape'] == 'convex_hull' for row in movers)
    contacts = [r for r in rows if r['kind'] == 'contact']
    if near_miss:
        assert stages['swept_object_hit'] == 0, 'A loose-radius near miss published a swept hit'
        assert stages['swept_object_miss'] == 3
        assert not contacts, 'A near miss created contact response'
        for row in movers:
            assert abs(states[row]['pos'][0] - 530) < 0.0001, 'Near miss consumed translation time'
            assert states[row]['vel'][0] == 6000, 'Near miss changed mover velocity'
    else:
        assert stages['swept_object_hit'] == 3, 'A genuine crossing missed exact swept contact'
        for row, obj in movers.items():
            target_name = obj['name'].replace('mover_', 'target_')
            target = next(k for k, v in identities.items() if v['name'] == target_name)
            assert any({c['body_a'], c['body_b']} == {row, target} and c['normal_impulse'] > 0 for c in contacts), 'Crossing lacks identity-bound response'
            assert states[row]['vel'][0] < 0, 'Mover passed through its stationary target'


def check_retained_stack(stack):
    assert stack['layerOrderRetained'], 'Stack layer order was lost'
    assert 0.95 <= stack['heightRetention'] <= 1.05, 'Stack collapsed or expanded'
    assert stack['maximumHorizontalDrift'] < 1.5, 'Stack crept beyond its footprint budget'
    assert stack['allSleeping'], 'Stable stack failed to settle'


def check_matrix(capture):
    results = json.loads((capture / 'results.json').read_text())
    assert len(results) == 36
    by_name = {Path(row['scene']).name: row for row in results}
    assert len(by_name) == 36
    for row in results:
        bodies = row['metrics']['bodies']
        assert bodies and len({body['sceneObjectId'] for body in bodies}) == len(bodies)
        for body in bodies:
            assert body['samplesLastFrame'] == row['ticks'] - 1
            assert all(math.isfinite(v) for v in body['finalPosition'] + body['finalOrientation'])
    for surface in ('flat', 'shallow_x', 'shallow_z', 'compound', 'basin', 'ridges'):
        for motion in ('rest', 'slide'):
            row = by_name[f'convex_quality_{surface}_{motion}.scene.json']
            assert all(b['finalSleeping'] and b['finalSpeed'] == 0 for b in row['metrics']['bodies']), (surface, motion)
    for surface in ('flat', 'shallow_x', 'ridges'):
        for stack in by_name[f'convex_quality_{surface}_stack.scene.json']['metrics']['stacks']:
            check_retained_stack(stack)
    for surface in ('flat', 'steps'):
        stacks = by_name[f'convex_quality_{surface}_tall_mixed_stack.scene.json']['metrics']['stacks']
        check_retained_stack(next(s for s in stacks if s['name'] == 'tower_0'))
    # Sleep alone must not turn a collapsed tower into an accepted stack.
    corrupted = copy.deepcopy(next(s for s in stacks if s['name'] == 'tower_0'))
    corrupted.update(heightRetention=0.02, layerOrderRetained=False, allSleeping=True)
    try: check_retained_stack(corrupted)
    except AssertionError: pass
    else: raise AssertionError('Sleeping-collapse negative control escaped')
    print('PASS 36 identity-bound finite scenes; broad rest/slide, three-level stacks, flat/steps five-high box hulls')
    print('Tall sloped, elongated and mixed towers remain measured solver-plan follow-up; sleeping collapse is rejected')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--matrix', action='store_true')
    args = parser.parse_args()
    for suffix in ('near_miss', 'thin_target', 'narrow_window', 'swapped'):
        rows, identities = first_tick(args.capture / f'convex_quality_ccd_{suffix}.scene')
        check_ccd(rows, identities, suffix == 'near_miss')
        # The same acceptance oracle must reject planted false publication and
        # missing response, rather than only checking non-empty render geometry.
        corrupted = copy.deepcopy(rows)
        if suffix == 'near_miss':
            next(r for r in corrupted if r['kind'] == 'pipeline_stages')['swept_object_hit'] = 1
        else:
            corrupted = [r for r in corrupted if r['kind'] != 'contact']
        try: check_ccd(corrupted, identities, suffix == 'near_miss')
        except AssertionError: pass
        else: raise AssertionError('CCD negative control escaped the acceptance oracle')
        print('PASS', suffix, 'including planted negative control')
    if args.matrix: check_matrix(args.capture)


if __name__ == '__main__': main()
