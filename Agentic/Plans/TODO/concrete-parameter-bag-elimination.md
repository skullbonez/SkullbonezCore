# Concrete Parameter-Bag Elimination

Date: 2026-07-26
Owner: skullbonez
State: IN PROGRESS (PB0-PB1 complete; PB2 next)
Ledger tasks: 8 (PB0-PB7)
Branch: `nightrunner-25th-JUL-26`

## Goal

Eliminate every large authority-free parameter bag found by the 2026-07-26
owner-requested audit. PB0 ratified the 22 registered target shapes and added
eight repair rows at implementation tip `e61e82a6`. The final inventory is 30
repair rows plus three existing 13-parameter render/UI operations.

The endpoint is concrete composition:

- an owner publishes a cohesive value through a concrete method such as
  `GetSaveState()`;
- a caller passes small domain values directly when no aggregate owns a rule;
- a multi-step operation uses a concrete, stack-scoped transaction with an
  enforced phase cursor when ordering or arbitration is the real invariant.

No inheritance or interface is permitted as part of this remediation. In
particular, do not introduce `ISerializable`, any other `I*` abstraction,
abstract bases, pure virtual methods, virtual dispatch, CRTP policy bases,
type-erased adapters, callback interfaces, or inheritance-based visitors.
Every replacement is a concrete value or concrete owner operation with static
dispatch.

## Why This Is A New Plan

The completed 2026-07-23 wide-signature campaign removed the mechanical bags
introduced by its own arity-reduction work and closed against a threshold-13
function inventory. This audit is broader: it examines current aggregate
construction, repeated owner projection, immediate repacking, and service
bundles regardless of which campaign introduced them.

The completed invariant-ownership plan remains authoritative through
`../../Reports/2026-07-26/invariant-ownership-governance-and-transaction-repair-closure.md`
for the governance rule and the scene-load transaction. This plan owns the
complete 30-row closure. It consumes GV2's scene-load result instead of
implementing a competing transaction, then closes every remaining named row.

## Binding Design Rules

1. **No inheritance or interfaces.** The ban applies to production code,
   tests, compatibility shims, and temporary migration seams introduced by
   this plan.
2. **No renamed bags.** Replacing `*Context`, `*Input`, or `*Request` with
   `*State`, `*View`, `*Snapshot`, `*Services`, `*Bindings`, or `*Transaction`
   is not progress unless the replacement has a domain identity independent of
   parameter count or owns and tests an enforceable invariant.
3. **Concrete owner state is narrow.** `GetSaveState()` or an equivalent
   method returns only values owned by that concrete owner. It cannot collect
   fields from sibling owners, retain owner pointers, or expose a route back
   into mutable authority.
4. **Transactions own rules, not services.** A transaction may retain values
   and a phase cursor. Borrowed concrete owners enter phase methods and expire
   when those calls return. The transaction stores no long-lived owner
   pointer/reference and no callback.
5. **Hot paths stay static and allocation-free.** Physics and render repairs
   use concrete types, spans, fixed storage, and direct calls. No virtual call,
   heap allocation, callback chain, handle lookup, or generic serialization
   mechanism may enter a per-frame or per-body path.
6. **Dependency direction remains visible.** Do not hide an illegal package
   edge behind an interface, base class, forwarding header, alias, or erased
   function object.
7. **Behavior is frozen.** No baseline, golden, screenshot, Replay artifact,
   scene, configuration, allocation-policy inventory, or physics CSV refresh
   is authorized. Divergence is reverted rather than normalized.
8. **The 12-parameter ceiling remains binding.** Splitting a bag into explicit
   concrete operations does not authorize a signature above the accepted
   ceiling.

## Authoritative Target Census

Every row is repair-required. There are no retain-as-is rows.

