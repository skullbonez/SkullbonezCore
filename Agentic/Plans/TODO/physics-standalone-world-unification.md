# Physics Standalone World Unification

Date: 2026-07-22
Owner: skullbonez
State: In progress — PU0-PU1 complete
Ledger tasks: 5 (PU0-PU4)

## Problem And Evidence (2026-07-22, main tip 0c5263e1)

The engine carries two parallel physics simulations:

- `PhysicsEngine`/`PhysicsWorld` is the production path: staged pipeline
  (force, external-force, broadphase, narrowphase, terrain, contact solver,
  sleep), dense stores, byte-exact deterministic baselines.
- `PhysicsStandaloneWorld` in `SkullbonezSource/Physics/PhysicsApi.h:522` and
  `PhysicsApi.cpp` (2,227 current-tip lines) is a second simulation with its
  own semi-implicit Euler `Step()` and its own sphere-sphere/sphere-box contact
  generation. The registration evidence described islands as a permanently
  empty stub; PU0 corrected that stale evidence: `GenerateStandaloneIslands()`
  is a second deterministic union-find island implementation and the smoke
  asserts contact-, wake-, stale-body-, and point-joint-connected islands.

The only production consumer of `PhysicsStandaloneWorld` outside
`PhysicsApi.*` is the standalone smoke probe at
`SkullbonezSource/Runtime/Startup/StartupProbeHarnesses.cpp:386`
(`RunPhysicsStandaloneSmoke()`), which backs the physics lane's
standalone-smoke engine process in `validate_physics` / `validate_full`.
`SkullbonezTests/` has zero direct references. A public API whose `Step()`
simulates differently from the engine that ships is worse than no API: any
consumer that trusts it learns wrong physics.

Prior rulings this plan supersedes or touches:

- `physics-facade-unification` (closed 2026-07-20) absorbed the *Scene*
  facade into `PhysicsEngine`; it did not address the standalone world.
- The round-6 monolith TU right-sizing cohesion ruling for `PhysicsApi.cpp`
  (2,226 lines) is superseded by this plan's owner-directed unification;
  re-litigation evidence is this plan's registration.

## Goal

One simulation. The public physics API surface (descriptors, handles, masked
updates, views, activation commands, ray cast, broadphase query) survives as
the contract, but every stepped behavior behind it is `PhysicsEngine`'s real
solver. The duplicate Euler integrator, duplicate contact generation, and
duplicate island generation are deleted with no compatibility spelling left
behind.

## Non-Goals

- No change to `PhysicsEngine`/`PhysicsWorld` solver behavior, stage order,
  or determinism envelope. The 44,401-line physics CSV stays byte-exact.
- No new inheritance, callback packs, or forwarding facades.
- No change to `PhysicsSceneObjectId` identity policy (2026-07-11 ruling
  stands).
- No public-API expansion beyond what the smoke re-host strictly needs.

## PU0 Current-Tip Census (2026-07-22)

Method: current CodeGraph index first (`codegraph status .` reported 1,041
files, 32,363 nodes, 94,630 edges, and up-to-date), then source-confirmed with
targeted `rg` and line reads. `PhysicsApi.h` has 38 top-level declarations
(three forward declarations, five enums, 26 structs, one class, and three free
functions). `PhysicsApi.cpp` has 70 function definitions: 26 unnamed-namespace
helpers, 43 `PhysicsStandaloneWorld` methods, and
`RunPhysicsStandaloneSmoke()`. The two FNV constants and
`INVALID_ISLAND_ROW` are also inventoried below.

### Symbol Classification

