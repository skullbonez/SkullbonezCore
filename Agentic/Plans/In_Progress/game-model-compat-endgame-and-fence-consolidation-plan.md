# GameModel Compatibility Endgame and Fence Consolidation Plan

Date: 2026-07-05
Status: In progress (not started)
Impact areas: physics, game model data ownership, scene system, render projection,
boundary checker tooling, tests
Validation for latest source slice: none yet; no source slice has been
implemented under this plan.

## Origin

This plan answers three external-reviewer-style criticisms the user accepted as
actionable. All other critique from that review was explicitly rejected as not
applicable to this application.

1. **The internal compatibility layer must die (Linus).** Internal interfaces
   get no backward-compatibility mercy: fix all callers and delete the old
   thing. `GameModelCollection` still acts as the "compatibility adapter for
   legacy scenes" for physics body state, and `GameModel` still carries mutable
   physics authoring payload that the stores re-import every topology change.
2. **The boundary checker's opening comment is a confession (Carmack).** The
   `Purpose:` block of `tools/check_runtime_boundaries.py` enumerates roughly
   thirty bespoke fences one clause at a time. As of 2026-07-05 the checker is
   17,698 lines with ~170 repo-level check entry points. Architecture is being
   enforced by grep instead of by construction; every fence marks a place the
   old path could leak back in because the old path still exists.
3. **The half-finished cut (Carmack).** `MakeBodyRecordFromAuthoredModel`
   carries its own "delete this when creation writes PhysicsBodyCreateDesc
   records directly" note, `PhysicsBodyStore::LoadFromModels` still copies
   legacy model state into store rows, and Phase 6 of
   `Agentic/Plans/To_Eval/physics-game-model-authority-plan.md` has open rows
   for writeback deletion, compat collider fields, and render reliance on the
   concrete collection. Finish the cut instead of fencing it.

The unifying remedy: delete the legacy path so the guarded regression becomes
structurally impossible (a compile error, not a lint hit), then retire or
collapse the fence in the same slice. Fences remaining at the end of this plan
must be permanent architecture rules, not migration guards.

## Relationship To Existing Work

- `Agentic/Plans/To_Eval/physics-game-model-authority-plan.md` is the parent
  authority plan. Its Phase 6 open checkboxes are absorbed here; tick them
  there when the matching slice lands here so the two plans never disagree.
- `Agentic/Plans/In_Progress/physics-standalone-agent-workqueue.csv` closed at
  `PHY-1076`. This plan is the "next audit pass" named in
  `Agentic/SessionState.md`: remaining non-presentation model-index fallbacks,
  name/asset identity cleanup outside grouping, and the final `GameModel`
  body/collider reader audit.
- Implementation defaults to `Agentic/Skills/orchestrator/SKILL.md` per
  `AGENTS.md` Plan Implementation Mode.
- Every deletion slice must satisfy the Migration Artifact Gate: name owner,
  reason, deletion condition, and checker budget in the slice entry and the
  commit body.

## Goal

1. No physics source file includes or names `GameModel`/`GameModelCollection`.
   Physics creation, edit, refresh, and diagnostics operate on descriptors,
   handles, and store rows only.
2. `GameModel` carries no mutable physics authoring or compatibility payload.
   Whatever survives of `GameModelCollection` is a legitimately-scoped
   presentation/authoring owner described with domain nouns, or it is deleted.
3. `tools/check_runtime_boundaries.py` shrinks. Migration fences are either
   deleted (path structurally impossible and name meaningless) or collapsed
   into a compact data-driven deleted-name tombstone table. The `Purpose:`
   header describes fence categories in a few sentences instead of enumerating
   every fence.
4. Physics determinism is preserved throughout: byte-exact
   `physics_regression_solver.csv` at every physics-gated checkpoint. This plan
   changes ownership and wiring, not simulation math; any baseline change is a
   defect or requires explicit user sign-off.

## Non-Goals

- No new scene/entity system design beyond the minimum metadata ownership
  needed to delete the compat paths. Broader entity identity work stays in the
  parent authority plan.
- No renderer feature work. Render projection changes are ownership moves only.
- No behavior tuning, solver changes, or baseline refreshes.
- No removal of permanent architecture fences (inheritance budget, Run.h
  composition-root rules, render-graph barrier rules, global-service rules).
