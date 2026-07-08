# Progress: Stable Identity (plan 06)

Source plan: `fable_plans/06-stable-identity-plan.md`
Status: phase 2 central resolvers complete on 2026-07-08; C1 mouse-pickup redundant-row sub-slice complete; subsystem conversions pending
Last updated: 2026-07-08

## How to work this file

- Do items in order; one checkbox = one verifiable action; tick only with the
  named evidence pasted under the box. `[B]` + reason if blocked twice.
- Anchors are file + search string; locate with `rg -n "<anchor>" <file>`.
- Comment quality gate applies to every touched source file.

## Verified facts (do not re-derive)

- Identity types:
  - `Physics::PhysicsBodyHandle` / `PhysicsColliderHandle` —
    `SkullbonezSource/Physics/PhysicsHandles.h:37/:48` — `{ uint32_t index;
    uint32_t generation; IsValid(); }`, invalid = `INVALID_PHYSICS_HANDLE_INDEX`
    or generation 0.
  - `ReplayBodyId` — `Runtime/Replay/ReplayRecorder.h:70` — `{ uint32_t value; }`,
    0 = null.
- Store resolvers already exist (headers):
  - `PhysicsBodyStore::HandleForModelIndex(int)` (PhysicsBodyStore.h:171),
    `ModelIndexForHandle(PhysicsBodyHandle)` (:175),
    `ResolveHandleForModelIndex(int, ...)` (:211).
  - `ColliderStore::HandleForModelIndex` (:104), `ModelIndexForHandle` (:113),
    `ResolveHandleForModelIndex` (:121).
  - `TryResolveReplayBodyModelIndex( const PhysicsBodyStore&, ReplayBodyId,
    int hint, int modelCount, int& out )` — RunReplayTools.cpp:158 (replay-id
    → model-index with hint fast path).
- Initial persisted model-index inventory (28 scalar members in 9 headers, plus
  1 non-scalar drag-group row) was frozen on 2026-07-08. The checker excludes
  function default parameters such as `Run.h:190`; those are not stored
  identity. The current scalar ratchet is 27 after the C1 mouse-pickup
  redundant row was removed.

| # | File:Line | Member | Adjacent stable id? | Class |
|---|-----------|--------|---------------------|-------|
| 1 | GameObjects/GameModelCollection.h:135 | SceneObjectGroupCreateDesc.rootModelIndex | scene group create metadata | recorded |
| 2 | GameObjects/GameModelCollection.h:167 | SceneObjectGroupRecord.rootModelIndex | scene group sidecar | recorded |
| 3 | Runtime/Replay/ReplayInteractionController.h:84 | ReplayVelocityEditDragStart.modelIndex | gesture-start packet; not retained by ReplayRuntime | frame-local |
| 4 | Runtime/Replay/ReplayRecorder.h:114 | ReplayBodyPresentationSample.modelIndex | ReplayBodyId + recorded presentation row | recorded |
| 5 | Runtime/Replay/ReplayRecorder.h:153 | ReplaySolverBodySample.modelIndex | ReplayBodyId + recorded solver row | recorded |
| 6 | Runtime/Replay/ReplayRuntime.h:121 | RunReplayPathTraceNode.modelIndex | node id | hint |
| 7 | Runtime/Replay/ReplayRuntime.h:122 | RunReplayPathTraceNode.parentModelIndex | parentId | hint |
| 8 | Runtime/Replay/ReplayRuntime.h:133 | RunReplayPathTarget.modelIndex | target id | hint |
| 9 | Runtime/Replay/ReplayRuntime.h:175 | RunReplayCameraState.focusModelIndex | focusedId | hint |
| 10 | Runtime/Replay/ReplayRuntime.h:176 | RunReplayCameraState.focusCounterpartModelIndex | counterpartId | hint |
| 11 | Runtime/Replay/ReplayRuntime.h:191 | RunReplayCauseTreeRow.modelIndex | row id | hint |
| 12 | Runtime/Replay/ReplayRuntime.h:192 | RunReplayCauseTreeRow.counterpartModelIndex | counterpartId | hint |
| 13 | Runtime/Replay/ReplayRuntime.h:245 | RunReplayPathVisualizerState.targetModelIndex | targetId | hint |
| 14 | Runtime/Replay/ReplayRuntime.h:254 | RunReplayPredictionBodyBackup.modelIndex | ReplayBodyId | hint |
| 15 | Runtime/Replay/ReplayRuntime.h:270 | RunReplayPredictionBodySample.modelIndex | ReplayBodyId + prediction sample row | recorded |
| 16 | Runtime/Replay/ReplayRuntime.h:287 | ReplayPredictionGhostDrawRequest.modelIndex | render request only | frame-local |
| 17 | Runtime/Replay/ReplayRuntime.h:300 | ReplayPredictionRetainedMarker.modelIndex | ReplayBodyId | hint |
| 18 | Runtime/Replay/ReplayRuntime.h:321 | ReplayPredictionBaselineBodyPose.modelIndex | ReplayBodyId + retained baseline pose | recorded |
| 19 | Runtime/Replay/ReplayRuntime.h:335 | ReplayPredictionBaselineSnapshot.rootModelIndex | rootId + retained baseline root path | recorded |
| 20 | Runtime/Replay/ReplayRuntime.h:367 | RunReplayPredictionState.targetModelIndex | targetId | hint |
| 21 | Runtime/RunState.h:233 | AttachedCameraTarget.modelIndex | body, collider, replayBodyId, name | hint |
| 22 | Runtime/RuntimeInteractionCommands.h:53 | RuntimeInteractionCommand.modelIndex | body + collider | hint |
| 23 | Runtime/RuntimeInteractionCommands.h:69 | RuntimeInteractionEvent.previousModelIndex | previousBody + previousCollider | hint |
| 24 | Runtime/RuntimeInteractionCommands.h:70 | RuntimeInteractionEvent.modelIndex | body + collider | hint |
| 25 | Runtime/RuntimeInteractionController.h:125 | RuntimeInteractionGesture.modelIndex | no stable id in gesture state | sole-identity |
| 26 | Runtime/RuntimePickService.h:68 | RuntimePickResult.modelIndex | body + collider in same pick result | frame-local |
| 27 | Runtime/Tools/RuntimeTools.h:165 | RunMousePickupState.modelIndex | body | removed 2026-07-08 |
| 28 | Runtime/Tools/RuntimeTools.h:198 | RunEditorPlacementState.selectedModelIndex | selectedBody + selectedCollider | hint |
| 29 | Runtime/Tools/RuntimeTools.h:226 | RunEditorPlacementState.gizmoDragGroupIndices[] | active gesture only | frame-local |