| # | Target | Current location | Required endpoint |
|---:|---|---|---|
| 1 | `SceneSaveRequest` | `SkullbonezSource/Scene/SceneSnapshotWriter.h` | Retain the domain request name only if reduced to output path plus concrete Scene, controller-state, and presentation save values |
| 2 | `SceneSaveView` | `SkullbonezSource/Scene/SceneSnapshotWriter.h` | Delete the duplicate borrowed projection; the writer consumes the composed save request or its owner values directly |
| 3 | `SceneLoadPolicyInputs` | `SkullbonezSource/Runtime/Scene/SceneController.h` | Delete as a caller-built service/value bundle through GV2's concrete scene-load transaction |
| 4 | `SceneLoadConsumerOutputs` | `SkullbonezSource/Runtime/Scene/SceneController.h` | Remove from caller-visible parameters; outputs belong to the concrete scene-load transaction |
| 5 | `RuntimePointerRouteInput` | `SkullbonezSource/Runtime/Input/InputRouter.h` | Delete; route concrete gesture/ray values into focused owner operations |
| 6 | `EditorPointerRouteInput` | `SkullbonezSource/Runtime/Input/InputRouter.h` | Delete the second mechanical projection in the pointer chain |
| 7 | `MousePickupPointerInput` | `SkullbonezSource/Runtime/Tools/RuntimeTools.h` | Delete; mouse pickup consumes its small concrete gesture and world-ray facts directly |
| 8 | `RenderFrameContext` | `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h` | Delete the 45-field frame service bag; concrete RuntimeRenderer phases consume the existing frame publication and owned render resources |
| 9 | `UiTextPassInputs` | `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h` | Delete; split text composition into concrete owner operations with focused values |
| 10 | `ReplayOverlayRenderContext` | `SkullbonezSource/Runtime/Planning/ReplayOverlayPackets.h` | Delete; Planning publishes immutable overlay values and Runtime/Render performs concrete draw composition |
| 11 | `ReplayCaptureInput` | `SkullbonezSource/Runtime/Replay/ReplayRecorder.h` | Replace the multi-owner capture bag with owner-produced concrete capture values and a focused recorder operation |
| 12 | `ReplayCameraFocusRequest` | `SkullbonezSource/Runtime/Replay/ReplayPresentation.h` | Delete the one-for-one camera-state copy; construct or mutate the concrete Replay camera state at its owner |
| 13 | `PersistentContactSolverContext` | `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h` | Delete; the concrete contact-solver stage owns phase coordination and accepts focused store/settings views |
| 14 | `PhysicsContactSolverStageContext` | `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h` | Delete the outer-to-inner repacking layer |
| 15 | `ObjectNarrowphasePairStageContext` | `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.h` | Delete; the concrete narrowphase stage consumes focused Physics-owned views and per-step values |
| 16 | `PhysicsSleepIslandStageContext` | `SkullbonezSource/Physics/Stages/PhysicsSleepController.h` | Delete; the concrete sleep controller owns island-phase sequencing |
| 17 | `PhysicsSleepWakeContext` | `SkullbonezSource/Physics/Stages/PhysicsSleepController.h` | Delete the wake service bundle; pass focused same-owner views to concrete wake operations |
| 18 | `PhysicsBroadphaseStageContext` | `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h` | Delete; broadphase consumes focused body, collider, sleep, settings, and step values without a master context |
| 19 | `ExternalForceBodyContext` | `SkullbonezSource/Physics/Stages/ExternalForceStage.h` | Delete; external-force application uses its real frame value plus explicit concrete Physics collaborators |
| 20 | `TerrainCandidateCommitContext` | `SkullbonezSource/Physics/Stages/PhysicsTerrainStage.h` | Delete; commit values stay in `PreparedTerrainCandidateCommit`, while concrete stores enter commit methods directly |
| 21 | `ReplayRestoreOwnerContext` | `SkullbonezSource/Runtime/App/ReplayValidation.cpp` | Delete the restore service bag; use a concrete stack-scoped restore transaction with borrowed phase-method owners |
| 22 | `ReplayRestoreStepContext` | `SkullbonezSource/Runtime/App/ReplayValidation.cpp` | Delete the second restore repacking layer; target/checkpoint values belong to the concrete transaction |

PB0 added eight binding repair rows:

1. `EditorSaveHotkeyContext`;
2. `UiTextPassState`;
3. the 13-member private Runtime render graph `*GraphCallbackData` family;
4. the Replay restore support family named in the PB0 report;
5. `BroadphaseCandidateFilterContext`;
6. `TerrainDetectionStageContext`;
7. `ApplyForcesStageContext`;
8. `IntegrateRemainingStageContext`.

