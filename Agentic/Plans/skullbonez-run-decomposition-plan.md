# Skullbonez Run Decomposition Plan

Date: 2026-06-21
Status: Draft implementation plan
Impact area: runtime architecture, input, replay tooling, editor tools, render pass contracts, scene integration
Validation for this document-only change: none required

## Goal

Shrink the `SkullbonezRun` runtime surface until it is a coordinator again
instead of the place where every gameplay, UI, replay, editor, scene, capture,
and render concern lands.

This plan focuses on two immediate pressure points:

- `SkullbonezSource/SkullbonezRun.h` is a 1,343-line public/private kitchen
  sink containing public launch API, private pass classes, render pass
  contracts, replay state, editor state, scene gate state, runtime settings,
  and helper tool declarations.
- `SkullbonezSource/SkullbonezRunInput.cpp` is a 6,876-line implementation file
  containing input routing, UI command application, camera movement, launcher
  firing, editor placement, editor gizmos, replay scrubber input, replay cause
  tree interaction, replay velocity editing, replay prediction, and replay path
  visualization.

The target is not a rewrite. The first objective is a mechanical decomposition
that preserves behavior while making future ownership extraction reviewable.

## Current Read

The runtime is already split across multiple files, but the boundaries are only
partly real:

| File | Current size | Main responsibility |
|------|--------------|---------------------|
| `SkullbonezRunInput.cpp` | 6,876 lines | Input routing, UI command application, replay tools, editor tools, launcher, camera |
| `SkullbonezRunScene.cpp` | 2,647 lines | Scene load/reset, generated objects, scene browser, defaults persistence, world setup |
| `SkullbonezRun.cpp` | 1,363 lines | Construction, resource release, public setters, replay helpers, init, diagnostics |
| `SkullbonezRun.h` | 1,343 lines | Public API, private state, pass declarations, replay/editor/render structs |
| `SkullbonezRunPasses.cpp` | 1,248 lines | Render pass resource hooks and pass bodies |
| `SkullbonezRunUiTextPass.cpp` | 940 lines | UI/text pass plus replay scrubber/cause-tree overlays |
| `SkullbonezRunRender.cpp` | 922 lines | Frame render orchestration and render-frame context |
| `SkullbonezRunFrame.cpp` | 777 lines | Main loop, physics tick, screenshots, perf, scene advance |

Useful existing extraction work:

- `SceneRuntime` owns scene queue/index/current scene state, but
  `SkullbonezRun` still owns most load/reset side effects.
- `SimulationSystem` owns timestep accumulators, but `SkullbonezRun` still
  stitches replay capture, diagnostics, and scene automation around the tick.
- `RuntimeInputContext` exists, but `TakeInput()` still applies nearly every
  command directly.
- Render pass bodies live in `SkullbonezRunPasses.cpp`, but pass classes,
  pass contracts, and pass resource aggregates still live inside
  `SkullbonezRun.h`.
- Replay recorder/exporter types exist, but replay UI, scrub/predict/edit
  tools, and visualizers are still methods and state on `SkullbonezRun`.

## Non-Goals

- Do not change input bindings, UI behavior, replay semantics, scene reset
  behavior, camera behavior, or render output during the mechanical split.
- Do not introduce a new ECS, job system, or broad runtime framework.
- Do not remove `SkullbonezRun` as the caller-facing facade in the first pass.
- Do not combine this with physics solver, render graph, material system, or
  scene parser behavior changes.
- Do not move public launch API out from under existing startup code unless the
  replacement is thin and compatibility-preserving.

## Target Shape

Short-term target:

- `SkullbonezRun.h` exposes the public runtime facade and includes small
  internal headers only where necessary.
- Private state and pass/replay/editor declarations live in focused headers.
- The current `.cpp` files are split by feature area so reviews can reason
  about one subsystem at a time.
- `SkullbonezRun` still owns the state, but the state is grouped and the code
  that mutates it is no longer piled into one implementation file.

Longer-term target:

- `SkullbonezRun` delegates to small owned controllers:
  `RunInputRouter`, `RunReplayTools`, `RunEditorTools`, `RunSceneController`,
  and `RunRenderPipeline` or equivalent names.
- Controllers receive explicit context objects instead of reaching through all
  of `SkullbonezRun`.