## Phase 1 — inventory freeze + ratchet

- [x] I1. Complete the table above: open each "classify/check struct" row,
  read the owning struct + comments, and fill the Class column with one of:
  `recorded` (replay/scene data keyed by row at record time — keep, annotate),
  `frame-local` (never crosses frames — keep, annotate),
  `hint` (stable id adjacent — wrap in Phase 3),
  `sole-identity` (no stable id adjacent — BUG: add handle/id in Phase 3).
  Also sweep for members missed by the `= -1` pattern:
  `rg -n "int\s+\w*[mM]odelIndex" SkullbonezSource --type-add 'hdr:*.h' -thdr`
  and add any struct members found (e.g. gizmo drag-group arrays in
  RuntimeTools.h). Evidence: table complete, no row says "classify".

  Evidence (2026-07-08): CodeGraph was used first for the identity inventory.
  The current scalar census is 28 direct stored `int *ModelIndex*` members in
  `.h` type bodies; `Runtime/Replay/ReplayInteractionController.h:84` was added
  to the seed table, and `Runtime/Tools/RuntimeTools.h:226`
  `gizmoDragGroupIndices[]` was recorded as the non-scalar active-gesture row.
  The checker census intentionally excludes `Run.h:190` because it is a
  function default parameter, not stored identity.