The authoritative file:line census, endpoint sketches, explicit retain
rulings, and task/gate mapping are in
`../../Reports/2026-07-26/concrete-parameter-bag-elimination-pb0-census.md`.
No registered row was removed or retained.

## Required Concrete Endpoints

### Scene save

The intended call-site shape is:

```cpp
const SceneSaveRequest saveRequest {
    snapshotOutPath,
    m_sceneController.Scene().GetSaveState(),
    m_sceneController.State().GetSaveState(),
    presentation.GetSaveState()
};
```

Names may be refined to the actual concrete owners, but the ownership shape is
binding. Each `GetSaveState()` returns a value owned entirely by that owner.
`SceneSnapshotWriter` must not reconstruct a second flat view, and all save
call sites must use the same complete state path. Focused tests must prove that
editor save, runtime save, and load-triggered save preserve every serialized
field; this specifically closes the existing partial-initialization drift.

### Scene load

GV2 supplies the concrete `SceneLoadTransaction` and phase cursor. This plan
requires `SceneLoadPolicyInputs` and caller-visible
`SceneLoadConsumerOutputs` to disappear from the final source and verifies
that the transaction stores values/cursor only. No base transaction,
transaction interface, visitor, or callback seam is allowed.

### Pointer routing

Define the smallest real values that already have independent meaning, such as
a pointer gesture/edge value and a world-ray value, then call the editor,
mouse-pick, and Replay owners directly. Do not create a replacement
`PointerRouteState` containing the union of all three consumers' needs. Each
consumer must receive only the values it uses, and the repeated
Runtime-to-Editor-to-Tools field copying must disappear.

### Render and UI

`RuntimeRenderer` remains the concrete render composition owner. Replace
`BuildRenderFrameContext` and the 45-field context with explicit concrete
frame phases that combine the existing `RuntimeRenderModelFrameView` with
renderer-owned resources. A concrete stack transaction is allowed only if it
owns and tests render phase order and stores no service references.

UI text and Replay overlay preparation split by output responsibility.
Planning publishes immutable overlay values; Runtime/Render translates those
values using its concrete renderer/UI owners. No render-pass interface,
backend interface, inherited pass type, or service registry is permitted.

### Replay

Replay capture composes concrete owner-produced values for timing/branch,
Scene, presentation, Physics, and gameplay-owned visual state. A value is
legitimate only when it has a domain consumer beyond shortening the capture
call and is not immediately unpacked into the former field list.

Camera focus applies the selected target to the concrete Replay presentation
camera owner without copying a 15-field request into an isomorphic state.

Replay restore becomes a concrete stack-scoped transaction. It owns artifact,
checkpoint, target, result, and phase-cursor values; concrete owners are
borrowed by phase methods only. Illegal phase transitions are lane-F fatal and
the pure cursor receives exhaustive focused tests.

### Physics

Do not replace the eight Physics contexts with one `PhysicsStepContext`.
Concrete stage owners consume existing store-produced hot/cold views, spans,
settings values, and step scalars directly. Stable same-owner dependencies may
move to a concrete stage constructor only when lifetime is explicit and the
stage cannot use the reference to recover unrelated `PhysicsWorld` authority.

Contact solver, narrowphase, broadphase, sleep, external-force, and terrain
repairs preserve exact loop order, floating-point operation order, diagnostic
emission order, fixed capacities, and worker partitioning. No virtual stage
base, `IPhysicsStage`, visitor, callback sink, allocator-backed command list,
or type erasure is allowed.

## Ledger