- Replay/editor/render helpers can evolve without adding more private methods
  to the central runtime facade.

## Proposed File Boundaries

### Public And Shared Type Headers

`SkullbonezRun.h`

- Keep the public `SkullbonezRun` class, constructor, destructor, `Initialise()`,
  `Run()`, `RunSceneLoadOnly()`, public launch setters, and
  `DumpTextureAssets()`.
- Keep only forward declarations and private members that cannot be hidden yet.
- Include the smallest set of focused runtime headers needed for by-value
  members.

`SkullbonezRunTypes.h`

- Move public/simple runtime enums:
  `OverlayMode`, `GeneratedObjectTypeOverride`, and any other launch-visible
  simple type.
- This can be included by startup code without dragging in render, replay, UI,
  terrain, and physics visualizer headers.

`SkullbonezRunState.h`

- Move plain state aggregates:
  `RunRuntimeSettings`, `RunTimerState`, `RunSubsystemState`,
  `RunCameraState`, `RunScreenshotState`, `RunLiveStyleControlState`,
  `RunDebugState`, `RunRayCastTestLine`, `RunLauncherFireMode`,
  `RunRayCastTestState`, `RunRequiredContactState`,
  `RunRequiredBroadphaseXCellsState`, and `RunUIStressState`.
- Keep this header behavior-free.
- Prefer forward declarations plus pointers where practical; only include full
  definitions for by-value members.

`SkullbonezRunRenderPassContracts.h`

- Move render pass resource structs, pass input/output structs,
  `SkyPassMode`, `ObjectPassMode`, `RenderFrameContext`, and pass class
  declarations.
- Good first version can keep nested or friend-style access if necessary.
- Better second version: top-level `RunInternal` pass classes that receive
  explicit `SkullbonezRun&` only as a temporary bridge.

`SkullbonezRunReplayState.h`

- Move replay-only state:
  `RunReplayTrack`, `RunReplayScrubberState`, path trace/target structs,
  cause tree rows/state, prediction backups/samples/frames/state,
  velocity edit state, pose backups, and debug scrub probe state.
- Keep recorder/exporter public types in the existing replay headers.

`SkullbonezRunEditorTools.h`

- Move `RunEditorPlacementState` and `RunEditorTracer` declaration.
- Later, split the tracer implementation into `SkullbonezRunEditorTracer.cpp`
  or a non-Run-specific debug draw helper if it stops needing runtime access.

### Internal Helper Headers

`SkullbonezRunInternal.h`

- Keep it as an aggregator for `.cpp` files during the first pass.
- Remove broad helper bodies from it as focused headers appear.
- It should eventually contain only high-frequency includes, using aliases,
  and truly shared tiny utilities.

`SkullbonezRunReplayUi.h`

- Move replay scrubber/cause-tree geometry helpers from
  `SkullbonezRunInternal.h`.
- Own constants such as scrubber panel sizes, prediction horizon limits, and
  cause-tree panel sizes.
- Used by replay input and replay overlay files, not by unrelated scene/render
  code.

`SkullbonezRunSceneReset.h`

- Move `SceneRuntimeResetSnapshot` and reset snapshot helper declarations if
  they are needed outside `SkullbonezRunScene.cpp`.
- Keep cinematic override helpers near scene/style load code rather than in the
  global internal header.

### Implementation Files

`SkullbonezRunInput.cpp`

- Shrink to input polling, mode state construction, and top-level
  `TakeInput()` orchestration.
- It should read like:
  poll hardware, update `RuntimeInputContext`, route UI commands, route
  keyboard actions, route editor/replay/launcher tools, update mouse/camera.

`SkullbonezRunInputCommands.cpp`

- Move focused command application helpers that mutate runtime/debug/UI state:
  water toggles, render/debug toggles, scene navigation/reset/save, physics
  debug toggles, UI visibility, fixed-step/time-scale/model-count command
  application, and live runtime controls.
- This is the natural home for most of the middle of `TakeInput()` once it is
  split into named helpers.

`SkullbonezRunCameraInput.cpp`

- Move `MoveCamera()`, mouse-look reset logic, camera selection/cycling logic,
  tracking camera controls, and `TryBuildMouseWorldRay()` if editor/launcher
  code can consume it through a shared helper.