| Classification | Symbols | Current consumers and PU1/PU3 disposition |
|---|---|---|
| Live body contract | `PhysicsBodyMotionKind`, `PhysicsBodyCreateDesc`, `PhysicsBodyUpdateMask` and its values, `PhysicsBodyUpdateDesc`, `MakePhysicsBodyCreateDesc()` | Production Physics, scene/editor/replay/startup/tools, and tests consume these. Retain; the enum type has no spelled external use but its body-mask values do. |
| Live collider contract | `PhysicsColliderCreateDesc`, `MakeColliderCreateDesc()` | Production Physics and scene/editor/replay/tools/tests consume these. Retain. |
| Live authored-registration contract | `PhysicsAuthoredBodyRegistration`, `PhysicsAuthoredBodyRefreshView` | `PhysicsEngine` and `SceneWorld` consume these. Retain. |
| Live point-joint creation contract | `PhysicsPointJointCreateDesc` | `PhysicsEngine`/`PhysicsWorld`, scene setup, startup, and tests consume it. Retain. |
| Forward declarations | `PhysicsDebugContact`, `PhysicsPipelineRecord`, `PhysicsSolverSnapshot` | `PhysicsDebugContact` and `PhysicsPipelineRecord` appear here only as pointer types inside the unused `PhysicsDiagnosticsSnapshot`; their real definitions/consumers are owned by `PhysicsDebugData.h`. `PhysicsSolverSnapshot` is declaration-only in this header and is owned by `PhysicsSolverSnapshot.h`. Delete these declarations when their dead local wrapper is removed; this does not delete the canonical types. |
| Standalone-only mutation contract | `PhysicsColliderUpdateMask` and values, `PhysicsColliderUpdateDesc`, `PhysicsPointJointUpdateMask` and values, `PhysicsPointJointUpdateDesc`, `PhysicsStandaloneStepDesc`, `PhysicsActivationCommandKind`, `PhysicsActivationCommand` | No consumer outside `PhysicsApi.*`; delete with the standalone world unless PU1 proves a strictly required engine-backed smoke packet. No compatibility spelling. |
| Standalone-only query/view contract | `PhysicsRayCastDesc`, `PhysicsRayCastHit`, `PhysicsBroadphaseCellQueryDesc`, `PhysicsBroadphaseQueryResultView`, `PhysicsBodyView`, `PhysicsBodyCollectionView`, `PhysicsColliderView`, `PhysicsColliderCollectionView`, `PhysicsPointJointView`, `PhysicsPointJointCollectionView`, `PhysicsContactView`, `PhysicsContactCollectionView`, `PhysicsIslandView`, `PhysicsIslandCollectionView` | No consumer outside `PhysicsApi.*`; all are projections or scratch views owned by the duplicate simulation. Delete unless PU1 identifies the minimum typed value needed to exercise the production engine query. |
| Dead wrapper | `PhysicsDiagnosticsSnapshot` | Definition-only; no external consumer and no implementation use. Delete. |
| Smoke-only values | `PhysicsStandaloneSmokeResult`, `RunPhysicsStandaloneSmoke()` | Only `StartupProbeHarnesses.cpp` consumes the result/function. `coverage_floors.json` names the function as a scope ruling. Replace with the engine-backed smoke result/entry in PU2, not a compatibility wrapper. |
| Duplicate owner public surface (25 methods) | `PhysicsStandaloneWorld::{Clear,CreateBody,UpdateBody,DestroyBody,SetPendingBodyImpulse,ApplyBodyImpulse,CreateCollider,UpdateCollider,DestroyCollider,CreatePointJoint,UpdatePointJoint,DestroyConstraint,Step,ApplyActivationCommand,SleepEnabled,RayCast,QueryBroadphaseCells,Body,Bodies,Collider,Colliders,PointJoint,PointJoints,Contacts,Islands}` | Called only by `RunPhysicsStandaloneSmoke()` in the same `.cpp`; delete the class. PU1 maps required lifecycle points to `PhysicsEngine`, not method-for-method forwarding. |
| Duplicate owner private surface (18 methods) | Three `IsAlive()` overloads; `MutableBodyRecord()`, `BodyRecord()`, `MakeBodyView()`, `InvalidateBodyViews()`, `BodyViewCache()`, `MakeColliderRecord()`, `MakeColliderView()`, `MakePointJointView()`, `TombstoneConstraintSlot()`, `ClearContacts()`, `ClearIslands()`, `GenerateStandaloneContacts()`, `GenerateStandaloneIslands()`, `TryAppendSphereSphereContact()`, `TryAppendSphereBoxContact()` | Called only by the duplicate owner. Delete. |
| Duplicate owner state (22 fields) | Body/collider stores; body/collider/point-joint view caches; contact/island/union-find/query scratch vectors; constraint generations/liveness/free slots; next-generation and sleep flags | Entirely private standalone state. Delete; `PhysicsEngine` remains the only store/solver owner. |
| Shared header helpers | `MakePhysicsBodyCreateDesc()`, `MakeColliderCreateDesc()` | Live production/test builders; retain. No `PhysicsApi.cpp` helper has external linkage or an external consumer. |
| Smoke hashing helpers | `FNV_OFFSET_BASIS`, `FNV_PRIME`, `HashBytes()`, `HashU32()`, `HashU64()`, `HashFloat()`, `HashVector()`, `ContactFeatureId()`, `HashContactView()`, `HashContactCollection()`, `HashIslandView()`, `HashIslandCollection()`, `HashSmokeResult()` | Used only by standalone contact generation or the smoke. Re-home only a minimal engine-smoke hash helper if PU1 keeps a hash; delete the rest. |
| Duplicate island helpers | `INVALID_ISLAND_ROW`, `FindIslandRoot()`, `UnionIslandRows()` | Used only by `GenerateStandaloneIslands()`. Delete. This is the source proof that the current island path is implemented, not stubbed. |
| Duplicate simulation/query helpers | `ClampFloat()`, `CopyContactMaterialPayload()`, `InvertNonZeroComponents()`, `ComputeInverseMass()`, `BodyPassesQueryFilters()`, `ConservativeShapeRadius()`, `ConservativeBroadphaseRadius()`, `ColliderShapeKindForShape()`, `EffectiveColliderRadius()`, `ColliderWorldCenter()`, `IntersectRaySphere()`, `SphereOverlapsAabb()`, `NextStandaloneInitialGeneration()` | Internal-only helpers for the duplicate integrator, lifecycle, contact, ray, broadphase, or generation behavior. Delete rather than merge into the production solver. |

