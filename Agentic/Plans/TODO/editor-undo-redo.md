# Editor Undo/Redo

Date: 2026-07-11
Status: Not started — 0%. **Unblocked 2026-07-11:** interaction-state-machine
completed the same day this plan was written (6/6 remaining phases, including
P9 commands), so the sequencing precondition is satisfied — build directly on
the completed command/gesture surface. Verify the U4 scene-lifecycle hook
against the completed runtime-shell scene ownership rather than the old C2
phase reference.
Impact area: editor tools, runtime interaction commands, UI
Origin: 2026-07-11 architecture gap review. The editor has no undo machinery
of any kind. The interaction-state-machine plan is building the prerequisite
(commands mutate, events observe); this plan adds the payoff: a bounded
command history for editor mutations.

## Scope decisions (binding)

- **Editor mode only.** Undo covers authoring mutations (place, delete,
  transform, parameter edits). It does not rewind simulation — replay
  scrubbing already owns time travel.
- **Sequenced after `TODO/interaction-state-machine.md` P9** (commands for
  remaining high-risk mutations). Building history on pre-command mutation
  paths would be built twice.
- **Fixed-capacity ring buffer** (e.g. 64 entries, owner-named constant),
  preallocated before steady gameplay per the allocation policy. Overflow
  drops the oldest entry; depth exhaustion is normal, not an error.
- **Inverse-command model, not world snapshots.** Each undoable command
  records the minimal prior state needed to invert itself (POD payloads,
  fixed size). If a mutation cannot cheaply capture its inverse, it is not
  undoable yet — record it in the non-undoable table here rather than
  snapshotting the world.

## Phases

- [ ] U1. Design slice: `EditorCommandHistory` owner (lives with the editor
      state owner, not `Run`), entry POD layout, and the undoable-command
      inventory table in this plan (place, delete, move/rotate/scale via
      gizmo, parameter edits from the editor UI). Each row names its inverse
      payload. Documentation-only.
- [ ] U2. History core + transform edits: gizmo drags coalesce into one
      entry per gesture (capture on gesture begin, commit on release —
      the typed-gesture work gives exact hooks). Ctrl+Z / Ctrl+Y (or
      Ctrl+Shift+Z) routed through the interaction controller's binding
      path. Gate: `validate_fast` + focused editor interaction proof.
- [ ] U3. Placement and deletion: delete captures the full re-create recipe
      (asset name/recipe, transform, physics params) — reusing the scene
      serialization records so re-created bodies match a scene round-trip.
      Body identity: history entries key on `PhysicsSceneObjectId` — the
      engine's single cross-system object identity per the 2026-07-11 owner
      ruling (see `TODO/entity-model-endgame.md`). Re-created objects get
      fresh live handles but keep their authored `sceneObjectId`, so undo
      chains survive delete/re-create without stale-handle resolution.
      Gate: `validate_full` (scene/creation-path scope).
- [ ] U4. Boundary rules: scene load/reset/exit-editor clears history (with
      event hook once runtime-shell C2 lifecycle events exist); replay/
      simulation mode transitions do not attempt cross-mode undo. UI
      affordance: undo depth indicator in the editor tab. Unit tests for
      push/undo/redo/overflow/clear in `SkullbonezTests`.
      Gate: `validate_tests` + `validate_fast`.
- [ ] U5. Closure: comment audit, `validate_full`, MASTER-PLAN/SessionState
      update, delete plan.

## Acceptance

- [ ] Place → move → delete → Ctrl+Z ×3 restores the original scene state
      (verified by scene-save diff, not eyeballing).
- [ ] Redo replays the same three; a new edit after undo truncates the redo
      branch.
- [ ] History is allocation-free after init; overflow and clear behave per
      the ring-buffer contract under unit test.
- [ ] Physics CSV byte-exact (editor-mode-only feature).

## Validation map

| Slice | Gate |
|-------|------|
| History core / bindings | `validate_fast` |
| Delete/re-create path | `validate_full` |
| Unit tests | `validate_tests` |