`SkullbonezRunLauncher.cpp`

- Move raycast test line state operations, launcher terrain/body hit tests,
  `FireRayCastTest()`, `FireLauncherLaser()`, and
  `FireLauncherProjectile()`.
- Keep replay launcher visual samples in replay/frame code unless they become
  part of a launcher controller.

`SkullbonezRunEditor.cpp`

- Move editor mode toggles, placement preview, object placement, editor object
  picking, and editor overlay orchestration.

`SkullbonezRunEditorGizmo.cpp`

- Move transform gizmo hit testing and mutation:
  translation axis hit, rotation ring hit, axis ray parameter, rotation angle,
  move, rotate, and scale selected object.
- Keep this separate because gizmo math is dense and easy to review in
  isolation.

`SkullbonezRunEditorTracer.cpp`

- Move `RunEditorTracer` method implementations if they are currently embedded
  in `SkullbonezRunInput.cpp`.
- Long-term candidate name: `SkullbonezDebugLineTracer`.

`SkullbonezRunReplayInput.cpp`

- Move replay scrubber input, active track state, pause/restore behavior,
  scrub position math, replay path picking, and cause-tree input.

`SkullbonezRunReplayPrediction.cpp`

- Move prediction job lifecycle and data capture:
  mark dirty, clear cache, cancel job, begin job, step job, capture/apply
  prediction body state, and capture prediction frames.
- This is the highest-risk replay slice because it temporarily mutates live
  solver/model state. Keep it together.

`SkullbonezRunReplayVelocityEdit.cpp`

- Move velocity edit enable/input/hit-test/drag/apply/render overlay methods.
- This is a clean seam because it already owns a distinct state struct and UI
  control.

`SkullbonezRunReplayVisualizers.cpp`

- Move replay path visualizer and prediction visualizer rendering methods.
- Keep overlay drawing in `SkullbonezRunUiTextPass.cpp` until the replay UI
  controller exists, then revisit.

`SkullbonezRunReplayUi.cpp`

- Optional later split for `RenderReplayScrubberOverlay()` and
  `RenderReplayCauseTreeOverlay()` out of `SkullbonezRunUiTextPass.cpp`.
- Do this after replay input/prediction movement so overlay state ownership is
  clear.

## Phase Plan

### Phase 0: Inventory And Guardrails

Tasks:

1. Capture current file sizes and a method inventory for every `SkullbonezRun*`
   file.
2. Check `git status --short --branch`; protect any user-owned dirty files.
3. Confirm whether the branch is PR-bound. If yes, prepare to run the full
   runtime validation gate before commit/push.
4. Add a temporary checklist in the implementation branch handoff listing
   every moved method and its destination.

Acceptance:

- No source changes yet.
- The implementation agent knows exactly which files are mechanically split
  first.

Validation:

- No repository validation required for inventory-only work.

### Phase 1: Header Hygiene Without Behavior Changes

Goal: reduce `SkullbonezRun.h` weight before moving behavior.

Tasks:

1. Add `SkullbonezRunTypes.h` and move public/simple enums.
2. Add `SkullbonezRunState.h` and move plain runtime state structs.
3. Add `SkullbonezRunReplayState.h` and move replay-only state structs.
4. Add `SkullbonezRunEditorTools.h` and move editor placement/tracer
   declarations.
5. Add `SkullbonezRunRenderPassContracts.h` and move pass contracts/resources.
6. Update includes in `SkullbonezRun.h` and the implementation files.
7. Add the new headers to `SKULLBONEZ_CORE.vcxproj` and
   `SKULLBONEZ_CORE.vcxproj.filters`.

Acceptance:

- `SkullbonezRun.h` contains the facade and member list, not hundreds of lines
  of unrelated state and pass declarations.
- No methods change bodies.
- No runtime behavior changes.

Validation:

- During iteration, a targeted `tools\validate_build.bat Profile` is acceptable
  if the split needs compile feedback.
- For PR-bound work, use `tools\validate_full.bat` because `SkullbonezRun*`
  files and project filters are touched.

### Phase 2: Split `SkullbonezRunInternal.h`

Goal: stop the internal header from becoming the next junk drawer.

Tasks:

