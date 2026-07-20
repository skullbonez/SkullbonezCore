# Physics Facade Unification

Status: In progress — 2/3 tasks (F0-F2)
Owner: repository owner; registered 2026-07-20 as campaign plan 2 of 8
Evidence: `../../Reports/2026-07-20/engine-architecture-review.md` (finding C)
Ledger: F0-F2
Depends on: `dependency-direction-restoration` L0-L2 recommended first (clean
include tree makes the absorb mechanical); not a hard blocker.

## Objective

Delete the PhysicsEngine/PhysicsScene double facade. **Owner decision
2026-07-20: `PhysicsEngine` survives; `PhysicsScene` is absorbed and
deleted.** One public physics owner remains behind `PhysicsApi.h`
descriptors, with identical forwarding order and byte-exact behavior.

## Problem / Evidence

`PhysicsEngine.h` (204 lines) and `PhysicsScene.h` (235 lines) expose
near-identical method lists; `PhysicsEngine.cpp` is 469 lines of 1:1
forwarders. `PhysicsEngine.h` self-describes as migration scaffolding. This
is the nominal forwarding owner shape the Migration Cleanup Review Rule
forbids retaining, and it doubles the surface every physics API change must
touch.

## Non-Goals

- No behavior, ordering, or diagnostics change: PhysicsEngine's forwarder
  sequencing comments say forwarders must not reorder solver, store-refresh,
  replay, or diagnostics calls — the absorb preserves call order exactly.
- No PhysicsApi.h redesign; descriptor/command/view contracts are untouched.
- No PhysicsWorld or stage changes.
- No baseline, golden, or replay artifact refresh.

## Binding Decisions

1. `PhysicsScene.{h,cpp}` are deleted; their implementation moves into
   `PhysicsEngine.{cpp}` (plus a private header only if TU size demands it —
   any split must be a cohesive owner file, not a mechanical TU split).
2. No compatibility alias, `using PhysicsScene = ...`, or forwarding header
   may remain.
3. Method bodies move verbatim except for member-name mechanics; any
   non-mechanical edit is called out in the commit body.
4. Callers that name `PhysicsScene` directly (if any exist outside
   PhysicsEngine) are migrated to `PhysicsEngine` in the same commit.

## F0 Inventory

Inventory completed 2026-07-20 from the current CodeGraph plus exact source and
project searches.

### Surface Counts

| Surface | Public entries | Shape |
|---|---:|---|
| `PhysicsEngine` | 63 | Default constructor, 49 command/query relays, and 13 immutable read projections. |
| `PhysicsScene` | 51 | Constructor, the 49 relay targets, and one aggregate `ReadView`. |

The 12-entry difference is exact: thirteen field-specific Engine readers
replace one Scene aggregate reader. `PhysicsScene` also has three private
helpers that move with its state: `LoadBodyDescriptors`,
`ApplyFixedTreeReleaseEvents`, and Debug-only
`ValidatePhysicsStoreMappings`.

### Forwarder Map

Every `PhysicsEngine.cpp` method contains exactly one `m_scene` target. The 46
identity relays below map to the same-named `PhysicsScene` method:

| Group | Engine method → Scene method |
|---|---|
| Lifecycle/configuration | `BindProfiler`, `ApplyRuntimeConfig`, `ApplyAuthoredBodyPolicy`, `ApplyAuthoredColliderPolicy`, `ReserveAuthoredBodyCapacity`, `AuthoredBodyDescriptorCount`, `CanRegisterAuthoredBody`, `TrimAuthoredBodyDescriptorsToCount`, `Clear`, `RefreshBodyStoreFromAuthoredDescriptors` → same name |
| Authoring/replay state | `RegisterAuthoredBody`, `DestroyAuthoredBody`, `UpdateAuthoredBody`, `UpdateAuthoredBodyAndCollider`, `ClearPendingBodyImpulses`, `TrimBodiesToCount`, `TrimCollidersToCount`, `RestoreReplayBodyState`, `RefreshColliderSnapshot` → same name |
| Live commands | `WakeBody`, `ReleaseFixedBodyAndAttachedTreeParts`, `SetBodyVelocity`, `SeedBodyAsleep`, `SetPendingBodyImpulse`, `ApplyBodyImpulse`, `BeginCollisionVisualFrame`, `EndCollisionVisualFrame`, `ClearPointJointConstraints`, `CreatePointJoint` → same name |
| Tornado/replay/diagnostics | `SetTornadoFieldConfig`, `GetTornadoFieldConfig`, `SetTornadoSystemConfig`, `GetTornadoSystemConfig`, `GetTornadoSystemElapsedSeconds`, `CaptureReplaySolverSnapshot`, `RestoreReplaySolverSnapshot`, `GetDiagnosticsView`, `CollectPhysicsWorldMemoryBytes`, `CollectDebugAndBroadphaseMemoryBytes`, `ShouldEmitStepDiagnostics`, `ShouldEmitCollisionTimeDiagnostics` → same name |
| Debug-only controls | `SetPhysicsRegressionLogPath`, `SetPhysicsCollisionTimeLogPath`, `SetPhysicsDiagnosticsPath`, `SetPhysicsDiagnosticsRunId`, `SetDiagnosticsSuppressed` → same name |

The three name-translated relays are:

