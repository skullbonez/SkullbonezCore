# Run Composition Root Shrink Plan

Date: 2026-06-26
Status: Active architecture cleanup plan
Impact area: runtime architecture, editor tools, replay tools, scene runtime, render host boundaries
Validation for this document-only change: none required

## Goal

Make `Run` shrink in source, not just in intent.

A refactor only counts when it deletes `Run::` declarations from
`SkullbonezSource/Runtime/Run.h`. Moving code between `Run*.cpp` files, adding
subsystem state, or adding callbacks from a subsystem back into `Run` is not
enough.

## Rules

- Each implementation slice must remove a coherent cluster of `Run::` methods.
- New subsystem state without moved behavior does not count as shrinkage.
- New callbacks from subsystem code back into `Run` are migration debt.
- Behavior-preserving slices should stay small enough to validate and revert.
- Each PR should report deleted `Run.h` declarations and deleted `Run::`
  definitions.

## First Slices

1. Move launcher behavior into `RuntimeTools`.
   - Move ray-test lines, hit tests, laser fire, projectile fire, and launcher
     visual sample helpers out of `Run`.
   - Target files: `Runtime/Editor/LauncherTools.cpp`,
     `Runtime/Tools/RuntimeTools.*`.

2. Move editor behavior into an editor tool owner.
   - Move placement preview, gizmo drag, object placement, editor UI commands,
     save hotkeys, and editor overlay generation out of `Run`.
   - Target files: `Runtime/Editor/RunEditorTools.cpp`,
     `Runtime/Editor/EditorTools.*`, `Runtime/Tools/RuntimeTools.*`.

3. Move replay UI/tool behavior into `ReplayRuntime`.
   - Move scrubber input, cause tree rows, velocity edit, prediction jobs,
     focus mask building, and replay overlay construction out of `Run`.
   - Target files: `Runtime/Replay/RunReplayTools.cpp`,
     `Runtime/Replay/ReplayRuntime.*`.

4. Make scene loading owned by scene runtime code.
   - Stop using `SceneRuntimeCoordinator` as a callback shell around
     `Run::LoadScene`.
   - Move reset snapshot, UI override clearing, perf-log close, generated setup,
     authored setup, world/terrain setup, and scene advancement side effects.
   - Target files: `Runtime/Scene/RunScene.cpp`,
     `Runtime/Scene/SceneRuntimeCoordinator.*`.

5. Split `RuntimeRenderHost`.
   - Replace the wide render host with narrow render-facing views:
     world/models, replay overlay, tool overlay, UI, diagnostics.
   - This unblocks deleting render callbacks such as editor overlay and replay
     prediction ghost rendering from `Run`.

## Ratchet

Extend `tools/check_runtime_boundaries.py` so `Run.h` cannot grow in private
method count without an explicit allowlist update.

For each shrink slice, record:

- deleted `Run.h` declarations,
- deleted `Run::` definitions,
- new owner class/methods,
- validation command selected for the PR gate.

## Validation

Documentation-only updates need no validation. Implementation slices should use:

- launcher/editor/input/tool behavior: `tools\validate_full.bat`;
- physics impulses or body mutation: `tools\validate_physics.bat`;
- render host or overlay rendering: `tools\validate_dx12_renderer.bat`;
- broad uncertain slices: `tools\agent_validate.bat`.