- [x] I2. Ratchet: in `tools/check_runtime_boundaries.py` add a census rule
  counting struct-scope `int *[mM]odelIndex*` declarations in `.h` files with
  the current total stored as the budget (count from I1). Follow the existing
  rule pattern in that script (find one rule + its self-test and imitate —
  anchor: run `python tools/check_runtime_boundaries.py` first and read its
  output sections to find rule names). Include a self-test that a synthetic
  new member fails. Evidence: checker passes on HEAD; self-test demonstrates
  a failure on synthetic input. Gate: `tools\validate_fast.bat`, then run the
  checker. Commit.

  Evidence (2026-07-08): added
  `MAX_STORED_MODEL_INDEX_MEMBER_FIELDS = 28` plus a direct type-member scanner
  and self-test. The self-test accepts a budget-matched synthetic header and
  rejects a synthetic added `previousModelIndex` member with
  `stored modelIndex member census exceeds ratchet`. Preliminary targeted
  checks passed before the commit gate:
  `python tools\check_runtime_boundaries.py --self-test` printed
  `SELF_TEST_PASS: runtime boundary checker synthetic cases passed`;
  `python tools\check_runtime_boundaries.py --max-errors 20` printed
  `Runtime boundary summary: TestOutput\validation\runtime_boundaries\summary.json (0 errors)`
  and `PASS: Runtime boundary validation passed.` Touched-file comment audit:
  1 source-bearing file inspected (`tools/check_runtime_boundaries.py`), 0
  deferred.

  Commit-gate validation (2026-07-08):
  `tools\validate_fast.bat` passed in 00:00:34.4710073; log:
  `Agentic\Reports\2026-07-08\logs\fable-06-phase1-validate-fast.log`.
  Key result lines: formatting, project filters, staged-size, runtime
  boundaries, unit tests, and Profile/Debug builds passed with 0
  warnings/errors; `VALIDATE_FAST: ALL PASSED`. The required post-gate checker
  rerun passed in 00:00:17.2728378; log:
  `Agentic\Reports\2026-07-08\logs\fable-06-phase1-boundary-check.log`, with
  `Runtime boundary summary: TestOutput\validation\runtime_boundaries\summary.json (0 errors)`
  and `PASS: Runtime boundary validation passed.`

## Phase 2 — the wrapper type + central resolvers

- [x] R1. Add to `SkullbonezSource/Physics/PhysicsHandles.h`:
  ```cpp
  // Concept: a ModelRowHint is a cached dense-row guess, never identity.
  // Persistent state stores PhysicsBodyHandle/ReplayBodyId/scene ids; the
  // hint only accelerates the resolver's O(1) fast path and may be stale.
  struct ModelRowHint
  {
      int value = -1;
  };
  ```
- [x] R2. Add hint-aware resolvers next to the stores (PhysicsBodyStore.h,
  implementation in PhysicsBodyStore.cpp), reusing the existing
  `ResolveHandleForModelIndex`/`ModelIndexForHandle` internals:
  ```cpp
  // Returns the current dense row for a live body handle, refreshing the
  // caller's hint. Returns -1 (and leaves the hint invalid) for stale handles.
  int ResolveModelRow( PhysicsBodyHandle handle, ModelRowHint& hint ) const;
  ```
  and the replay-id form in RunReplayTools.cpp beside
  `TryResolveReplayBodyModelIndex` (which stays the internal engine).
  Evidence: Profile build 0/0. Gate: `validate_fast`. Commit.
- [x] R3. Unit tests if `fable_plans/01` phase 0 has landed (stale handle →
  -1; post-edit remap → hint self-heals). Otherwise `[B]` on plan 01 and
  continue.

  Evidence (2026-07-08): added `Physics::ModelRowHint` in
  `PhysicsHandles.h`, `PhysicsBodyStore::ResolveModelRow` in
  `PhysicsBodyStore.h/.cpp`, and a `ModelRowHint&` replay-id resolver overload
  beside `TryResolveReplayBodyModelIndex` in `RunReplayTools.cpp`. The retained
  replay marker, camera-focus, and prediction-begin paths now wrap their stored
  model-index caches as `ModelRowHint` at lookup time and propagate repaired or
  invalidated hint values back to retained UI state. `TestPhysicsHandles.cpp`
  added the focused stale-handle and moved-row self-heal coverage. Euclid
  performed a read-only implementation map before edits. Touched-file comment
  audit inspected 6 source-bearing files (`PhysicsHandles.h`,
  `PhysicsBodyStore.h`, `PhysicsBodyStore.cpp`, `RunReplayTools.cpp`,
  `RunReplayPredictionVisualizer.inl`, `TestPhysicsHandles.cpp`) with 0
  deferred.

  Commit-gate validation (2026-07-08):
  `tools\validate_fast.bat` passed in 00:00:52.8044; log:
  `Agentic\Reports\2026-07-08\logs\fable-06-r1-r3-validate-fast.log`.
  Key result lines: formatting, project filters, staged-size, runtime
  boundaries, Profile/Debug builds, and doctests passed; doctest reported
  `45 | 45 passed`, `582 | 582 passed`; Profile/Debug builds had 0
  warnings/errors; `VALIDATE_FAST: ALL PASSED`. The required post-gate checker
  rerun passed in 00:00:17.8207; log:
  `Agentic\Reports\2026-07-08\logs\fable-06-r1-r3-runtime-boundaries.log`, with
  `Runtime boundary summary: TestOutput\validation\runtime_boundaries\summary.json (0 errors)`
  and `PASS: Runtime boundary validation passed.`

## Phase 3 — subsystem conversion (one commit per group)