1. Move replay scrubber/cause-tree UI constants and geometry helpers to
   `SkullbonezRunReplayUi.h`.
2. Move reset snapshot type and scene reset helpers to
   `SkullbonezRunSceneReset.h` if they remain shared.
3. Move cinematic override helpers near scene/style loading, ideally into a
   scene-focused internal header.
4. Keep `SkullbonezRunInternal.h` as a short implementation aggregator while
   the rest of the code still expects it.

Acceptance:

- Replay UI helpers are not visible to unrelated render/scene implementation
  files unless needed.
- Scene reset state is not mixed with replay scrubber geometry.

Validation:

- Same as Phase 1 if PR-bound: `tools\validate_full.bat`.

### Phase 3: Carve `TakeInput()` Into Named Helpers

Goal: make the top-level input path readable before moving chunks into new
files.

Tasks:

1. Inside `SkullbonezRunInput.cpp`, split `TakeInput()` into private methods:
   `PollRuntimeInput()`, `ApplyUiCommands()`, `ApplyKeyboardActions()`,
   `UpdateInputModes()`, `UpdateCameraInput()`, `UpdateLauncherInput()`,
   `UpdateEditorInput()`, and `UpdateReplayInput()` or equivalent local names.
2. Keep methods in the same file for the first slice to avoid include churn.
3. Preserve command order exactly. In particular, do not reorder UI blocking,
   replay scrub pause, editor mode, launcher mode, and camera movement logic.
4. Add short comments only where ordering is non-obvious.

Acceptance:

- `TakeInput()` becomes an orchestration function.
- Diff review can verify command order mechanically.
- No behavior changes.

Validation:

- Targeted Profile build during iteration if needed.
- PR gate: `tools\validate_full.bat`.

### Phase 4: Mechanical `SkullbonezRunInput.cpp` File Split

Goal: move already-grouped methods into focused `.cpp` files with minimal code
edits.

Tasks:

1. Move UI/runtime command application helpers into
   `SkullbonezRunInputCommands.cpp`.
2. Move camera helpers into `SkullbonezRunCameraInput.cpp`.
3. Move launcher/raycast helpers into `SkullbonezRunLauncher.cpp`.
4. Move editor placement methods into `SkullbonezRunEditor.cpp`.
5. Move editor gizmo methods into `SkullbonezRunEditorGizmo.cpp`.
6. Move `RunEditorTracer` implementation into
   `SkullbonezRunEditorTracer.cpp`.
7. Update project and filter files.

Acceptance:

- `SkullbonezRunInput.cpp` should drop below roughly 1,200 lines after this
  phase.
- Each new file has a clear file header and one responsibility.
- Method bodies remain recognizably moved, not rewritten.

Validation:

- Targeted Profile build after each large move if compile feedback is needed.
- PR gate: `tools\validate_full.bat`.

### Phase 5: Replay Tool Split

Goal: remove replay-specific behavior from generic runtime input.

Tasks:

1. Move scrubber input and pause/restore behavior to
   `SkullbonezRunReplayInput.cpp`.
2. Move cause-tree input/focus/build helpers to the same replay input file or
   `SkullbonezRunReplayCauseTree.cpp` if it stays large.
3. Move prediction job lifecycle to `SkullbonezRunReplayPrediction.cpp`.
4. Move velocity edit behavior to `SkullbonezRunReplayVelocityEdit.cpp`.
5. Move path/prediction visualizers to `SkullbonezRunReplayVisualizers.cpp`.
6. Decide whether replay overlay rendering should remain in
   `SkullbonezRunUiTextPass.cpp` for now or move to `SkullbonezRunReplayUi.cpp`.

Acceptance:

- Generic input files do not contain replay prediction internals.
- Replay files are organized around user-facing replay tools:
  scrubber, cause tree, prediction, velocity edit, visualizers.
- Prediction code remains contiguous because it mutates live state while
  building lookahead samples.

Validation:

- PR gate: `tools\validate_full.bat`.
- If replay prediction or solver restore behavior changes beyond mechanical
  movement, add `tools\validate_physics.bat` before claiming behavior safety.

### Phase 6: Render Pass Header And Implementation Cleanup

Goal: finish the header cleanup around pass contracts without disturbing the
already completed render pipeline extraction.

Tasks:

