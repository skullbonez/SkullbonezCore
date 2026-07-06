# Stable Identity Plan (modelIndex vs handle duality)

Date: 2026-07-06
Status: Proposed
Impact area: runtime tools, replay, UI, physics stores; behavior-preserving
Validation for this document: none (documentation-only)

## Problem

The codebase has **four identity systems for the same object** and no rule for
which one may be stored:

- `modelIndex` — dense model-order row. Invalidated by any collection edit.
- `Physics::PhysicsBodyHandle` / `PhysicsColliderHandle` — generational store
  handles; stale-safe.
- `ReplayBodyId` — stable id for replay identity across retained samples.
- `sceneObjectId` — authoring-time identity (model append requires one).

The failure pattern is that the *unstable* one gets stored. Headers persist
`modelIndex` fields across frames (`ReplayRuntime.h` alone has six
`...ModelIndex = -1;` members; `RuntimeTools.h`, `GameModelCollection.h`, and
`RuntimeInteractionCommands.h` carry more), and the code openly documents the
resulting discipline burden:

- `RuntimeTools.h` glossary: *"Model index: dense model-order row used for UI
  and replay identity, **validated before use because collection edits can
  change it**."*
- `RuntimeTools.h` on selection state: *"The model index is only the
  editor/UI row hint paired with those handles."*
- Replay code resolves ids through helpers like
  `TryResolveReplayBodyModelIndex(...)` and "WithHint" lookups because a
  stored index is never trustworthy.

Every consumer re-validates ad hoc; each new feature (today's prediction
markers included) re-learns the rule or ships a stale-index bug. This is a
class of bug the type system could delete.

## Goal / Definition of Done

1. **One storage rule:** persistent state (anything living across frames)
   stores stable identity only — `PhysicsBodyHandle` for live-body references,
   `ReplayBodyId` for replay/prediction identity, `sceneObjectId` for authored
   identity. `modelIndex` is a frame-local cursor: it may be a local variable,
   a function parameter, or an explicitly-annotated cached hint — never bare
   stored identity.
2. **Resolve-on-use is centralized:** one helper family
   (`ResolveModelIndex( bodyStore, handle )`,
   `ResolveModelIndex( bodyStore, replayId, hint )`) replaces the scattered
   per-subsystem validators; hint-cached variants keep the O(1) fast path.
3. **The type system enforces it:** stored hints use a dedicated wrapper type
   (`ModelRowHint { int value; }`) so a bare `int fooModelIndex` member no
   longer typechecks against store APIs. Grep-level ratchet backs this up.
4. The "validated before use" caveats disappear from the glossaries because
   they are no longer true — there is nothing unvalidated to store.

## Non-goals

- Merging `ReplayBodyId` and `PhysicsBodyHandle` into one universal id. They
  have different lifetimes by design (replay ids outlive live bodies). The fix
  is *which one gets stored where*, not a single god-id.
- Changing dense iteration in hot loops. Solver/broadphase code iterating rows
  by index is correct and untouched; this plan governs *stored identity* only.

## Phased slices

### Phase 1 — inventory and ratchet

- `git ls-files`-based census (per the comment-pass convention) of every
  persisted `*ModelIndex` member in headers; classify each as (a) redundant
  next to an existing handle/id (delete), (b) genuine hint (wrap as
  `ModelRowHint`), (c) the only identity present (bug: add the proper handle/id
  alongside, then demote the index to hint).
- Extend `tools/check_runtime_boundaries.py`: budget for
  `int\s+\w*[Mm]odelIndex\w*\s*=` in struct/class scope in headers; count may
  not grow. Self-test included.

### Phase 2 — central resolvers

- Add the resolver helpers next to the stores (they mostly exist as scattered
  statics: `TryResolveReplayBodyModelIndex`,
  `FindReplayPredictionBodyByIdWithHint`, collider `HandleForModelIndex`
  round-trips). One home, one naming scheme, unit tests under plan 01 phase 2
  (stale handle → resolve fails; post-edit remap → hint miss falls back and
  self-heals).

### Phase 3 — subsystem conversion (one per slice)

Order by blast radius, smallest first:

1. `RuntimeTools.h` selection/mouse-pickup state (handles already stored
   alongside — mostly deletions of category (a) members).
2. `RuntimeInteractionCommands.h` command payloads → handles/ids.
3. Editor placement/gizmo state (`RunEditorPlacementState.selectedModelIndex`
   → already paired with `selectedBody`/`selectedCollider`; demote to hint).
4. Replay/prediction state (`ReplayRuntime.h`'s six members): `targetId`
   (ReplayBodyId) is already authoritative; `targetModelIndex` and friends
   become `ModelRowHint`s resolved through phase-2 helpers.
5. `GameModelCollection.h` members last (coordinates with
   `authoritative-plan-02-physics-store-authority`, which is actively moving
   authority out of that class — do not convert what that plan is deleting).

### Phase 4 — delete the caveats

- Remove the ad hoc validation branches the resolvers replaced; update the
  glossaries in `RuntimeTools.h` / `ReplayRuntime.h`; drop the "validated
  before use" invariant lines. Their deletion is the acceptance test.

## Risks

- Hidden semantic differences: some stored indices are intentionally *frozen*
  (e.g. replay visual samples that must reference the row as it was when
  recorded). Classification in phase 1 must mark these explicitly as recorded
  data (category (b) with a comment), not live identity — converting them
  would change replay behavior.
- UI code paths use `modelIndex == selected` equality pervasively; conversions
  must keep an equally cheap comparison (compare handles, or compare resolved
  indices within a single frame scope).

## Validation map

| Slice | Validation |
|-------|-----------|
| Inventory/ratchet | `validate_fast`, then run the changed checker |
| Resolver helpers | `validate_fast` + plan-01 unit tests when available |
| Editor/tools conversions | `validate_fast` (+ manual editor smoke via `run_graphics_stress.bat 1` if selection churn is touched) |
| Replay/prediction conversions | `validate_full` (replay identity is determinism-adjacent) + `prediction_ragdoll_wall_200_predict` proof |
