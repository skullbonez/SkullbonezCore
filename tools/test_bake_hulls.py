"""Analytic mass-property checks independent of the runtime tensor migration."""
import math
from pathlib import Path
import unittest
from bake_hulls import SourceHull, bake_source_hull, read_source_hull

ROOT = Path(__file__).resolve().parents[1]


class ExactHullInertia(unittest.TestCase):
    def assert_tensor(self, actual, expected):
        for a, b in zip(actual, expected): self.assertAlmostEqual(a, b, places=10)

    def tetrahedron(self, scale=(1, 1, 1), translation=(0, 0, 0)):
        vertices = [(0, 0, 0), (2, 0, 0), (0, 3, 0), (0, 0, 4)]
        vertices = [tuple(v[i]*scale[i]+translation[i] for i in range(3)) for v in vertices]
        return bake_source_hull(SourceHull(Path('analytic_tetrahedron.hull'), 'analytic', vertices,
                                           [[0, 2, 1], [0, 1, 3], [0, 3, 2], [1, 2, 3]]))

    def test_asymmetric_tetrahedron_has_products_of_inertia(self):
        hull = self.tetrahedron()
        self.assert_tensor(hull.unit_inertia_tensor, (75/80, 60/80, 39/80, 6/80, 8/80, 12/80))
        self.assertEqual(hull.center_of_mass, (0.5, 0.75, 1))
        # Planted AABB inertia must disagree with this analytic oracle.
        self.assertGreater(max(abs(a-b) for a,b in zip(hull.unit_inertia, hull.unit_inertia_tensor)), 1)

    def test_translation_mass_and_uniform_density_scaling(self):
        base = self.tetrahedron()
        moved = self.tetrahedron(translation=(1000, -500, 2000))
        self.assert_tensor(base.unit_inertia_tensor, moved.unit_inertia_tensor)
        large = self.tetrahedron(scale=(3, 3, 3))
        self.assert_tensor(large.unit_inertia_tensor, tuple(9*x for x in base.unit_inertia_tensor))
        self.assertAlmostEqual(large.default_mass/base.default_mass, 27)
        for a,b in zip(large.unit_inertia_tensor, base.unit_inertia_tensor):
            self.assertAlmostEqual(a*large.default_mass, b*base.default_mass*3**5)

    def test_nonuniform_scale_uses_volume_integrals(self):
        hull = self.tetrahedron(scale=(2, 0.5, 3))
        a,b,c = 4,1.5,12
        expected = (3*(b*b+c*c)/80, 3*(a*a+c*c)/80, 3*(a*a+b*b)/80, a*b/80,a*c/80,b*c/80)
        self.assert_tensor(hull.unit_inertia_tensor, expected)

    def test_box_equivalence_and_every_asset_is_positive_definite(self):
        for path in (ROOT/'SkullbonezData/hulls').glob('*.hull'):
            hull = bake_source_hull(read_source_hull(path))
            self.assertTrue(all(math.isfinite(x) for x in hull.unit_inertia_tensor), path)
        hull = bake_source_hull(read_source_hull(ROOT/'SkullbonezData/hulls/convex_quality_box_hull_ordinary.hull'))
        self.assert_tensor(hull.unit_inertia_tensor, (8/3,8/3,8/3,0,0,0))


if __name__ == '__main__': unittest.main()