1. Move pass classes from nested private declarations toward top-level
   `RunInternal` classes when compile pressure allows.
2. Replace broad private access with a small render context:
   textures, backend, world environment, game model collection, pass resources,
   debug state, and active cinematic config.
3. Keep `DrawPrimitives()` as the ordered pass scheduler.
4. Avoid resource lifecycle changes unless a separate slice explicitly owns
   them.

Acceptance:

- Pass declarations no longer dominate `SkullbonezRun.h`.
- Render pass files can be opened without dragging replay/editor/input state
  into the reader's head.

Validation:

- If only declarations move: `tools\validate_full.bat` at PR gate because
  `SkullbonezRun*` is touched.
- If render pass resource ownership or render output can change:
  `tools\validate_dx12_renderer.bat`, and escalate to `tools\validate_full.bat`
  if runtime lifecycle is touched.

### Phase 7: Introduce Controllers After Mechanical Splits

Goal: move from file decomposition to real ownership boundaries.

Candidate controllers:

| Controller | Owns | Initial migration path |
|------------|------|------------------------|
| `RunInputRouter` | Polling, mode transitions, command ordering | Wrap current `TakeInput()` helper calls |
| `RunReplayTools` | Scrubber, cause tree, prediction, velocity edit | Start as facade over existing replay state |
| `RunEditorTools` | Placement, selection, gizmo, tracer | Start with explicit access to model/world/camera context |
| `RunLauncherTools` | Raycast fire mode, laser/projectile firing, test lines | Start with camera/world/model context |
| `RunRenderPipeline` | Pass objects and render frame context | Start after pass contract headers are stable |
| `RunSceneController` | Scene load/reset side effects around `SceneRuntime` | Start after input and replay are split |

Rules:

- Do not introduce all controllers in one branch.
- Each controller starts as a thin facade over existing state.
- Only move state ownership into a controller once the method boundary is
  already stable.
- Prefer explicit context structs over giving every controller unlimited access
  to `SkullbonezRun`.

Acceptance:

- `SkullbonezRun` can delegate feature work without exposing dozens of private
  helper methods.
- New features have an obvious subsystem home.

Validation:

- Controller introduction that touches broad runtime flow uses
  `tools\validate_full.bat`.
- Replay/physics-affecting controller behavior may also need
  `tools\validate_physics.bat`.
- Render controller behavior may also need `tools\validate_dx12_renderer.bat`.

## Suggested Implementation Slices

### Slice A: Header Split Only

Files likely touched:

- `SkullbonezRun.h`
- `SkullbonezRunTypes.h`
- `SkullbonezRunState.h`
- `SkullbonezRunReplayState.h`
- `SkullbonezRunEditorTools.h`
- `SkullbonezRunRenderPassContracts.h`
- `SKULLBONEZ_CORE.vcxproj`
- `SKULLBONEZ_CORE.vcxproj.filters`

Why first:

- It reduces compile surface and review noise before behavior files move.
- It gives later phases stable include targets.

### Slice B: Internal Helper Split

Files likely touched:

- `SkullbonezRunInternal.h`
- `SkullbonezRunReplayUi.h`
- `SkullbonezRunSceneReset.h`
- `SkullbonezRunScene.cpp`
- `SkullbonezRunUiTextPass.cpp`
- replay input files once created

Why second:

- Replay UI helpers and scene reset snapshot state should not travel together.

### Slice C: `TakeInput()` Helper Extraction

Files likely touched:

- `SkullbonezRun.h`
- `SkullbonezRunInput.cpp`

Why third:

- It makes command ordering explicit before moving functions across files.

### Slice D: Non-Replay Input Split

Files likely touched:

- `SkullbonezRunInput.cpp`
- `SkullbonezRunInputCommands.cpp`
- `SkullbonezRunCameraInput.cpp`
- `SkullbonezRunLauncher.cpp`
- `SkullbonezRunEditor.cpp`
- `SkullbonezRunEditorGizmo.cpp`
- `SkullbonezRunEditorTracer.cpp`
- project and filter files

Why fourth:

- Editor/launcher/camera code is large but less entangled with solver restore
  semantics than replay prediction.

### Slice E: Replay Split

Files likely touched:

