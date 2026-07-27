# Coverage Gate Test Reorganization — CG1 Owner Move

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan phase: CG1

## Result

`SkullbonezTests/TestCoverageFloorContracts.cpp` and both of its project rows
are deleted. Its five tests now live with their subsystem owners:

| Test | Destination |
|---|---|
| Full replay tracks round-trip | `TestReplayArtifact.cpp` |
| Every object-manifold shape pair | `TestObjectContactManifold.cpp` |
| Box/hull partial-submersion forces | `TestPhysicsHandles.cpp` |
| Terrain sweep/manifold shape matrix | `TestTerrain.cpp` |
| Replay timeline retention/event sequencing | `TestReplayRecorder.cpp` |

`TestCollisionShapeFixtures.h` owns the two value-only sphere/box constructors
shared by three destinations. This avoids duplicating a cross-subsystem fixture;
it owns no state, test, gate, or metric policy.

Each receiving file's learning header now names the invariant it received. The
five test names are unchanged. A normalized assertion-statement comparison
found zero missing statements, and focused execution retained all 231 moved
assertions.

## Validation

- `tools\validate_tests.bat`: PASS; 418/418 cases and 2,410,159/2,410,159
  assertions, identical to the CG0 baseline.
- `tools\validate_coverage.bat`: PASS directly at unchanged floors.
- `tools\validate_format.bat`: PASS; 569 implementations, 316 headers, and all
  Related paths clean.
- Project/filter check: 114 project items and 114 filter items, zero errors.
- Obsolete-name search: no `TestCoverageFloorContracts` source, project, or
  filter row remains.

## Before / After Coverage

| Subsystem | Before | After | Delta |
|---|---:|---:|---:|
| maths | 86.60% | 86.60% | 0.00 pp |
| core_primitives | 88.39% | 88.39% | 0.00 pp |
| physics_stores | 76.90% | 76.90% | 0.00 pp |
| physics_stages_and_solver | 80.37% | 80.37% | 0.00 pp |
| replay_artifact_codecs | 76.34% | 76.34% | 0.00 pp |
| startup | 91.64% | 91.64% | 0.00 pp |
| config_and_schema | 94.84% | 94.84% | 0.00 pp |
| runtime_input_and_interaction | 76.25% | 76.25% | 0.00 pp |
| scene_logic | 97.44% | 97.44% | 0.00 pp |
| replay_value_seams | 84.49% | 84.49% | 0.00 pp |

Whole instrumented output also stayed byte-for-byte equivalent as a count:
21,044 / 28,282 lines (74.41%). Coverage policy files were not edited.