- The other reviewer criticisms (language choice, comment standard, platform,
  agent-process weight) are explicitly out of scope.

## Phase 0 - Startup and Inventory

- [ ] Follow the Agent Startup Contract; run `git status --short --branch` and
  protect user-owned dirty files.
- [ ] Reader audit: enumerate every remaining production read of `GameModel`
  body/collider/physics state and every model-index fallback outside
  presentation. Record the list in this plan as the slice inventory. Anchors
  known at plan-writing time:
  - `PhysicsBodyStore::LoadFromModels` (`SkullbonezSource/Physics/PhysicsBodyStore.h:123`),
    called from `GameModelCollection.cpp` topology repair and `Runtime/Init.cpp`.
  - `PhysicsEngine::RefreshBodyFromModel` (`SkullbonezSource/Physics/PhysicsEngine.h:64`),
    called from `GameModelCollection.cpp` and `PhysicsScene.cpp`.
  - `MakeBodyRecordFromAuthoredModel` (`SkullbonezSource/Physics/PhysicsBodyStore.h:110`).
  - Post-solve model mirror application near
    `SkullbonezSource/Physics/PersistentContactSolver.cpp:1364` (owner-side,
    pending final reader migration per parent plan Phase 6).
  - Compat collider fields on `GameModel` (parent plan Phase 6 open row).
  - Model-order presentation projection
    (`GameModelCollection::RefreshRenderInstances`, private) and any production
    render reliance on the concrete collection (parent plan Phase 6 open row).
  - `RigidBody` compatibility body state (`SkullbonezSource/Physics/RigidBody.h`).
  - `PhysicsModelAccess` narrow refresh facade
    (`SkullbonezSource/Physics/PhysicsModelAccess.h`).
- [ ] Fence audit: classify every check entry point in
  `tools/check_runtime_boundaries.py` into exactly one bucket and record counts
  in this plan:
  - **(a) retire** - guarded shape is already structurally impossible; the
    fence can become a tombstone row or be deleted outright.
  - **(b) load-bearing** - the guarded legacy path still exists; the fence
    retires only in the Track A slice that deletes the path.
  - **(c) permanent** - a standing architecture rule that outlives migration.
- [ ] Choose slice order from the two audits: smallest set of readers whose
  migration unlocks the largest fence retirement first.

## Track A - Finish the Cut (Linus / Carmack half-cut)

Order within the track may be re-sequenced by the Phase 0 audit; the deletion
conditions may not be weakened. Every slice ships with: owner, reason, deletion
condition, checker budget, updated comments in touched files, and the fence
retirement for whatever the slice made structurally impossible (Track B rule).

- [ ] A1. Creation-side descriptor ownership. Scene, editor, and asset creation
  write `PhysicsBodyCreateDesc`-style value records directly to the stores;
  append no longer converts a just-appended model. Deletion condition:
  `MakeBodyRecordFromAuthoredModel` has no declaration, definition, or caller.
- [ ] A2. Delete the compat refresh path. Topology repair and cold init build
  store rows from descriptors/authoritative store state, not model re-import.
  Deletion condition: `PhysicsBodyStore::LoadFromModels` and
  `PhysicsEngine::RefreshBodyFromModel` have no declaration, definition, or
  caller, and `PhysicsBodyStore.h/cpp` name no `GameModel` type.
- [ ] A3. Delete the remaining store-to-model writeback. Migrate the final
  readers of mirrored `GameModel` body state (diagnostics, presentation,
  editor) to store rows, then delete the post-solve model mirror application
  and any bulk-step writeback. Deletion condition: no production code writes
  body pose/velocity/sleep from store records into `GameModel`.
- [ ] A4. Delete compat collider fields from `GameModel` after the final
  reader migrates to `ColliderStore` rows (parent plan Phase 6 row).
- [ ] A5. Move model-order presentation facts (material, highlight, display
  name where still load-bearing) to an explicitly owned presentation record so
  render projection fills from stores plus that record. Deletion condition: no
  production render code takes a concrete `GameModelCollection&` (parent plan
  Phase 6 row). Follow the Hot-Path gate: value records and plain fills, no new
  adapter/sink/bridge types.
- [ ] A6. Non-presentation model-index fallback cleanup and name/asset identity
  cleanup outside grouping, per the SessionState next-audit list. Identity
  flows through handles/scene-object ids; display names are presentation only.