### Live Symbol Consumers

Every symbol not listed in this table has no consumer outside `PhysicsApi.*`
except for the smoke/tool metadata recorded below.

| Symbol family | Production consumers | Test consumers |
|---|---|---|
| `PhysicsBodyMotionKind` | `PhysicsBodyStore.cpp`, `PhysicsEngine.cpp`; `Runtime/Editor/RunEditorHistory.cpp`, `RunEditorObjectPlacement.cpp`; `Runtime/Scene/SceneAuthoredSetup.cpp`, `SceneGeneratedSetup.cpp`, `SceneWorld.cpp`; `Runtime/Startup/StartupProbeHarnesses.cpp`; `Runtime/Tools/RuntimeTools.cpp` | `TestCoverageFloorContracts.cpp`, `TestDeterminism.cpp`, `TestPhysicsStageState.cpp` |
| `PhysicsBodyCreateDesc` | `PhysicsBodyStore.cpp/.h`, `PhysicsEngine.cpp/.h`; `Runtime/Editor/RunEditorHistory.cpp`, `RunEditorObjectPlacement.cpp`; `Runtime/Scene/SceneAuthoredSetup.cpp`, `SceneGeneratedSetup.cpp`, `SceneWorld.cpp/.h`; `Runtime/Startup/StartupProbeHarnesses.cpp` | `TestPhysicsHandles.cpp` |
| `MakePhysicsBodyCreateDesc()` | `Runtime/Editor/RunEditorObjectPlacement.cpp`; `Runtime/Scene/SceneAuthoredSetup.cpp`, `SceneGeneratedSetup.cpp`; `Runtime/Startup/StartupProbeHarnesses.cpp`; `Runtime/Tools/RuntimeTools.cpp` | `TestCoverageFloorContracts.cpp`, `TestDeterminism.cpp`, `TestPhysicsStageState.cpp` |
| `PhysicsBodyUpdateDesc` and live body-mask values | `PhysicsEngine.cpp/.h`; `Runtime/Editor/EditorTools.h`, `RunEditorGizmoTools.cpp`, `RunEditorHistory.cpp`, `RunEditorTools.cpp`; `Runtime/Replay/ReplayValidation.cpp`, `ReplayValidation.Probes.cpp`; `Runtime/Startup/StartupProbeHarnesses.cpp` | None |
| `PhysicsColliderCreateDesc` | `PhysicsEngine.cpp/.h`; `Runtime/Editor/EditorTools.h`, `RunEditorHistory.cpp`, `RunEditorObjectPlacement.cpp`, `RunEditorTools.cpp`; `Runtime/Replay/ReplayValidation.cpp`; `Runtime/Scene/SceneAuthoredSetup.cpp`, `SceneGeneratedSetup.cpp`, `SceneWorld.cpp/.h` | None |
| `MakeColliderCreateDesc()` | `Runtime/Editor/RunEditorGizmoTools.cpp`, `RunEditorHistory.cpp`, `RunEditorObjectPlacement.cpp`; `Runtime/Replay/ReplayValidation.cpp`, `ReplayValidation.Probes.cpp`; `Runtime/Scene/SceneAuthoredSetup.cpp`, `SceneGeneratedSetup.cpp`; `Runtime/Startup/StartupProbeHarnesses.cpp`; `Runtime/Tools/RuntimeTools.cpp` | `TestCoverageFloorContracts.cpp`, `TestDeterminism.cpp` |
| `PhysicsAuthoredBodyRegistration` | `PhysicsEngine.cpp/.h`, `Runtime/Scene/SceneWorld.cpp` | None |
| `PhysicsAuthoredBodyRefreshView` | `PhysicsEngine.cpp/.h`, `Runtime/Scene/SceneWorld.cpp` | None |
| `PhysicsPointJointCreateDesc` | `PhysicsEngine.cpp/.h`, `PhysicsWorld.cpp/.h`; `Runtime/Scene/SceneAuthoredSetup.cpp`; `Runtime/Startup/StartupProbeHarnesses.cpp` | None |

