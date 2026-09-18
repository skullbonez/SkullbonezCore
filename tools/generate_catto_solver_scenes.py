"""Generate Catto Solver2D scene adaptations; source provenance lives in the manifest.

Geometry formulas are adapted from Erin Catto's MIT-licensed Solver2D samples.
See ThirdPtySource/solver2d_samples_LICENSE.txt. These are 3D experiments, not a
port of Solver2D's solver, material model, 2D constraints or timestep.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

from bake_hulls import SourceHull, bake_source_hull, serialize_hull

ROOT = Path(__file__).resolve().parents[1]
SCENES = ROOT / 'SkullbonezData/scenes'
REVISION = '1e0492d81f68c7831cfa549699dd98bcb8454060'
SOURCE = f'https://github.com/erincatto/solver2d/blob/{REVISION}/samples/collection/'
SCALE = 4.0
DEFAULT_ORIGIN = (512.0, 160.0, 512.0)


def encoded(value):
    return json.dumps(value, indent=2, allow_nan=False) + '\n'


class Scene:
    """Owns one authored case and its stable body/joint identities."""

    def __init__(self, slug, sample, family='contact', origin=DEFAULT_ORIGIN, notes=(), stress=False):
        self.slug, self.sample, self.family = slug, sample, family
        self.origin, self.notes, self.stress = origin, list(notes), stress
        self.objects, self.joints, self.hulls = [], [], {}

    def position(self, x, y, z=0):
        return [self.origin[0] + SCALE*x, self.origin[1] + SCALE*y, self.origin[2] + SCALE*z]

    def base(self, name, x, y, mass, fixed=False):
        value = dict(name=name, sceneObjectId=1001+len(self.objects), position=self.position(x, y),
                     mass=mass, restitution=0.0, fixed=fixed)
        self.objects.append(value)
        return value

    def box(self, name, x, y, hx, hy, density=1, fixed=False, angle=0, hz=0.5):
        value = self.base(name, x, y, 4*hx*hy*density, fixed)
        value.update(type='box', halfExtents=[SCALE*hx, SCALE*hy, SCALE*hz], euler=[0, 0, angle])
        return value

    def ball(self, name, x, y, radius, density=1, fixed=False):
        mass = math.pi*radius*radius*density
        value = self.base(name, x, y, mass, fixed)
        value.update(type='ball', radius=SCALE*radius, moment=0.4*mass*(SCALE*radius)**2)
        return value

    def ground(self, width=40, thickness=1):
        # Broadphase uses a bounding sphere: one enormous slab exhausts the fixed
        # cell reservation. Tile the same top surface without solver changes.
        count = math.ceil(width / 3)
        half = width / count
        for i in range(count):
            self.box(f'ground_{i:02}', -width+half+2*half*i, -thickness,
                     half, thickness, fixed=True, hz=1)
        self.notes.append(f'Floor is {count} adjoining slabs with the original top height; seams add contact features.')

    def joint(self, a, b, anchor_a, anchor_b):
        self.joints.append(dict(bodyA=a['name'], bodyB=b['name'],
                                localAnchorA=[SCALE*v for v in anchor_a],
                                localAnchorB=[SCALE*v for v in anchor_b],
                                slack=0, frequencyHz=40, dampingRatio=1, flags=0))

    def polygon(self, name, points):
        # Bake the serialized decimal coordinates so a later --check is byte stable.
        vertices = [tuple(float(format(v, '.9g')) for v in (x*SCALE, y*SCALE, z*SCALE))
                    for z in (-0.5, 0.5) for x, y in points]
        count = len(points)
        faces = [list(reversed(range(count))), list(range(count, count*2))]
        faces += [[i, (i+1) % count, (i+1) % count+count, i+count] for i in range(count)]
        path = ROOT / f'SkullbonezData/hulls/catto_{name}.hull'
        hull = bake_source_hull(SourceHull(path, 'catto_'+name, vertices, faces))
        source_center = hull.center_of_mass
        # Scene setup adds a hull's source COM to its authored translation.
        # Center editable vertices too, otherwise that offset is applied twice.
        vertices = [tuple(float(format(v-c, '.9g')) for v, c in zip(vertex, source_center)) for vertex in vertices]
        hull = bake_source_hull(SourceHull(path, 'catto_'+name, vertices, faces))
        self.hulls[path] = serialize_hull(hull)
        area = abs(sum(points[i][0]*points[(i+1) % count][1] - points[(i+1) % count][0]*points[i][1]
                       for i in range(count))) / 2
        value = self.base(name, 0, 0, area)
        value.update(type='convexHull', hull=path.relative_to(ROOT).as_posix(),
                     position=[o+c for o, c in zip(self.origin, source_center)])

    def document(self):
        dynamic = [v for v in self.objects if not v['fixed']]
        lo, hi = [float('inf')]*2, [-float('inf')]*2
        for value in dynamic:
            extent = value.get('halfExtents', [value.get('radius', 4)]*3)
            for axis in range(2):
                lo[axis] = min(lo[axis], value['position'][axis] - max(extent[:2]))
                hi[axis] = max(hi[axis], value['position'][axis] + max(extent[:2]))
        if any(v['name'].startswith('ground_') for v in self.objects):
            lo[1] = min(lo[1], self.origin[1]-SCALE)
        center = [(a+b)/2 for a, b in zip(lo, hi)]
        distance = max(12, (hi[1]-lo[1])/2, (hi[0]-lo[0])/3.2)/math.tan(math.radians(25))*1.35
        # A low hidden safety terrain cannot contact the authored initial test.
        terrain_y = min(lo[1]-1000, self.origin[1]-1000)
        document = dict(format='skullbonez.scene.json', version=5,
                        name='Catto - '+self.sample,
                        simulation=dict(seed=20240205, physics=True, text=False, timeScale=1,
                                        world=dict(gravity=-10*SCALE, fluidHeight=terrain_y-1000, fluidDensity=0)),
                        cinematic=dict(rendering=True, skyAtmosphere=True, clouds=False, fog=False,
                                       godRays=False, volumetricLighting=False, bloom=False,
                                       skyHorizonR=0.16, skyHorizonG=0.20, skyHorizonB=0.27,
                                       skyZenithR=0.06, skyZenithG=0.08, skyZenithB=0.12,
                                       exposure=1.0, gamma=2.2),
                        runtime=dict(vsync=False), playback=dict(frames='unlimited', fixedStep=True, exitOnComplete=False, pauseSnapshotState=False),
                        terrain=dict(flatSlope=dict(baseY=terrain_y, slopeX=0, slopeZ=0)),
                        debug=dict(waterHidden=True, terrainHidden=True),
                        cameras=[dict(name='catto_front', position=[*center, self.origin[2]+distance],
                                      view=[*center, self.origin[2]], up=[0, 1, 0], fovDegrees=50)],
                        objects=self.objects)
        if self.stress:
            document['simulation']['modelCapacity'] = max(4000, len(self.objects)+64)
        if self.joints:
            document['ragdollJoints'] = self.joints
        # Existing native material presentation; no material value changes Physics.
        document['objectMaterials'] = [dict(target=value['name'], mode='matte',
                                           tint=([0.24, 0.28, 0.33] if value['fixed'] else
                                                 ([0.95, 0.49, 0.16] if index % 2 else [0.16, 0.63, 0.83])))
                                       for index, value in enumerate(self.objects)]
        return document

    def manifest(self):
        source_file = {'contact': 'sample_contact.cpp', 'joints': 'sample_joints.cpp', 'far': 'sample_far.cpp'}[self.family]
        return dict(scene=f'SkullbonezData/scenes/catto_{self.slug}.scene.json', sample=self.sample,
                    source=SOURCE+source_file, family=self.family, stress=self.stress,
                    bodies=len(self.objects), dynamicBodies=sum(not v['fixed'] for v in self.objects),
                    joints=len(self.joints), minimumModelCapacity=max(4000, len(self.objects)+64),
                    tickRate=120, suggestedTicks=1200, sourceOriginMapping=list(self.origin),
                    adaptations=self.notes, initialStateOnly=False)


def pyramid(scene, count, shift=0.5, lift=0):
    for i in range(count):
        for j in range(i, count):
            scene.box(f'block_{i:03}_{j:03}', (i+1)*shift+2*(j-i)*shift-0.5*count,
                      (2*i+1)*shift+lift, 0.5, 0.5)


def overlap(scene):
    for i in range(4):
        for j in range(i, 4):
            scene.box(f'block_{i}_{j}', 0.375*(i-4)+0.75*(j-i), 0.5+0.75*i, 0.5, 0.5)


def build_contacts():
    scenes = []
    scene = Scene('single_box', 'Single Box')
    scene.ground(66)
    scene.box('falling_box', 0, 4, 1, 1)
    scenes.append(scene)
    scene = Scene('high_mass_ratio_1', 'High Mass Ratio 1', notes=['Three 10-row pyramids retain 100:1, 200:1 and 300:1 top/body mass ratios.'])
    scene.ground(66)
    for lane in range(3):
        for level in range(10):
            count = 10-level
            for i in range(count):
                scene.box(f'pyramid_{lane}_{level}_{i}', 2*(i-0.5*count)-20+22*lane,
                          1+2*level+(2 if count == 1 else 0), 1, 1,
                          density=(lane+1)*100 if count == 1 else 1)
    scenes.append(scene)
    for variant in (2, 3):
        scene = Scene(f'high_mass_ratio_{variant}', f'High Mass Ratio {variant}',
                      notes=['The original 400:1 mass ratio is preserved.',
                             'Segment floor becomes a solid 3D slab.' if variant == 2 else 'Original thick box floor retained.'])
        scene.ground(20 if variant == 2 else 40, 0.1 if variant == 2 else 2)
        scene.box('support_left', -9, 0.5, 0.5, 0.5)
        scene.box('support_right', 9, 0.5, 0.5, 0.5)
        scene.box('heavy_box', 0, 26, 10, 10)
        scenes.append(scene)
    scene = Scene('overlap_recovery', 'Overlap Recovery')
    scene.ground()
    overlap(scene)
    scenes.append(scene)
    scene = Scene('vertical_stack', 'Vertical Stack')
    scene.ground(100)
    for i in range(15):
        scene.box(f'block_{i:02}', -0.01 if i % 2 == 0 else 0.01, 0.55+1.1*i, 0.5, 0.5)
    scenes.append(scene)
    for count in (20, 100):
        scene = Scene(f'pyramid_{count}', f'Pyramid ({count} rows)', stress=count == 100,
                      notes=[f'Original {"Debug" if count == 20 else "NDEBUG"} size: {count*(count+1)//2} dynamic boxes.'])
        scene.ground(100)
        pyramid(scene, count)
        scenes.append(scene)
    scene = Scene('double_domino', 'Double Domino', notes=['Initial off-center impulse converted to equivalent linear/angular velocity using box inertia.'])
    scene.ground(100)
    for i in range(15):
        value = scene.box(f'domino_{i:02}', -7.5+i, 0.5, 0.125, 0.5)
        if i == 0:
            mass = value['mass']
            hx, hy, hz = value['halfExtents']
            inertia = [mass*(hy*hy+hz*hz)/3, mass*(hx*hx+hz*hz)/3, mass*(hx*hx+hy*hy)/3]
            value.update(type='boxState', velocity=[0.2*SCALE/mass, 0, 0],
                         angularVelocity=[0, 0, -0.1*SCALE*SCALE/inertia[2]],
                         orientation=[0, 0, 0, 1], inertia=inertia, sleeping=False)
            del value['euler']
    scenes.append(scene)
    scene = Scene('card_house', 'Card House', notes=['Original 0.001 half-thickness retained before 4x scaling; no thickening to hide instability.'])
    scene.ground(40, 2)
    for row in range(5):
        count, x0, y = 5-row, 0.175*row, 0.18+0.37*row
        for i in range(count):
            x = x0+0.35*i
            if i != count-1:
                scene.box(f'cap_{row}_{i}', x+0.25, y+0.185, 0.001, 0.2, angle=90)
            scene.box(f'left_{row}_{i}', x, y, 0.001, 0.2, angle=-25)
            scene.box(f'right_{row}_{i}', x+0.175, y, 0.001, 0.2, angle=25)
    scenes.append(scene)
    scene = Scene('circle_stack', 'Circle Stack')
    scene.ground()
    for i in range(10):
        scene.ball(f'ball_{i:02}', 0, 4+3*i, 1)
    scenes.append(scene)
    scene = Scene('confined', 'Confined', notes=['Capsule boundary becomes four box walls; rounded corners are not reproduced.',
                                               'Sphere extrusion can escape the original XY plane; no hidden planar lock is added.'])
    scene.box('floor', 0, 0, 11, 0.5, fixed=True)
    scene.box('ceiling', 0, 20.5, 11, 0.5, fixed=True)
    for sign in (-1, 1):
        scene.box(f'wall_{sign}', sign*10.5, 10.25, 0.5, 10.25, fixed=True)
    for column in range(25):
        for row in range(25):
            scene.ball(f'ball_{column:02}_{row:02}', -8.75+column*18/25, 1.5+row*18/25, 0.5)
    scenes.append(scene)
    return scenes


def build_arch():
    inner = [(16, 0), (14.93803712795643, 5.133601056842984), (13.79871746027416, 10.24928069555078),
             (12.56252963284711, 15.34107019122473), (11.20040987372525, 20.39856541571217),
             (9.66521217819836, 25.40369899225096), (7.87179930638133, 30.3179337000085),
             (5.635199558196225, 35.03820717801641), (2.405937953536585, 39.09554102558315)]
    outer = [(24, 0), (22.33619528222415, 6.02299846205841), (20.54936888969905, 12.00964361211476),
             (18.60854610798073, 17.9470321677465), (16.46769273811807, 23.81367936585418),
             (14.05325025774858, 29.57079353071012), (11.23551045834022, 35.13775818285372),
             (7.752568160730571, 40.30450679009583), (3.016931552701656, 44.28891593799322)]
    inner, outer = [[(x*0.25, y*0.25) for x, y in points] for points in (inner, outer)]
    scene = Scene('arch', 'Arch', notes=['17 source quadrilaterals extruded into baked convex hulls, plus four original loading slabs.'])
    scene.ground(100)
    for i in range(8):
        points = [inner[i], outer[i], outer[i+1], inner[i+1]]
        scene.polygon(f'arch_right_{i}', points)
        scene.polygon(f'arch_left_{i}', [(-x, y) for x, y in reversed(points)])
    scene.polygon('arch_keystone', [inner[8], outer[8], (-outer[8][0], outer[8][1]), (-inner[8][0], inner[8][1])])
    for i in range(4):
        scene.box(f'load_{i}', 0, 0.5+outer[8][1]+i, 2, 0.5)
    return scene


JOINT_NOTE = '2D revolute joints become native 3D point joints (40 Hz, damping 1, zero slack); no hinge motors, limits or 0.1 body damping.'


def build_joints():
    scene = Scene('bridge', 'Bridge', 'joints', notes=[JOINT_NOTE])
    anchor = scene.box('anchor_left', -80.25, 20, 0.25, 0.25, fixed=True)
    previous = anchor
    for i in range(160):
        value = scene.box(f'link_{i:03}', -79.5+i, 20, 0.5, 0.125, density=20)
        scene.joint(previous, value, (0.25 if i == 0 else 0.5, 0, 0), (-0.5, 0, 0))
        previous = value
    anchor = scene.box('anchor_right', 80.25, 20, 0.25, 0.25, fixed=True)
    scene.joint(previous, anchor, (0.5, 0, 0), (-0.25, 0, 0))
    scenes = [scene]
    scene = Scene('ball_and_chain', 'Ball & Chain', 'joints', notes=[JOINT_NOTE, 'Capsule links become rectangular prisms; source capsule area mass retained.'])
    previous = scene.box('anchor', -0.25, 20, 0.25, 0.25, fixed=True)
    for i in range(40):
        value = scene.box(f'link_{i:02}', 0.5+i, 20, 0.5, 0.125, density=20)
        value['mass'] = 20*(0.25+math.pi*0.125**2)
        scene.joint(previous, value, (0.25 if i == 0 else 0.5, 0, 0), (-0.5, 0, 0))
        previous = value
    ball = scene.ball('heavy_ball', 48, 20, 8, density=20)
    scene.joint(previous, ball, (0.5, 0, 0), (-8, 0, 0))
    scenes.append(scene)
    scene = Scene('joint_grid', 'Joint Grid (20 x 20)', 'joints', notes=[JOINT_NOTE,
                  'Reduced from 100 x 100 release grid to 20 x 20 to fit current body/joint limits.',
                  'Original category-wide collision suppression is unavailable; spheres retain normal collision.'])
    bodies = {}
    for k in range(20):
        for i in range(20):
            value = scene.ball(f'node_{k:02}_{i:02}', k, -i, 0.4, fixed=i == 0 and 7 <= k <= 13)
            bodies[k, i] = value
            if i:
                scene.joint(bodies[k, i-1], value, (0, -0.5, 0), (0, 0.5, 0))
            if k:
                scene.joint(bodies[k-1, i], value, (0.5, 0, 0), (-0.5, 0, 0))
    scenes.append(scene)
    scene = Scene('stretched_chain', 'Stretched Chain', 'joints', notes=[JOINT_NOTE,
                  'Initial one-unit anchor errors retained; original maskBits=0 collision suppression is not reproduced.'])
    previous = scene.box('anchor', 0, 40, 0.1, 0.1, fixed=True)
    for i in range(40):
        value = scene.ball(f'link_{i:02}', 0, 38-2*i, 0.2)
        scene.joint(previous, value, (0, -0.5, 0), (0, 0.5, 0))
        previous = value
    scenes.append(scene)
    return scenes


def far_origin(x, y):
    # The native broadphase rejects bounds outside +/-100000. Preserve large
    # absolute offsets without the local 4x scale; keep the 100000 case in bounds.
    return (min(x, 90000), y, 512)


def build_far():
    scene = Scene('far_pyramid', 'Far Pyramid', 'far', far_origin(100000, -80000))
    scene.ground(100)
    pyramid(scene, 10, shift=0.625, lift=0.5)
    scenes = [scene]
    scene = Scene('far_stack', 'Far Stack', 'far', far_origin(40000, -25000))
    scene.ground(10)
    scene.ball('round_support', 1.875, 0.125, 0.1)
    scene.box('square_support', -1.875, 0.15, 0.1, 0.125)
    scene.box('beam', 0, 0.325, 2, 0.05)
    scene.box('small_load', -0.5, 0.9, 0.25, 0.25)
    scene.box('large_load', -0.55, 1.7, 0.5, 0.5)
    scenes.append(scene)
    scene = Scene('far_recovery', 'Far Recovery', 'far', far_origin(80000, -70000))
    scene.ground()
    overlap(scene)
    scenes.append(scene)
    scene = Scene('far_chain', 'Far Chain', 'far', far_origin(40000, -35000), notes=[JOINT_NOTE, 'Capsules become boxes with source capsule-area masses.'])
    previous = scene.box('anchor', -0.05, 4, 0.05, 0.05, fixed=True)
    for i in range(40):
        value = scene.box(f'link_{i:02}', 0.1+0.2*i, 4, 0.1, 0.025)
        value['mass'] = 20*(0.2*0.05+math.pi*0.025**2)
        scene.joint(previous, value, (0.05 if i == 0 else 0.1, 0, 0), (-0.1, 0, 0))
        previous = value
    scenes.append(scene)
    return scenes


def build():
    scenes = build_contacts()+[build_arch()]+build_joints()+build_far()
    for scene in scenes:
        if scene.family == 'far':
            scene.notes.append('Local geometry is 4x; absolute source origin is unscaled to fit native +/-100000 bounds. Far Pyramid moves x=100000 to x=90000. This changes the source precision ratio.')
    files = {}
    for scene in scenes:
        document = scene.document()
        if scene.slug == 'confined':
            document['simulation']['world']['gravity'] = 0
        if scene.slug == 'joint_grid':
            document['simulation']['world']['gravity'] *= 2
        files[SCENES / f'catto_{scene.slug}.scene.json'] = encoded(document)
        files.update(scene.hulls)
    for stress in (False, True):
        files[SCENES / ('catto_solver_stress.suite.json' if stress else 'catto_solver_tests.suite.json')] = encoded(
            dict(format='skullbonez.suite.json', version=1,
                 scenes=[s.manifest()['scene'] for s in scenes if s.stress == stress]))
    omitted = [
        dict(sample='Warm Start Energy', source=SOURCE+'sample_contact.cpp', reason='Requires deleting the heavy top body at step 120 (2 seconds in Solver2D); static preload is not the test.'),
        dict(sample='Friction Ramp', source=SOURCE+'sample_contact.cpp', reason='Five independent exact friction coefficients and mixing are essential; scene primitives do not expose these values.'),
        dict(sample='Rush', source=SOURCE+'sample_contact.cpp', reason='Requires constant-magnitude inward force recomputed every step, not one initial velocity or inverse-square gravity.'),
        dict(sample='Ragdoll', source=SOURCE+'sample_joints.cpp', reason='Source capsule/compound bodies, hinge limits and motor friction are not the native point-joint ragdoll model.'),
        dict(sample='Ragdoll Stress', source=SOURCE+'sample_joints.cpp', reason='Requires motor-driven obstacles and timed spawn/despawn in addition to source ragdoll constraints.'),
        dict(sample='Far Ragdoll Pile', source=SOURCE+'sample_far.cpp', reason='Depends on the same source ragdoll model; substituting the native template would change the test.')]
    manifest = dict(version=1, sourceRevision=REVISION, video='https://www.youtube.com/watch?v=sKHf_o_UCzI&t=596s',
                    videoVerifiedBy='Solver2D README links this exact video; video frames/timestamps were not accessible.',
                    sourceSampleCount=26, scale=SCALE, cases=[s.manifest() for s in scenes], deferred=omitted,
                    commonAdaptations=['Free 3D motion: no planar lock or guide walls.',
                                       'Boxes have source XY dimensions scaled by 4 and default full depth 4; circles become spheres.',
                                       'Source 2D area-density masses preserve ratios; 3D inertia is used.',
                                       'Uniform gravity scales from -10 to -40; native tick rate is 120 Hz, not 60 Hz.',
                                       'Native PGS, sleep, drag, friction and CCD policies remain unchanged; source per-shape friction is not ported.',
                                       'Finite segments become fixed slabs; hidden safety terrain sits 1000 units below each test.',
                                       'All scenes run indefinitely for inspection; manifest suggestedTicks is a measurement duration.',
                                       'Disable replay recording for solver benchmarks; the large pyramid exceeds the existing 8 MiB solver-snapshot cap.'])
    files[SCENES / 'catto_solver_manifest.json'] = encoded(manifest)
    return files, manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Compare all generated scenes, hulls and manifests without writing.')
    args = parser.parse_args()
    files, manifest = build()
    mismatches = []
    for path, content in files.items():
        if args.check:
            if not path.exists() or path.read_text(encoding='utf-8-sig') != content:
                mismatches.append(str(path.relative_to(ROOT)))
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding='utf-8', newline='\n')
    if mismatches:
        raise SystemExit('Generated file mismatch: '+', '.join(mismatches))
    print(f'{"Verified" if args.check else "Wrote"} {len(files)} files; {len(manifest["cases"])} scenes; {len(manifest["deferred"])} explicitly deferred samples.')


if __name__ == '__main__':
    main()
