"""Negative controls for the native hull quality measurements."""
import copy
import unittest
from measure_convex_quality import stack_metrics
from generate_convex_quality_scenes import build


class HullQualityMeasurements(unittest.TestCase):
    def test_sleeping_collapse_does_not_retain_a_stack(self):
        bodies = [{'name': f'tower_0_{i}_box_hull', 'sceneObjectId': 30+i,
                   'initialPosition': [0, 2+4*i, 0], 'finalPosition': [0, 2+4*i, 0],
                   'finalSleeping': True} for i in range(5)]
        stable = stack_metrics(list(reversed(bodies)))[0]
        self.assertEqual(stable['levels'], 5)
        self.assertEqual(stable['members'], list(range(30, 35)))
        self.assertEqual(stable['heightRetention'], 1)
        self.assertTrue(stable['layerOrderRetained'])
        collapsed = copy.deepcopy(bodies)
        for i, body in enumerate(collapsed): body['finalPosition'] = [i*5, 2, 0]
        failed = stack_metrics(collapsed)[0]
        self.assertTrue(failed['allSleeping'])
        self.assertFalse(failed['layerOrderRetained'])
        self.assertEqual(failed['heightRetention'], 0)
        self.assertEqual(failed['maximumHorizontalDrift'], 20)

    def test_matrix_has_tall_hull_and_mixed_towers_on_varied_terrain(self):
        import json
        files, cases = build()
        towers = [c for c in cases if 'tall_mixed_stack' in c['scene']]
        self.assertEqual({c['terrain'] for c in towers}, {'flat', 'shallow_x', 'steps'})
        for case in towers:
            path = next(p for p in files if p.as_posix().endswith(case['scene']))
            objects = json.loads(files[path])['objects']
            self.assertEqual(len({o['sceneObjectId'] for o in objects}), len(objects))
            lanes = {}
            for obj in objects: lanes.setdefault(obj['name'].split('_')[1], []).append(obj)
            self.assertEqual([len(lanes[str(i)]) for i in range(4)], [5, 5, 4, 5])
            self.assertTrue(all(o['type'] == 'convexHull' for o in lanes['0'] + lanes['1'] + lanes['2']))
            self.assertTrue(all(not o['fixed'] for o in objects))
            for group in lanes.values():
                self.assertTrue(all(a['position'][1] < b['position'][1] for a, b in zip(group, group[1:])))


if __name__ == '__main__': unittest.main()