### Include, Build, Tool, And Validation Consumers

There are 29 direct includes of `PhysicsApi.h` outside `PhysicsApi.cpp`:

- Physics (3): `PhysicsBodyStore.cpp`, `PhysicsEngine.cpp`, `PhysicsWorld.cpp`.
- Runtime (22): `Diagnostics/SceneMemoryDiagnostics.cpp`, `RunFrame.cpp`,
  `RuntimeOverlayDiagnostics.cpp`; editor `LauncherTools.cpp`,
  `RunEditorGizmoTools.cpp`, `RunEditorHistory.cpp`,
  `RunEditorObjectPlacement.cpp`, `RunEditorTools.cpp`; replay
  `ReplayPrediction.cpp`, `ReplayPredictionPublication.cpp`,
  `ReplayPredictionScheduling.cpp`, `ReplayPredictionTopologyPublication.cpp`,
  `ReplayRuntime.cpp`, `ReplayValidation.cpp`, `ReplayValidation.Probes.cpp`;
  scene `SceneAuthoredSetup.cpp`, `SceneGeneratedSetup.cpp`, `SceneWorld.cpp`,
  `SceneWorld.h`; `Startup/StartupProbeHarnesses.cpp`,
  `Tools/RuntimeTools.cpp`, `UI/OperatorEditorFrameComposer.cpp`.
- Tests (4): `TestCoverageFloorContracts.cpp`, `TestDeterminism.cpp`,
  `TestPhysicsHandles.cpp`, `TestPhysicsStageState.cpp`.

Build/tool consumers are `SKULLBONEZ_PHYSICS.vcxproj` and `.filters` (one
compile and one include row each); `allocation_policy_allowlist.json`
(`PhysicsApi.cpp` growth/make-unique row, `PhysicsApi.h` vector row, and the
startup-smoke owner cap); `check_allocation_policy.py` (header member-scan
path); and `coverage_floors.json` (scope ruling). `validate_physics.bat`
launches `Debug/SKULLBONEZ_CORE.exe --physics-standalone-smoke` before the
44,401-row regression process; `validate_full.bat` delegates to that script
with the Debug build reused, and `validate_select.bat physics` delegates to
the same script. No test executable calls `PhysicsStandaloneWorld` directly.

### What The Smoke Actually Proves

