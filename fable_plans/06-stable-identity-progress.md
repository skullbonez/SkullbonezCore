# Progress: Stable Identity (plan 06)

Source plan: `fable_plans/06-stable-identity-plan.md`
Status: not started
Last updated: 2026-07-07

## How to work this file

- Do items in order; one checkbox = one verifiable action; tick only with the
  named evidence pasted under the box. `[B]` + reason if blocked twice.
- Anchors are file + search string; locate with `rg -n "<anchor>" <file>`.
  Line numbers in the inventory table below are from 2026-07-06 and have
  drifted a few lines (2026-07-08 check: the six `ReplayRuntime.h` `= -1`
  members now sit at :119, :172, :173, :189, :242, :326;
  `TryResolveReplayBodyModelIndex` is at RunReplayTools.cpp:144). Anchor by
  MEMBER NAME, never by the table's line number.
- CENSUS RULE: the I1 sweep and the I2 ratchet count must use gitignore-blind
  tools (`git ls-files` + `grep -rn`), never bare `rg` — `.gitignore` `Debug/`
  hides tracked `SkullbonezSource/Physics/Debug/` from `rg`. (2026-07-08
  check: no `= -1` model-index members live there today, but the ratchet must
  count what grep counts.) Never raise the I2 budget to get green.
- The acceptance test that matters is STRUCTURAL: persistent members use
  `ModelRowHint`/handles and reads go through the R2 resolvers. Renaming a
  bare `int fooModelIndex` to `int fooRow` to evade the I2 regex, or deleting
  a glossary comment without the conversion behind it, is a review failure.
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
- Full persisted `= -1` model-index member inventory (26 members, 9 headers):

| # | File:Line | Member | Adjacent stable id? | Class |
|---|-----------|--------|---------------------|-------|
| 1 | GameObjects/GameModelCollection.h:134 | rootModelIndex | scene group data | recorded scene metadata — verify in I1 |
| 2 | GameObjects/GameModelCollection.h:166 | rootModelIndex | scene group data | same |
| 3 | Runtime/RunState.h:197 | modelIndex | comment: "UI/presentation hint; revalidated before use" | hint — wrap |
| 4 | Runtime/RuntimeInteractionController.h:123 | modelIndex | check struct | classify in I1 |
| 5 | Runtime/RuntimePickService.h:68 | modelIndex | pick result (frame-local?) | classify in I1 |
| 6 | Runtime/RuntimeInteractionCommands.h:53 | modelIndex | command payload | replace with handle |
| 7 | Runtime/RuntimeInteractionCommands.h:69 | previousModelIndex | command payload | replace with handle |
| 8 | Runtime/RuntimeInteractionCommands.h:70 | modelIndex | command payload | replace with handle |
| 9 | Runtime/Replay/ReplayRuntime.h:116 | RunReplayPathTraceNode.modelIndex | node has `id` (ReplayBodyId); comment: "Fast lookup hint; ReplayBodyId remains authority" | hint — wrap |
| 10 | ReplayRuntime.h:117 | parentModelIndex | node has parentId | hint — wrap |
| 11 | ReplayRuntime.h:128 | modelIndex | check struct | classify |
| 12 | ReplayRuntime.h:170 | focusModelIndex | camera focus state | classify |
| 13 | ReplayRuntime.h:171 | focusCounterpartModelIndex | camera focus state | classify |
| 14 | ReplayRuntime.h:186 | modelIndex | cause-tree row (has id fields) | hint — wrap |
| 15 | ReplayRuntime.h:187 | counterpartModelIndex | cause-tree row | hint — wrap |
| 16 | ReplayRuntime.h:240 | RunReplayPathTarget.targetModelIndex | has targetId | hint — wrap |
| 17 | ReplayRuntime.h:249 | modelIndex | check struct | classify |
| 18 | ReplayRuntime.h:265 | RunReplayPredictionBodySample.modelIndex | sample has id | recorded data — keep, annotate |
| 19 | ReplayRuntime.h:282 | ReplayPredictionGhostDrawRequest.modelIndex | render request (frame-local) | frame-local — keep, annotate |
| 20 | ReplayRuntime.h:291 | modelIndex | check struct | classify |
| 21 | ReplayRuntime.h:317 | RunReplayPredictionState.targetModelIndex | has targetId | hint — wrap |
| 22 | Runtime/Replay/ReplayRecorder.h:114 | modelIndex | recorded sample row | recorded data — keep, annotate |
| 23 | ReplayRecorder.h:153 | modelIndex | recorded sample row | recorded data — keep, annotate |
| 24 | Runtime/Tools/RuntimeTools.h:163 | RunMousePickupState.modelIndex | struct also has `Physics::PhysicsBodyHandle body` | redundant-next-to-handle → wrap or delete |
| 25 | RuntimeTools.h:196 | RunEditorPlacementState.selectedModelIndex | struct has selectedBody + selectedCollider; comment says "row hint" | hint — wrap |
| 26 | (drag-group arrays etc. — see I1) | | | |

