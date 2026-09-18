"""Author the permanent convex-response scene matrix without changing engine policy."""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path
from bake_hulls import SourceHull, read_source_hull, bake_source_hull, serialize_hull

ROOT = Path(__file__).resolve().parents[1]
SCENES = ROOT / 'SkullbonezData/scenes'
HULLS = ROOT / 'SkullbonezData/hulls'
TERRAINS = ROOT / 'SkullbonezData/terrain'
SHAPES = {'box_hull': ('brick_wall_unit', (2, 2, 2)),
          'wedge': ('wedge', (2, 2, 2)),
          'pyramid': ('pyramid', (2, 2, 2)),
          'elongated': ('brick_wall_unit', (0.4, 0.6, 6)),
          'rock': ('rock_lump_large', (2, 2, 2)),
          'tetrahedron': ('test_motion_tetrahedron', (2, 2, 2))}
SCALES = {'small': 0.5, 'ordinary': 1.0, 'large': 2.0}
TERRAIN_NAMES = ('flat', 'shallow_x', 'shallow_z', 'compound', 'steep', 'basin', 'ridges', 'steps')


def height(name, x, z):
    if name == 'shallow_x': return 0.0874886635 * (x - 512)
    if name == 'shallow_z': return 0.0874886635 * (z - 512)
    if name == 'compound': return 0.061745 * (x + z - 1024)
    if name == 'steep': return 0.5773502692 * (x - 512)
    if name == 'basin': return 0.0004 * ((x - 512) ** 2 + (z - 512) ** 2)
    if name == 'ridges': return 2 * abs(((z - 480) % 48) / 24 - 1)
    if name == 'steps': return 1.25 * math.floor((z - 448) / 24)
    return 0.0


def terrain(name):
    if name in ('basin', 'ridges', 'steps'):
        return {'heightMap': f'../terrain/convex_quality_{name}.heightmap'}
    sx, sz = height(name, 513, 512), height(name, 512, 513)
    return {'flatSlope': {'baseY': -512 * (sx + sz), 'slopeX': sx, 'slopeZ': sz}}


def hull_geometry(kind, scale):
    source_name, target = SHAPES[kind]
    source = read_source_hull(HULLS / (source_name + '.hull'))
    extents = [0.5 * (max(v[i] for v in source.vertices) - min(v[i] for v in source.vertices)) for i in range(3)]
    factor = SCALES[scale]
    name = f'convex_quality_{kind}_{scale}'
    # Bake the exact editable coordinates the writer persists. Otherwise a
    # later ordinary rebake rounds the source first and changes its hash/tensor.
    vertices = [tuple(float(format(v[i] * target[i] * factor / extents[i], '.9g')) for i in range(3)) for v in source.vertices]
    return bake_source_hull(SourceHull(HULLS / (name + '.hull'), name, vertices, source.faces))


def body(kind, scale, name, x, z, surface, geometry, *, tilt=0, lift=0.03, velocity=None, fixed=False):
    factor = SCALES[scale]
    angle = math.radians(tilt)
    if kind == 'primitive_box':
        vertices = [(x, y, z) for x in (-2 * factor, 2 * factor) for y in (-2 * factor, 2 * factor) for z in (-2 * factor, 2 * factor)]
    else:
        vertices = geometry[kind, scale].centered_vertices
    # Place every transformed vertex above the actual supporting surface. The
    # small gap is a common authored drop, never a contact/sleep policy override.
    rotated = [(math.cos(angle) * vx - math.sin(angle) * vy, math.sin(angle) * vx + math.cos(angle) * vy, vz) for vx, vy, vz in vertices]
    y = max(height(surface, x + vx, z + vz) - vy for vx, vy, vz in rotated) + lift
    result = {'type': 'box' if kind == 'primitive_box' else 'convexHull', 'name': name,
              'position': [x, y, z], 'mass': 12 * factor ** 3, 'restitution': 0.1,
              'fixed': fixed, 'euler': [0, 0, tilt]}
    if kind == 'primitive_box': result['halfExtents'] = [2 * factor] * 3
    else: result['hull'] = f'SkullbonezData/hulls/convex_quality_{kind}_{scale}.hull'
    if velocity is not None: result['velocity'] = velocity
    return result


def scene(name, surface, objects, ticks=2400):
    for index, obj in enumerate(objects, 1): obj['sceneObjectId'] = index
    return {'format': 'skullbonez.scene.json', 'version': 5, 'name': name,
            'simulation': {'seed': 62929, 'physics': True, 'text': False, 'timeScale': 1,
                           'world': {'gravity': -32, 'fluidHeight': -1000, 'fluidDensity': 0}},
            'runtime': {'vsync': False},
            'playback': {'frames': ticks, 'fixedStep': True, 'exitOnComplete': False},
            'terrain': terrain(surface), 'debug': {'waterHidden': True},
            'cameras': [{'name': 'diagnostic_overview', 'position': [510, 170, 760],
                         'view': [510, 5, 512], 'up': [0, 1, 0], 'fovDegrees': 58}],
            'objects': objects}