`StartupProbeHarnesses.cpp` accepts both `--physics-standalone-smoke` and
`--physics_standalone_smoke`, optionally writes the same report through the
hyphen/underscore log options, and exits zero only when both
`RunPhysicsStandaloneSmoke()` and the already-engine-backed
`RunPhysicsRuntimeHandleSmokeSample()` pass.

The standalone half hardcodes exact state/invariants, not expected hash
constants. It requires:

- exact final primary position `(3,9,-2)` and velocity `(2,-4,0)`, exact
  secondary position `(-4,8,1.5)` and velocity `(-1,-7,0.5)`, four steps, two
  live bodies, one collider, zero point joints, two contacts, two islands, and
  one broadphase result;
- invalid/stale create, update, destroy, activation, child-collider, and
  connected-constraint rejection; masked body/collider/joint updates; handle
  generation after clear; impulse and sleep-gate behavior;
- exact sphere-sphere and sphere-box contact identities/material values,
  deterministic contact order, contact/wake/stale/constraint island topology,
  exact ray candidate/distance/point/normal, and broadphase membership.

`contactHash`, `islandHash`, and `deterministicHash` are computed and printed,
but neither the launcher nor any test/tool compares them to a hardcoded value.
The runtime-mirror half separately checks atomic failed creation, store/render/
joint handle alignment, collider refresh, reorder-preserved handle state,
atomic deletion, and stable-handle mutation. PU1 must preserve meaningful
lifecycle/determinism assertions while transitioning expected state to the real
engine path; there is no committed standalone hash oracle to refresh.

## PU1 Decision Record (2026-07-22)

Owner decision: delete-and-re-host is final. `PhysicsEngine`/`PhysicsWorld` is
the only stepped simulation; `PhysicsStandaloneWorld` is not taught the real
solver, wrapped, aliased, or renamed. PU0 found no external standalone-world
consumer that needs a transition seam.

### Retained Contract

The final `PhysicsApi.h` retains only contract values with a live production
consumer or a required engine-backed smoke/query use:

- body: `PhysicsBodyMotionKind`, `PhysicsBodyCreateDesc`,
  `PhysicsBodyUpdateMask` and values, `PhysicsBodyUpdateDesc`, and
  `MakePhysicsBodyCreateDesc()`;
- collider: `PhysicsColliderCreateDesc` and `MakeColliderCreateDesc()`;
- authored registration/refresh: `PhysicsAuthoredBodyRegistration` and
  `PhysicsAuthoredBodyRefreshView`;
- point joint: `PhysicsPointJointCreateDesc`,
  `PhysicsPointJointUpdateMask` and values, and
  `PhysicsPointJointUpdateDesc`;
- production-store queries: `PhysicsRayCastDesc`, `PhysicsRayCastHit`,
  `PhysicsBroadphaseCellQueryDesc`, and
  `PhysicsBroadphaseQueryResultView`.

The former body/collider/point-joint collection projections do not survive as
parallel cache types. `PhysicsEngine::ReadBodies()`, `ReadColliders()`, and
`ReadPointJointConstraints()` are the existing canonical immutable views;
`GetDiagnosticsView()` remains the production contact/island diagnostic view.
The standalone step/activation packets, collider-update packet, contact/island
projections, dead diagnostics wrapper, smoke result, and all associated masks,
helpers, caches, and forward declarations are deleted. Explicit engine methods
(`WakeBody`, `SeedBodyAsleep`, `SetSleepEnabled`) remain the activation command
surface; a generic command variant would weaken that typed boundary.

`PhysicsApi.cpp` has no surviving implementation responsibility after the
move: the header's two descriptor builders remain inline, engine query logic
lives with `PhysicsEngine`, and cold smoke logic lives with the startup probe.
PU3 therefore deletes `PhysicsApi.cpp` and both project/filter rows rather than
leaving an empty or forwarding translation unit.

### Public-Contract-To-Engine Map