## Phase 1 — inventory freeze + ratchet

- [ ] I1. Complete the table above: open each "classify/check struct" row,
  read the owning struct + comments, and fill the Class column with one of:
  `recorded` (replay/scene data keyed by row at record time — keep, annotate),
  `frame-local` (never crosses frames — keep, annotate),
  `hint` (stable id adjacent — wrap in Phase 3),
  `sole-identity` (no stable id adjacent — BUG: add handle/id in Phase 3).
  Also sweep for members missed by the `= -1` pattern:
  `rg -n "int\s+\w*[mM]odelIndex" SkullbonezSource --type-add 'hdr:*.h' -thdr`
  and add any struct members found (e.g. gizmo drag-group arrays in
  RuntimeTools.h). Evidence: table complete, no row says "classify".
- [ ] I2. Ratchet: in `tools/check_runtime_boundaries.py` add a census rule
  counting struct-scope `int *[mM]odelIndex*` declarations in `.h` files with
  the current total stored as the budget (count from I1). Follow the existing
  rule pattern in that script (find one rule + its self-test and imitate —
  anchor: run `python tools/check_runtime_boundaries.py` first and read its
  output sections to find rule names). Include a self-test that a synthetic
  new member fails. Evidence: checker passes on HEAD; self-test demonstrates
  a failure on synthetic input. Gate: `tools\validate_fast.bat`, then run the
  checker. Commit.

## Phase 2 — the wrapper type + central resolvers

- [ ] R1. Add to `SkullbonezSource/Physics/PhysicsHandles.h`:
  ```cpp
  // Concept: a ModelRowHint is a cached dense-row guess, never identity.
  // Persistent state stores PhysicsBodyHandle/ReplayBodyId/scene ids; the
  // hint only accelerates the resolver's O(1) fast path and may be stale.
  struct ModelRowHint
  {
      int value = -1;
  };
  ```
- [ ] R2. Add hint-aware resolvers next to the stores (PhysicsBodyStore.h,
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
- [ ] R3. Unit tests if `fable_plans/01` phase 0 has landed (stale handle →
  -1; post-edit remap → hint self-heals). Otherwise `[B]` on plan 01 and
  continue.

## Phase 3 — subsystem conversion (one commit per group)

Order: smallest blast radius first. For each member: if class `hint`, change
`int fooModelIndex` → `Physics::ModelRowHint fooModelRow` and route reads
through R2 resolvers; if `redundant`, delete and use the adjacent handle; if
`sole-identity`, first ADD the proper id (handle at capture point), then
demote the int to a hint. `recorded`/`frame-local` members get an explicit
`// Lifetime:` comment naming their class instead of a conversion.

- [ ] C1. `RuntimeTools.h` cluster (#24 mouse pickup — redundant next to
  `body` handle; #25 selection — hint next to selectedBody/selectedCollider;
  any drag-group entries from I1). Gate: `validate_fast` + editor smoke
  (launch, click-select, drag gizmo via
  `tools\run_graphics_stress.bat 1` if selection code churned). Commit.
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