- [x] **PB0 - Ratify the implementation-tip census and owner designs.**

  Re-run the aggregate/construction audit after the prerequisite plans land.
  Record every definition, construction site, consumer, immediate repack, and
  owner boundary for the 22 rows. Add any newly discovered mechanical shape.
  For every row/group, record the final concrete owner, the values it may
  retain, the values/owners borrowed per call, and the exact symbol deletion
  or approved `SceneSaveRequest` field transformation.

  Acceptance:

  - all 22 rows have current file:line and caller evidence;
  - every additional hit receives repair or an owner-approved retain ruling;
  - every repair has a concrete, non-inherited endpoint sketch;
  - no source changes; documentation-only, no repository validation.

  Evidence:

  - implementation tip `e61e82a6`;
  - 22/22 registered rows have current definition, construction, consumer,
    repack, owner, lifetime, deletion-proof, and validation evidence;
  - eight repair rows and three existing ceiling defects were assigned;
  - every other reviewed hit has an explicit retain ruling;
  - permanent report:
    `../../Reports/2026-07-26/concrete-parameter-bag-elimination-pb0-census.md`.

- [x] **PB1 - Repair Scene save and consume the scene-load transaction.**

  Introduce the three concrete owner-produced save values, reduce
  `SceneSaveRequest` to path plus those values, delete `SceneSaveView`, and
  migrate every save call site. Consume GV2's concrete scene-load transaction
  and prove the two scene-load bag rows are gone. Split
  `EditorSaveHotkeyContext` into focused scene-save and screenshot operations.

  Acceptance:

  - every save producer uses the same complete concrete owner-state path;
  - focused serialization tests cover every persisted field and all save entry
    points;
  - `SceneSaveView`, `SceneLoadPolicyInputs`, and
    `SceneLoadConsumerOutputs` have zero definitions/usages;
  - `SceneSaveRequest` contains only path and three owner-produced values;
  - `EditorSaveHotkeyContext` has zero definitions/usages;
  - no inheritance, interface, callback, service bag, or behavior change.

  Evidence:

  - `SceneSaveRequest` is path plus `SceneWorldSaveState`,
    `SceneSessionSaveState`, and `PresentationSaveState`;
  - all three production save entry points use concrete owner publications;
  - the four retired symbols have zero definitions/usages;
  - changed Scene/load/editor operations remain below 13 parameters;
  - focused tests cover every persisted policy/environment/camera value and
    the existing object/asset round trip;
  - comment audit: 20/20 touched source files checked, 0 deferred;
  - `validate_tests.bat`: 398/398 tests, 2,403,462 assertions;
  - authoritative `validate_full.bat`: PASS in 173.3 seconds with DX12
    baselines unchanged and the 44,401-line physics oracle byte-exact;
  - permanent report:
    `../../Reports/2026-07-26/concrete-parameter-bag-elimination-pb1-scene.md`.

- [ ] **PB2 - Collapse the pointer-routing projection chain.**

  Delete the Runtime, Editor, and mouse-pick pointer bags. Route real gesture,
  ray, selection, and command values directly to their concrete owners while
  preserving arbitration and consumption order.

  Acceptance:

  - all three target symbols have zero definitions/usages;
  - no union-of-consumer-needs replacement type exists;
  - editor interaction, mouse pickup, Replay picking, and UI-consumption tests
    cover the unchanged precedence rules;
  - no inheritance, interface, callback, retained host pointer, or allocation.

- [ ] **PB3 - Remove render-frame, UI-text, and Replay-overlay service bags.**

  Delete `RenderFrameContext`, `UiTextPassInputs`, and
  `ReplayOverlayRenderContext`, eliminate `UiTextPassState` and the PB0-named
  graph callback payload family, and repair the three assigned 13-parameter
  operations. Give concrete render phases and owners the values they actually
  consume, retaining `RuntimeRenderModelFrameView` only as the previously
  ruled Scene-to-render publication.

  Acceptance:

  - the three target symbols and `BuildRenderFrameContext` have zero
    definitions/usages;
  - `UiTextPassState`, all 13 PB0-named graph callback payloads, and all three
    13-parameter operation definitions are gone or below the ceiling through
    concrete phase decomposition;
  - no replacement master render context or pass-interface hierarchy exists;
  - pass order, resource lifetime, overlay semantics, UI text, allocation
    behavior, and render fingerprints remain exact;
  - DX12 and graphics-stress gates pass with zero refresh.