| Contract point | Production owner after PU2 | Decision |
|---|---|---|
| Create body + collider | `PhysicsEngine::RegisterAuthoredBody()` | Existing atomic command; smoke asserts both handles and owned rows. |
| Update body | `PhysicsEngine::UpdateAuthoredBody()` | Existing masked command; stale handles must fail. |
| Update collider | `PhysicsEngine::UpdateAuthoredBodyAndCollider()` | Existing atomic body/collider edit; no independent collider authority is added. |
| Destroy body + collider | `PhysicsEngine::DestroyAuthoredBody()` | Existing atomic command also removes connected point joints; smoke asserts stale body/collider identity and survivor stability. |
| Create point joint | `PhysicsEngine::CreatePointJoint()` | Existing command. |
| Update/destroy point joint | New `PhysicsEngine::{UpdatePointJoint,DestroyConstraint}()` commands owned by `PhysicsWorld` | `PointJointConstraint` carries its stable typed handle. Update/destroy resolve the exact handle; erasing a dense row cannot retarget another live handle. The world advances its constraint generation across `Clear()` and assigns monotonic indices within an epoch. |
| Activation | Existing `WakeBody()`, `SeedBodyAsleep()`, `SetSleepEnabled()` | Smoke checks real store/sleep views; no standalone command packet. |
| Step | Existing `PhysicsEngine::Step()` with an inline, zero-worker `WorkerPool` | Runs the shipping stage order and real solver; no second integrator. |
| Ray cast | New `PhysicsEngine::RayCast(PhysicsRayCastDesc)` | Read-only conservative query over the engine-owned collider/body stores; stable collider-row tie order and no heap growth. |
| Broadphase AABB query | New `PhysicsEngine::QueryBroadphaseCells(PhysicsBroadphaseCellQueryDesc)` | Read-only candidate query over real engine stores with a `PhysicsFixedList` scratch member; deterministic body-store order and no dynamic growth. It is a public candidate query, not a second broadphase simulation. |
| Immutable views | Existing `PhysicsEngine::Read*` and `GetDiagnosticsView()` | No duplicate caches or mutable authority escape. |

The query methods may use geometry-only helpers moved into
`PhysicsEngine.cpp`; none may integrate bodies, generate contacts/islands, or
mutate solver state. `RuntimeTools` migration to the new ray query is not
required for this plan because it has different launcher/tool behavior and is
not a consumer of the old API.

### Smoke Transition

`--physics-standalone-smoke` and its underscore alias remain accepted because
they are validation/operator entry points, but the implementation and report
identify an engine lifecycle smoke. The public `PhysicsStandaloneSmokeResult`
and `RunPhysicsStandaloneSmoke()` disappear. A startup-local
`PhysicsEngineLifecycleSmokeResult` runs the scenario against a heap-owned
`PhysicsEngine`; the existing `PhysicsRuntimeHandleSmokeResult` continues to
prove the scene/render mirror.

The engine lifecycle scenario exercises body/collider create-update-destroy,
point-joint create-update-destroy and cascade deletion, activation commands,
one or more real production steps, ray cast, broadphase query, and immutable
views. It runs twice from fresh engine state in the same process. Each run
hashes stable handles, exact body hot state, collider records, point-joint
records, query results, and selected production contact/island diagnostics;
both hashes and all explicit lifecycle assertions must match.

Hash transition ruling:

- there is no old committed or hardcoded hash expectation to refresh;
- the old printed standalone `contactHash`, `islandHash`, and
  `deterministicHash` are deleted with the duplicate simulation;
- the new printed hash derives solely from the production-engine scenario and
  is accepted only when two fresh runs match byte-exactly in one process;
- exact expected counts/state for the new scenario land with PU2 and are
  lifecycle evidence, not a physics CSV, replay, screenshot, or behavioral
  golden; no baseline/golden file may change.

The allocation-policy metadata moves the cold owner allowance from
`PhysicsApi.cpp` to the startup probe and deletes the header vector allowance.
The coverage-floor scope ruling is rewritten to name the engine lifecycle
smoke. `validate_physics.bat`, `validate_full.bat`, and `validate_select.bat`
retain their existing one-process lane wiring.

## Phases

- [x] PU0 — Census. Inventory every symbol in `PhysicsApi.h`/`PhysicsApi.cpp`
  and classify: pure contract (descs/handles/masks/views), standalone-only
  simulation code, and shared helpers. Inventory every consumer of each
  class (production, tests, tools, validation scripts) and record what the
  standalone smoke actually asserts (hardcoded expected hashes vs internal
  invariants). Deliverable: classification table committed in this plan.
