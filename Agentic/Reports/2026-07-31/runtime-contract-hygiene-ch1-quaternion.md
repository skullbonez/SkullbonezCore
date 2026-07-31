# Runtime Contract Hygiene — CH1 Quaternion Contract

Date: 2026-07-31
Plan: `Agentic/Plans/TODO/runtime-contract-hygiene.md`
Branch: `nightrunner-30th-JUL-26`

## Outcome

`Quaternion.h` now describes only the public surface that exists. The orphan
angular-displacement sentence and the three deleted private-axis-builder
comments are gone. `RotateAboutAxis` states its normalized world-axis
precondition and radians unit in both the header invariant and its declaration.

`Quaternion::RotateAboutAxis` now computes the axis magnitude only under
`_DEBUG` and asserts that it is within `TOLERANCE` of one. Release retains the
exact existing axis-angle construction, Hamilton composition, and final
normalization. The local hazard comment explains why normalizing the composed
quaternion cannot repair a non-unit input axis: the bad length changes only the
delta quaternion's vector part and therefore changes the requested angle.

## Caller Audit

The tree contains 22 textual calls across 12 files. Every production call
already supplies a unit axis:

| Owner/file | Unit-axis proof |
|---|---|
| `Runtime/App/ReplayValidation.Probes.cpp` | literal world +Y |
| `Runtime/Editor/EditorTerrainOrientation.cpp` | antiparallel fallback uses literal +X; general cross-product axis divides by its measured magnitude |
| `Runtime/Editor/EditorPlacementAssets.cpp` | literal world +Y |
| `Runtime/Editor/EditorGizmoTools.cpp` | both calls use `EditorAxisVector`, which returns one Cartesian unit basis vector |
| `Physics/PhysicsBodyStore.cpp` | angular velocity divides by `omegaMag` after a positive-magnitude guard |
| `Physics/Ragdoll.cpp` | correction axis calls `Normalise` before rotation |

Test callers using literal Cartesian axes or an explicitly normalized random
axis were already valid:

- `Agentic/Tests/RuntimeInteractionPolicyTests/RuntimeInteractionPolicyTests.cpp`;
- `SkullbonezTests/TestPersistentContactSolver.cpp`;
- `SkullbonezTests/TestPhysicsHandles.cpp`;
- all nine call sites in `SkullbonezTests/TestQuaternion.cpp`.

The plan's provisional claim that every caller already satisfied the
precondition was not fully correct. Two test helper boundaries accepted
arbitrary non-unit axes:

- `TestMatrix4.cpp` passed `(1,2,3)` through its `Rotation` helper;
- `TestObjectContactManifold.cpp` passed three diagonal axes through `MakeBody`.

Both helpers now normalize their by-value fixture axis immediately before
calling `RotateAboutAxis`. This corrects test setup without changing production
arithmetic or weakening the public precondition.

## Validation

| Command | Result |
|---|---|
| `tools\validate_format.bat` | PASS; all 575 source files and 320 headers clean |
| `tools\validate_build.bat Debug` | PASS; zero warnings/errors |
| Debug Quaternion/Matrix4/object-manifold filter | PASS; 28 cases / 2,302 assertions |
| production arithmetic diff review | PASS; only `_DEBUG` magnitude/assert code added |
| repository-wide caller search | PASS; 22/22 textual calls audited |

CH1 does not require the repository-wide gate. Its production change is public
documentation plus one Debug-only misuse tripwire. The two additional arithmetic
lines are confined to test-fixture normalization required by the newly verified
contract.

## Comment Audit

All 4/4 touched source-bearing files were inspected against the comment style
guide. `Quaternion.h` names the public precondition and units;
`Quaternion.cpp` explains the numerical hazard and Debug/Release policy; both
test helpers explain why fixture normalization belongs at their boundary. No
file is deferred.

## Independent Review

The first read-only review blocked closure on a stale `RotateAboutXYZ`
learning-header invariant in `Quaternion.cpp`. That deleted-method claim was
replaced with the live normalized-world-axis/radians contract. Re-review
returned **ACCEPT/CLEAR** with no remaining blocker.