- [ ] **PB4 - Repair Replay capture, camera focus, and restore.**

  Replace `ReplayCaptureInput` with concrete owner-produced capture values,
  remove the isomorphic camera-focus request, and implement the concrete
  phase-checked restore transaction that deletes both restore contexts and
  the PB0-named restore support owner bags/callback seams.

  Acceptance:

  - all four target symbols have zero definitions/usages;
  - `ReplaySolverSampleRestoreContext`, `ReplayArtifactTopologyOwners`,
    `ReplayRestoreEventContext`, and `ReplaySceneTimelineResetOwners` are
    deleted, while `ReplayRestoreTransaction` is the value-and-cursor owner;
  - capture values have independent domain identity and are not immediately
    flattened;
  - restore order is enforced by a tested cursor and lane-F transition checks;
  - the transaction stores no owner pointer/reference or callback;
  - Replay artifact, scrub, visual-fidelity, allocation, and broad behavior
    gates pass with zero refresh.

- [ ] **PB5 - Repair Physics collision and solver stage bags.**

  Delete `PersistentContactSolverContext`,
  `PhysicsContactSolverStageContext`, `ObjectNarrowphasePairStageContext`, and
  `PhysicsBroadphaseStageContext` plus PB0-added
  `BroadphaseCandidateFilterContext`. Use concrete stage owners and focused
  store/settings/step values without changing hot-loop order.

  Acceptance:

  - all five target symbols have zero definitions/usages;
  - no `PhysicsStepContext`, virtual stage base, interface, callback, or
    allocation replaces them;
  - focused determinism, collision, solver, and broadphase tests pass;
  - Physics, performance, and broad validation pass with byte-exact CSVs.

- [ ] **PB6 - Repair Physics sleep, force, and terrain stage bags.**

  Delete `PhysicsSleepIslandStageContext`, `PhysicsSleepWakeContext`,
  `ExternalForceBodyContext`, `TerrainCandidateCommitContext`,
  `TerrainDetectionStageContext`, `ApplyForcesStageContext`, and
  `IntegrateRemainingStageContext`. Keep the concrete stage owners, existing
  fixed storage, and prepared terrain commit value while making per-operation
  borrows explicit.

  Acceptance:

  - all seven target symbols have zero definitions/usages;
  - sleep/wake sequencing, external-force accumulation, terrain commit order,
    diagnostics, worker partitioning, and allocation behavior remain exact;
  - no inheritance, interface, callback, master Physics context, or owner
    reach-back exists;
  - Physics, performance, and broad validation pass with byte-exact CSVs.

- [ ] **PB7 - Prove complete closure.**

  Re-run the PB0 audit from final source, reconcile every target and added row,
  audit every touched source-bearing file under the comment-style skill, and
  run one independent hostile review. The review mandate is to find renamed
  bags, inheritance/interface indirection, service retention, immediate
  repacking, widened signatures, hot-path allocation, and behavior drift.

  Acceptance:

  - all 30 rows and all three assigned ceiling defects are closed;
  - the original bad full-field `SceneSaveRequest` construction has zero
    matches and all save sites use concrete owner save values;
  - no inheritance, interface, virtual method, type erasure, callback pack,
    replacement context bag, or unruled aggregate appears in the diff;
  - the final comment-audit inventory is complete;
  - one independent hostile review has no unresolved finding;
  - all cumulative mapped gates and `tools\validate_full.bat` pass with zero
    refresh;
  - closure evidence is written under `Agentic/Reports/<date>/`, this plan is
    removed under inventory rule 4, and `MASTER-PLAN.md` plus
    `Agentic/SessionState.md` record the handoff.

## Dependencies And Coordination

- Binding order prerequisites (Replay partition RS5, downward-domain-bleed
  DB0-DB5, and invariant-ownership GV1-GV4) are complete. PB0-PB7 is now the
  active sequence.
- GV2 owns the first implementation of the concrete scene-load transaction.
  PB1 consumes and verifies it; it does not introduce a second transaction.
- GV1's ruled census feeds PB0. The 30 rows in this plan are assigned here,
  not duplicated in GV3; GV3 remains responsible for additional
  invariant-shaped offenders outside this explicit bag inventory.
- Replay package names and include paths are taken from the post-RS5 tree.
- Physics owner placement is taken from the post-DB5 tree. PB5/PB6 may not
  reintroduce an edge DB5 removed.