- [x] PU1 — Decision record. From PU0, ratify the target: contract types are
  retained (relocated if needed), `PhysicsStandaloneWorld` is deleted, and
  the smoke probe is re-hosted as a `PhysicsEngine`-backed lifecycle smoke
  exercising the same public contract points (create/update/destroy bodies,
  colliders, point joints, activation commands, ray cast, broadphase query,
  deterministic hashes). Record how smoke hash expectations transition: they
  derive from the engine path after re-host, updated in the same commit, and
  are lifecycle evidence, not physics-behavior baselines.
- [x] PU2 — Re-host the smoke. Implement the engine-backed smoke path per
  PU1, keep `--physics-standalone-smoke` (or its successor flag) exiting
  zero with meaningful lifecycle/determinism assertions, and keep the
  validation lane wiring (`validate_physics`, `validate_full`) intact in the
  same commit.
- [x] PU3 — Delete the standalone simulation. Remove
  `PhysicsStandaloneWorld`, its scratch stores, its contact/island
  generation, and every dead helper; retain only contract types with live
  consumers. No `*Compatibility`/`*Bridge` respellings. Project files and
  filters updated in the same commit.
- [ ] PU4 — Closure. Independent rubber-duck review over the whole logical
  `PhysicsApi` surface (header + implementation + smoke harness) confirming
  one simulation, no authority escape, and no dead contract types. Final
  gates below from final source.

## Dependencies And Decisions

- Depends on no other plan; first in the round-2 campaign binding order.
- Owner decision ratified at registration: delete-and-re-host (not "finish
  the migration by teaching the standalone world the real solver"); the
  production solver is the only simulation.
- Any PU0 discovery of an external consumer beyond the smoke probe is
  recorded and ruled before PU2 proceeds.

## Acceptance

- Zero classes in the tree implement a second physics `Step()`.
- The smoke lane still runs one engine process and exits zero with
  lifecycle + determinism assertions backed by `PhysicsEngine`.
- Physics CSV baselines unchanged, byte-exact.
- Independent review records no compatibility spelling, forwarding shape, or
  dead contract type.

## Validation

- Per implementation task: focused build plus the targeted doctest/smoke run
  answering that task's question.
- PU2/PU3/PU4 pre-commit: `tools\validate_physics.bat` (byte-exact CSV plus
  engine-lifecycle-smoke lane) and, at PU4 closure, `tools\validate_full.bat`.
- No behavioral baseline, golden, screenshot, or replay refresh. Divergence
  is reverted, never normalized.

## PU2-PU3 Implementation Evidence (2026-07-22)

- `PhysicsStandaloneWorld`, its result/step/activation/update/view types, and
  `PhysicsApi.cpp` are deleted. Project/filter metadata and the two obsolete
  allocation exceptions are removed; live source has no compatibility wrapper
  or second `Step()` owner.
- `PhysicsEngine` now owns stable point-joint update/destroy commands and
  conservative fixed-scratch ray/AABB queries. The startup probe runs two fresh
  heap-owned engines through the production step with an explicit flat terrain
  owner and inline zero-worker pool.
- The exact accepted lifecycle state is two bodies, two colliders, one step,
  one broadphase result, one terrain contact, two island rows, final position
  `(3.25, 2, 0)`, final velocity `(1, 0, 0)`, and hash
  `0x466576DF4B00186F` on both fresh runs.
- Touched-file comment audit: 14 retained source files checked, zero deferred;
  stale standalone/facade ownership wording was removed. The deleted
  `PhysicsApi.cpp` has no retained comment surface.
- Focused Debug build passed (18.21 s); focused smoke passed (3.31 s).
  Pre-commit gates passed: `tools\validate_fast.bat` (76.55 s, zero warnings),
  allocation checker self-test plus repository scan (9.34 s, 425 files, zero
  allowlist errors), and `tools\validate_physics.bat` (26.15 s, exact smoke
  hash and 44,401-line byte-exact CSV). All dependency-direction and downward
  Replay-include proofs returned no rows. No baseline/golden file changed.