def build():
    files = {}
    geometry = {(kind, scale): hull_geometry(kind, scale) for kind in SHAPES for scale in SCALES}
    for value in geometry.values(): files[value.source.path] = serialize_hull(value)
    for surface in ('basin', 'ridges', 'steps'):
        # Native text heightmaps are x-major, 257 posts at four world units.
        samples = [format(height(surface, x * 4, z * 4), '.9g') for x in range(257) for z in range(257)]
        files[TERRAINS / f'convex_quality_{surface}.heightmap'] = 'SKULLBONEZ_HEIGHTMAP 1\n257 4 15\n' + '\n'.join(samples) + '\n'
    cases = []

    def add(label, surface, objects, family, purpose, ticks=2400):
        name = 'convex_quality_' + label
        path = SCENES / (name + '.scene.json')
        files[path] = json.dumps(scene(name, surface, objects, ticks), indent=2) + '\n'
        cases.append({'scene': path.relative_to(ROOT).as_posix(), 'family': family, 'terrain': surface,
                      'ticks': ticks, 'purpose': purpose, 'bodies': [x['name'] for x in objects]})

    kinds = tuple(SHAPES) + ('primitive_box',)
    for surface in TERRAIN_NAMES:
        for motion in ('rest', 'slide'):
            objects = [body(kind, 'ordinary', kind, 350 + index * 52, 512, surface, geometry,
                            velocity=[0, 0, 6] if motion == 'slide' else None)
                       for index, kind in enumerate(kinds)]
            add(f'{surface}_{motion}', surface, objects, motion,
                'Measure slip, rotation, support, impulses, sleep latency and wake cycles; steep sliding remains legitimate.')
    for scale in ('small', 'large'):
        for surface in ('flat', 'shallow_x', 'ridges'):
            objects = [body(kind, scale, kind, 350 + index * 52, 512, surface, geometry, tilt=8)
                       for index, kind in enumerate(kinds)]
            add(f'{surface}_{scale}', surface, objects, 'scale', 'Check scale-sensitive settling and contact continuity.')
    for surface in ('flat', 'shallow_x', 'ridges'):
        objects = []
        for lane, kind in enumerate(('box_hull', 'primitive_box', 'rock')):
            for level in range(3):
                obj = body(kind, 'ordinary', f'{kind}_{level}', 430 + lane * 75, 512, surface, geometry)
                obj['position'][1] += level * 4.03
                objects.append(obj)
        add(f'{surface}_stack', surface, objects, 'stack', 'Measure three-body stack retention, creeping, energy and repeated waking.')
    for surface in ('flat', 'shallow_x', 'steps'):
        objects = []
        for lane, layers in enumerate((('box_hull',) * 5,
                                        ('elongated',) * 5,
                                        ('box_hull', 'elongated', 'box_hull', 'rock'),
                                        ('primitive_box',) * 5)):
            support_height = None
            for level, kind in enumerate(layers):
                obj = body(kind, 'ordinary', f'tower_{lane}_{level}_{kind}', 390 + lane * 80, 512, surface, geometry)
                vertices = geometry[kind, 'ordinary'].centered_vertices if kind != 'primitive_box' else [(0, -2, 0), (0, 2, 0)]
                bottom, top = min(v[1] for v in vertices), max(v[1] for v in vertices)
                if support_height is not None: obj['position'][1] = support_height - bottom + 0.03
                support_height = obj['position'][1] + top
                objects.append(obj)
        add(f'{surface}_tall_mixed_stack', surface, objects, 'stack',
            'Five-high hull towers and a mixed box-hull/slab/rock stack; compare a five-high primitive control and measure height retention.')
    for surface in ('flat', 'shallow_z', 'steps'):
        objects = []
        for index, (kind, tilt) in enumerate((('pyramid', 180), ('box_hull', 45), ('primitive_box', 45), ('wedge', 35))):
            for pushed in (False, True):
                obj = body(kind, 'ordinary', f'{kind}_{"topple" if pushed else "balance"}', 340 + index * 95, 480 if pushed else 550,
                           surface, geometry, tilt=tilt + (3 if pushed else 0))
                objects.append(obj)
        add(f'{surface}_topple', surface, objects, 'topple', 'Contrast balanced-awake point/edge poses with real gravitational toppling; do not accept arbitrary nudges.')
    for collision in ('near_miss', 'thin_target', 'narrow_window', 'opposing', 'swapped'):
        objects = []
        for lane, target_kind in enumerate(('primitive_box', 'box_hull', 'wedge')):
            z = 400 + lane * 100
            mover = body('elongated', 'ordinary', f'mover_{lane}', 480, z, 'flat', geometry, velocity=[50000 if collision == 'narrow_window' else 6000, 0, 0])
            mover['position'][1] = 80
            target = body(target_kind, 'ordinary', f'target_{lane}', 500, z, 'flat', geometry, fixed=collision != 'opposing')
            target['position'][1] = 83 if collision == 'near_miss' else 80
            if target_kind == 'primitive_box': target['halfExtents'] = [0.05, 1, 7]
            if collision == 'opposing': target['velocity'] = [-1200, 0, 0]
            objects.extend([target, mover] if collision == 'swapped' else [mover, target])
        add('ccd_' + collision, 'flat', objects, 'ccd', 'Confirm exact swept contact or true miss without false wake or consumed time; compare shape order.', 120)
    files[SCENES / 'convex_quality_matrix.json'] = json.dumps({'version': 1, 'tickRate': 120, 'cases': cases}, indent=2) + '\n'
    return files, cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    files, cases = build()
    mismatches = []
    for path, content in files.items():
        if args.check:
            if not path.exists() or path.read_text(encoding='utf-8') != content: mismatches.append(str(path.relative_to(ROOT)))
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding='utf-8')
    if mismatches: raise SystemExit('Outdated fixtures: ' + ', '.join(mismatches))
    print(f'{"CHECKED" if args.check else "WROTE"}: {len(cases)} scenes, {len(SHAPES) * len(SCALES)} hulls, 3 heightmaps')


if __name__ == '__main__': main()