- [ ] A7. Endgame decision slice: with physics authority gone, either re-scope
  `GameModelCollection` under its real domain role (scene presentation/authoring
  container, named and commented as such, no "compatibility" vocabulary left in
  its learning headers) or dissolve it into its owners. Delete
  `PhysicsModelAccess` and `RigidBody` compatibility state when their last
  consumer goes. Deletion condition: `rg "compatibility" SkullbonezSource/Physics`
  returns no hits describing a live model/store seam.
- [ ] A8. Rewrite learning headers and boundary comments in every file whose
  mental model changed (`PhysicsBodyStore`, `PhysicsScene`, `PhysicsEngine`,
  `PhysicsWorld`, `GameModelCollection`, `Init`, touched runtime files), then
  run `Agentic/Skills/comment-style-audit/skill.md` over every touched
  source-bearing file.

## Track B - Shrink the Checker (Carmack confession)

The guardrail contract in `AGENTS.md` (deleted artifacts stay textually
detectable, with self-tests) is preserved; what changes is the cost per fence.

- [ ] B1. Build the tombstone table: a single data-driven registry of deleted
  names (token or small regex, scope, error message) evaluated by one shared
  engine, with one parameterized self-test harness generating the
  old/allowed/comment-only cases per row. New deleted-artifact guards are table
  rows, not new check functions.
- [ ] B2. Migrate bucket (a) fences from the Phase 0 audit into tombstone rows
  or delete them where the guarded name is gone and meaningless; verify
  `--self-test` coverage is equivalent before and after each batch.
- [ ] B3. Retire bucket (b) fences inside the Track A slice that deletes their
  legacy path - same commit, so the checker never guards a shape that can no
  longer compile.
- [ ] B4. Rewrite the module `Purpose:`/`Mental model:` header to describe the
  fence categories (composition-root rules, inheritance budget, store-authority
  rules, tombstone table) in at most ~15 lines, pointing at the table instead
  of enumerating fences.
- [ ] B5. Ratchet: record checker line count and check-entry-point count at
  plan start (17,698 lines / ~170 entry points on 2026-07-05) and at each
  checkpoint. The plan is not complete while either number has grown net.
- [ ] B6. End-state audit: every surviving check function is a bucket (c)
  permanent rule with a one-line justification in the header; migration fences
  are tombstone rows only.

## Validation Plan

Repository validation runs as pre-commit/PR gates per slice, not per edit.
Per the standing SessionState note, completed physics source slices take an
intermittent `tools\validate_physics.bat` checkpoint.

| Slice type | Gate |
|------------|------|
| Physics store/refresh/writeback slices (A1-A4, A6) | `tools\validate_physics.bat`, byte-exact CSV |
| Render projection slice (A5) | `tools\validate_dx12_renderer.bat` + `tools\validate_perf.bat` (collection render stream + hot-loop rule) |
| Init/runtime wiring touched (`Init.cpp`, `Run*`) | `tools\validate_full.bat` |
| Checker changes (B1-B6) | `tools\validate_fast.bat`, then `python tools\check_runtime_boundaries.py --repo .` and its `--self-test` |
| Final acceptance | `tools\validate_full.bat` |

A single independent rubber-duck review runs once at the end of the whole
plan, not per slice.

## Final Acceptance Checklist

- [ ] `rg "GameModel" SkullbonezSource/Physics` - no production-code hits
  (comment-only historical references acceptable only where the comment
  documents the current owner, not a live seam).
- [ ] `rg "LoadFromModels|RefreshBodyFromModel|MakeBodyRecordFromAuthoredModel|WriteBackToModel|ForCompatibility" SkullbonezSource` - no hits.
- [ ] `rg "compatibility" SkullbonezSource/Physics` - no live-seam hits.
- [ ] Checker line count and entry-point count strictly below the 2026-07-05
  baseline; `Purpose:` header at or under ~15 lines; tombstone table carries
  the deleted-name guards with passing self-tests.
- [ ] Parent plan Phase 6 rows ticked to match; workqueue/checklist rows added
  with owner/reason/deletion/checker evidence per slice.
- [ ] `Agentic/SessionState.md` updated; this plan moved to `Agentic/Plans/Done/`.
- [ ] Final `tools\validate_full.bat` pass quoted, including byte-exact
  `physics_regression_solver.csv` and zero DX12 InfoQueue errors.