- PB3 and PB4 coordinate on the Planning-to-Render overlay value seam; neither
  may create an upward Replay/Planning provider dependency in engine layers.

## Static Closure Proofs

The following symbol inventory must return no rows at closure except
`SceneSaveRequest`, which is allowed only in its reduced four-field form:

```powershell
rg -n 'SceneSaveView|SceneLoadPolicyInputs|SceneLoadConsumerOutputs' SkullbonezSource
rg -n 'EditorSaveHotkeyContext' SkullbonezSource
rg -n 'RuntimePointerRouteInput|EditorPointerRouteInput|MousePickupPointerInput' SkullbonezSource
rg -n 'RenderFrameContext|UiTextPassInputs|UiTextPassState|ReplayOverlayRenderContext|BuildRenderFrameContext' SkullbonezSource
rg -n '(CinematicPost|Shadow|Reflection|Object|Terrain|Water|DebugOverlay|SceneTarget|BackbufferAcquire|Skybox|UiText|ReplayGhost|DevelopmentUi)GraphCallbackData' SkullbonezSource
rg -n 'ReplayCaptureInput|ReplayCameraFocusRequest|ReplayRestoreOwnerContext|ReplayRestoreStepContext' SkullbonezSource
rg -n 'ReplaySolverSampleRestoreContext|ReplayArtifactTopologyOwners|ReplayRestoreEventContext|ReplaySceneTimelineResetOwners' SkullbonezSource
rg -n 'PersistentContactSolverContext|PhysicsContactSolverStageContext|ObjectNarrowphasePairStageContext|PhysicsBroadphaseStageContext|BroadphaseCandidateFilterContext' SkullbonezSource
rg -n 'PhysicsSleepIslandStageContext|PhysicsSleepWakeContext|ExternalForceBodyContext|TerrainCandidateCommitContext|TerrainDetectionStageContext|ApplyForcesStageContext|IntegrateRemainingStageContext' SkullbonezSource
```

Review the diff for inheritance/interface substitutions; do not add a
repository-wide spelling or count ratchet:

```powershell
git diff --unified=0 <base>...HEAD -- SkullbonezSource |
  rg '^\+.*(class I[A-Z]|virtual |override\b|std::function|Callback|Services|Bindings)'
```

Any hit is reviewed manually and blocks closure when introduced by this plan.

## Validation Map

| Phase | Iteration evidence | Pre-commit/closure gates |
|---|---|---|
| PB0 | Census, call-site map, concrete owner sketches | Documentation-only; no repository validation |
| PB1 | Focused serialization and scene-lifecycle tests | `tools\validate_tests.bat`, `tools\validate_full.bat` |
| PB2 | Focused editor/mouse-pick/Replay interaction tests | `tools\validate_fast.bat`, then `tools\validate_full.bat` at slice close |
| PB3 | Render fingerprints and focused UI/overlay tests | `tools\validate_dx12_renderer.bat`, `tools\run_graphics_stress.bat 1`, `tools\validate_perf.bat` |
| PB4 | Replay capture/restore/camera tests | Replay artifact, scrub, visual-fidelity, allocation-policy gates, then `tools\validate_full.bat` |
| PB5 | Collision/solver/broadphase determinism tests | `tools\validate_physics.bat`, `tools\validate_perf.bat`, `tools\validate_full.bat` |
| PB6 | Sleep/force/terrain determinism tests | `tools\validate_physics.bat`, `tools\validate_perf.bat`, `tools\validate_full.bat` |
| PB7 | Final census, comment audit, hostile review | All cumulative mapped gates plus `tools\validate_full.bat` |

## Closure Evidence Requirements

The closure report must contain:

- the final PB0 census with all 30 repair rows and three assigned ceiling defects;
- before/after construction examples for each group;
- exact definitions/usages deletion proof per target;
- the reduced `SceneSaveRequest` definition and all call sites;
- concrete-owner and transaction invariant evidence, including phase-cursor
  tests where used;
- an explicit statement that no inheritance or interface was introduced;
- hot-path allocation and dependency-direction review results;
- focused and mapped validation outputs with zero-refresh confirmation;
- the complete touched-source comment-audit inventory;
- the independent hostile-review verdict and all remediation.