| Engine method | Scene target | Extra behavior |
|---|---|---|
| `Step` | `RunPhysics` | None; parameter order is identical. |
| `SetSleepEnabled` | `SetPhysicsSleepEnabled` | None. |
| `IsSleepEnabled` | `IsPhysicsSleepEnabled` | None. |

The thirteen immutable read relays each call `ReadView` once and return one
field:

| Engine reader | `PhysicsSceneReadView` field |
|---|---|
| `ReadBodies` | `bodies` |
| `ReadColliders` | `colliders` |
| `ReadSpatialGrid` | `spatialGrid` |
| `ReadFixedContactHighlightBodies` | `fixedContactHighlightBodies` |
| `ReadCollisionCellKeys` | `collisionCellKeys` |
| `ReadCollisionVisualContacts` | `collisionVisualContacts` |
| `ReadSleepStates` | `sleepStates` |
| `ReadSleepIslandVisualIds` | `sleepIslandVisualIds` |
| `ReadSleepSupportedStates` | `sleepSupportedStates` |
| `ReadSleepInhibitedStates` | `sleepInhibitedStates` |
| `ReadDebugContacts` | `debugContacts` |
| `ReadPipelineTrace` | `pipelineTrace` |
| `ReadPointJointConstraints` | `pointJointConstraints` |

No forwarder branches, mutates unrelated state, reorders calls, or invokes a
second target. `RegisterAuthoredBody` and `UpdateAuthoredBodyAndCollider` only
move their by-value collider argument into the Scene call; the absorb retains
that ownership transfer. The thirteen read projections are the only logic
beyond direct return/call syntax and remain field-for-field on PhysicsEngine.

### Direct Users And F1 Edit Census

There are zero direct `PhysicsScene` type users outside `PhysicsEngine`.
`PhysicsEngine.h` is the sole owner and include site. Four project/filter rows
register `PhysicsScene.h/.cpp` and must be deleted in F1. Remaining external
source rows are comments or Related paths in `ColliderStore.h`,
`PhysicsBodyStore.h`, `PhysicsApi.h`, `PhysicsWorld.h/.cpp`,
`RenderInstanceStore.h`, `SceneAuthoredSetup.cpp`, and `SceneWorld.cpp`; F1
updates those owner words/paths to `PhysicsEngine`.

The absorbed state is cohesive: `PhysicsWorld`, authored body descriptors,
body/collider stores, physics material, body limits, contact policy, last world
forces/validity, and the reused fixed-tree wake list all remain together on the
single `PhysicsEngine` owner.

## F1 Result

Completed 2026-07-20. `PhysicsScene.h/.cpp` and their four project/filter rows
are deleted. Their three private helpers, cohesive state, and method bodies now
live directly on `PhysicsEngine`; the public Engine names `Step`,
`SetSleepEnabled`, and `IsSleepEnabled` remain unchanged, and the thirteen
immutable readers project the same owned fields directly instead of building an
aggregate read view.

The only diagnostic-text mechanic is the fatal owner label moving from
`Physics/PhysicsScene` to `Physics/PhysicsEngine`, matching the surviving owner.
The first targeted compile exposed an over-broad comment replacement that had
also changed the policy-mandated `PhysicsSceneObjectId` token; the exact identity
name was restored before successful builds or formal validation. Final Profile
and Debug targeted builds passed in 48.03s with zero warnings/errors.

Comment audit checked all 10 surviving touched source-bearing files with zero
deferred. No test names the standalone facade, so F1's conditional
`validate_tests` lane was not required. `tools\validate_physics.bat` passed in
81.55s with a byte-exact 44,401-line regression baseline and zero build
warnings/errors. No baseline or artifact changed.

## Tasks

- [x] F0 — Inventory: enumerate both public surfaces, map every forwarder to
  its target, list all direct `PhysicsScene` users outside PhysicsEngine, and
  record any forwarder that is not a pure relay (extra logic must be
  preserved and named). Output: inventory table in this plan or the closure
  report. No validation (documentation).
- [x] F1 — Absorb and delete: move PhysicsScene implementation into
  PhysicsEngine, delete `PhysicsScene.h/.cpp`, update includes and project
  files, migrate any direct callers, keep call order verbatim. Apply the
  comment standard to the touched files (facade vocabulary now lives on
  PhysicsEngine). Validation: `tools\validate_physics.bat` (byte-exact CSV)
  and `tools\validate_tests.bat` if any test names PhysicsScene.
- [ ] F2 — Closure: grep proof that the standalone `PhysicsScene` type,
  `PhysicsSceneReadView`, and `PhysicsScene.h/.cpp` appear nowhere in source or
  project files; `PhysicsSceneObjectId` remains required by the repository's
  stable identity policy and is explicitly excluded. Independent rubber-duck
  review confirms no authority moved anywhere except into PhysicsEngine and no
  compatibility spelling survives; final gates. Validation:
  `tools\validate_physics.bat` plus `tools\validate_full.bat` at closure tip.

## Acceptance

- Exactly one concrete public physics owner (`PhysicsEngine`) remains behind
  `PhysicsApi.h`; the standalone `PhysicsScene` type is deleted with zero
  aliases while the policy-mandated `PhysicsSceneObjectId` identity remains.
- Physics regression CSV is byte-exact against committed baselines.
- Independent review is clear; a credible forwarding/authority finding
  reopens F1.

## Validation Summary

F1: `validate_physics` (+ `validate_tests` when tests are touched).
F2: `validate_physics` + `validate_full` at final source.