Order: smallest blast radius first. For each member: if class `hint`, change
`int fooModelIndex` → `Physics::ModelRowHint fooModelRow` and route reads
through R2 resolvers; if `redundant`, delete and use the adjacent handle; if
`sole-identity`, first ADD the proper id (handle at capture point), then
demote the int to a hint. `recorded`/`frame-local` members get an explicit
`// Lifetime:` comment naming their class instead of a conversion.

- [ ] C1. `RuntimeTools.h` cluster (#27 mouse pickup — redundant next to
  `body` handle; #28 selection — hint next to selectedBody/selectedCollider;
  any drag-group entries from I1). Gate: `validate_fast` + editor smoke
  (launch, click-select, drag gizmo via
  `tools\run_graphics_stress.bat 1` if selection code churned). Commit.

  Partial evidence (2026-07-08): the mouse-pickup redundant stored row was
  removed. `RunMousePickupState` now stores only `PhysicsBodyHandle` for live
  pickup command paths; pickup physics and angular-velocity restore revalidate
  the handle before writing, and editor overlay drawing resolves the current
  model row locally from `PhysicsBodyStore::ModelIndexForHandle`. The
  `RuntimeInteractionGesture::modelIndex`, selection hint, and drag-group rows
  remain for later C1/C3 work, so C1 stays unchecked. `MAX_STORED_MODEL_INDEX_MEMBER_FIELDS`
  dropped from 28 to 27 in `tools/check_runtime_boundaries.py`.

  Gate evidence: `python tools\check_runtime_boundaries.py --self-test`
  passed; `python tools\check_runtime_boundaries.py --max-errors 20` passed
  with 0 errors; `tools\validate_scene_parser_tests.bat` passed in
  00:00:06.4059333 after the companion fable-05 missing-camera parser fix; and
  `tools\validate_fast.bat` passed in 00:00:52.7932666 with formatting,
  project filters, staged-size, runtime boundaries, unit tests, and
  Profile/Debug builds all clean. Logs:
  `Agentic\Reports\2026-07-08\logs\fable-05-06-scene-parser-tests.log` and
  `Agentic\Reports\2026-07-08\logs\fable-05-06-mouse-pickup-validate-fast.log`.
  Touched-file comment audit inspected 7 source-bearing files with 0 deferred.
- [ ] C2. `RuntimeInteractionCommands.h` payloads (#6–#8): commands must carry
  `PhysicsBodyHandle` (already resolvable at enqueue time — find enqueue sites
  with `rg -n "RuntimeInteractionCommands|modelIndex" SkullbonezSource/Runtime/RunInput.cpp`
  and capture the handle there). Gate: `validate_fast` + interaction proofs
  (`memory_overlay_f6_toggle`, `replay_branch_restore_live_edge`). Commit.
- [ ] C3. `RunState.h:197` + `RuntimeInteractionController.h:123` +
  `RuntimePickService.h:68` per their I1 class. Gate: `validate_fast`. Commit.
- [ ] C4. Replay cluster (#9–#21): convert hint members to `ModelRowHint`;
  `targetId`/node `id` remain authority (already true by comment). Do NOT
  touch recorded sample rows (#18, #22, #23) beyond `// Lifetime:` annotations
  — they are replay data keyed by row-at-record-time and converting them
  changes replay semantics. Gate: `tools\validate_full.bat` (replay identity
  is determinism-adjacent) + `prediction_ragdoll_wall_200_predict` proof.
  Commit.
- [ ] C5. `GameModelCollection.h` rootModelIndex rows (#1–#2): coordinate with
  authoritative-plan-02 (scene grouping is actively migrating — check that
  plan's PHYS-002 status first; if in flight, `[B]` with pointer). Gate:
  `validate_physics`. Commit.

## Phase 4 — closure

- [ ] Z1. Delete now-dead ad hoc validation branches: re-grep
  `rg -n "modelIndex >= 0 &&|>= modelCount|< modelCount" SkullbonezSource/Runtime`
  and remove guards made redundant by resolver returns (each deletion cites
  the resolver that replaced it). Gate: `validate_fast` per file's area map.
- [ ] Z2. Update glossaries: remove "validated before use because collection
  edits can change it" from RuntimeTools.h; update ReplayRuntime.h "Fast
  lookup hint" comments to name `ModelRowHint`. Their deletion is the
  acceptance test from the source plan.
- [ ] Z3. Drop the I2 ratchet budget to the post-conversion count.
- [ ] Z4. Update `fable_plans/06-stable-identity-plan.md` status + this file.