- `SkullbonezRun.cpp`
- `SkullbonezRunFrame.cpp`
- `SkullbonezRunInput.cpp`
- `SkullbonezRunUiTextPass.cpp`
- `SkullbonezRunReplayInput.cpp`
- `SkullbonezRunReplayPrediction.cpp`
- `SkullbonezRunReplayVelocityEdit.cpp`
- `SkullbonezRunReplayVisualizers.cpp`
- optional `SkullbonezRunReplayUi.cpp`

Why fifth:

- Replay is high value and high risk. It should be moved after the simpler
  file boundaries prove stable.

### Slice F: First Real Controller

Recommended first controller: `RunLauncherTools` or `RunEditorTools`.

Reason:

- Launcher/editor tools have clearer input/output context than replay
  prediction.
- They provide a pattern for explicit context structs without immediately
  touching scene load/reset or render pass resource ownership.

## Risks And Mitigations

| Risk | Mitigation |
|------|------------|
| Input behavior changes because command order shifts | First split `TakeInput()` into helpers in-place; move only after order is visible. |
| Replay prediction corrupts live state after movement | Keep prediction job code contiguous; do not mix with UI overlay movement in the same slice. |
| Include churn breaks build or slows iteration | Create focused headers first, then move `.cpp` bodies. Use a targeted Profile build when compile feedback is needed. |
| New files are missing from Visual Studio project filters | Update `.vcxproj` and `.vcxproj.filters` in the same slice that adds files. |
| `SkullbonezRunInternal.h` becomes another monolith | Split replay UI and scene reset helpers before adding more implementation helpers. |
| Pass classes keep private access to everything | Accept as temporary during mechanical movement; introduce explicit render context only in a later slice. |
| Mechanical movement hides behavior edits | Keep move-only commits small and avoid formatting churn. |
| Validation scope becomes unclear | Default to `tools\validate_full.bat` for PR-bound `SkullbonezRun*` refactors. Add renderer/physics gates only when those semantics can change. |

## Validation Matrix

Repository validation scripts are PR/commit gates, not routine iteration
commands. During implementation, targeted builds are allowed when they answer a
specific compile question.

| Change | PR-bound validation |
|--------|---------------------|
| Documentation-only plan updates | No validation required |
| Header-only `SkullbonezRun*` decomposition | `tools\validate_full.bat` |
| Mechanical input/editor/launcher file split | `tools\validate_full.bat` |
| Replay scrubber or replay UI movement | `tools\validate_full.bat` |
| Replay prediction or solver restore behavior changes | `tools\validate_full.bat` plus `tools\validate_physics.bat` |
| Render pass declaration movement only | `tools\validate_full.bat` |
| Render pass resource ownership or output changes | `tools\validate_dx12_renderer.bat`, plus `tools\validate_full.bat` if lifecycle changes |
| Project/filter changes only | `tools\validate_fast.bat` if isolated; otherwise use the owning slice's gate |

## Success Criteria

Near-term success:

- `SkullbonezRun.h` is under roughly 500 lines and mostly describes the facade,
  member aggregates, and private orchestration entry points.
- `SkullbonezRunInput.cpp` is under roughly 1,200 lines and contains only
  top-level input routing.
- Replay prediction, replay velocity editing, editor gizmos, launcher firing,
  and camera input each live in their own implementation file.
- New files are present in project and filter metadata.
- No behavior changes are intentionally introduced by the mechanical slices.

Long-term success:

- `SkullbonezRun` has a small, readable private method surface.
- New replay/editor/launcher features have obvious homes outside the central
  runtime facade.
- Render pass contracts no longer force every runtime reader through a massive
  public header.
- Scene load/reset, input routing, replay tools, editor tools, and render
  pipeline can be modified and validated independently.

## Implementation Notes

- Implementation work from this plan should use
  `Agentic/Skills/orchestrator/SKILL.md` unless the user explicitly asks to
  bypass it.
- Check `git status --short --branch` before each implementation slice and
  before any commit.
- Do not overwrite unrelated dirty files.
- Do not run broad repository validation while iterating; save formal
  validation for PR-bound commit/PR prep.
- Because `SkullbonezRun*` maps to broad runtime behavior, assume
  `tools\validate_full.bat` is the default final gate unless a slice is truly
  documentation-only.
