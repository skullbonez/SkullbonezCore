"""Check every captured solver tick and replay byte against the ragdoll control."""
import argparse
import json
from pathlib import Path
from physics_ab_capture import digest, write_json


def samples(path):
    result = {'solverHashes': {}, 'eventCounts': {}}
    with path.open(encoding='utf-8') as stream:
        for line in stream:
            row = json.loads(line)
            if row.get('topic') != 'replay.timeline': continue
            payload = row['payload']
            tick = payload.get('solver', {}).get('nextFrame', 0)
            if tick <= 0: continue
            key = str(tick)
            state = payload['solver']['latestHash']
            events = payload['events']['totalCaptured']
            if key in result['solverHashes']:
                assert result['solverHashes'][key] == state, f'Solver tick {tick} changed without advancement'
                assert result['eventCounts'][key] == events, f'Events changed at fixed tick {tick}'
            result['solverHashes'][key] = state
            result['eventCounts'][key] = events
    return result


def compare(expected, actual):
    for topic in ('solverHashes', 'eventCounts'):
        assert expected[topic].keys() == actual[topic].keys(), f'Missing or extra {topic} ticks'
        for tick, value in expected[topic].items():
            assert value == actual[topic][tick], f'{topic} differs at tick {tick}'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--control', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--label', default='after')
    args = parser.parse_args()
    expected = json.loads((args.control / 'exact-state-control.json').read_text())
    actual = samples(args.candidate / args.label / 'runtime.skarness.ndjson')
    assert set(actual['solverHashes']) == {str(i) for i in range(1, expected['ticks']+1)}
    compare(expected['samples'], actual)
    old_hash = digest(args.control / 'before.skreplay')
    new_hash = digest(args.candidate / (args.label + '.skreplay'))
    assert old_hash == new_hash, 'Replay bytes diverged from preservation control'
    corrupted = {topic: dict(rows) for topic, rows in actual.items()}
    corrupted['solverHashes']['1200'] ^= 1
    try: compare(expected['samples'], corrupted)
    except AssertionError: pass
    else: raise AssertionError('Planted joint/solver-state mutation escaped the comparison')
    result = {'ticks': expected['ticks'], 'solverAndEventsExact': True, 'replaySha256': new_hash,
              'plantedSolverMutationRejected': True}
    write_json(args.candidate / (args.label + '.preservation.json'), result)
    print(json.dumps(result))


if __name__ == '__main__': main()
