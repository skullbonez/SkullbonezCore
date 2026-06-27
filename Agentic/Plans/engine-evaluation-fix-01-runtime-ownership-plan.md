# Engine Evaluation Fix 01: Runtime Ownership Concentration

Date: 2026-06-27
Status: Completed implementation plan
Source finding: the engine is still too centralized; `Run`, `RuntimeRenderHost`,
`GameModelCollection`, and several large runtime/UI files carry too much
behavioral responsibility.
Impact area: runtime architecture, scene system, renderer orchestration, replay,
editor tools, diagnostics, UI
Validation for this document-only change: none required

## Goal

Turn the runtime from a broad behavior owner into a thin composition shell.

The target is not merely smaller files. The target is that each subsystem owns
its state, invariants, and commands:

```text
Run
  Owns process lifetime, subsystem construction, top-level frame order, and
  shutdown.

SceneRuntime
  Owns scene load/reset/advance policy and lifecycle events.

RuntimeRenderer / RenderPipeline
  Own render-facing frame construction and pass services.

ReplayRuntime
  Owns replay state, scrub/prediction decisions, and replay overlays.

RuntimeTools
  Owns editor/manipulator/launcher/mouse-pickup interaction state.

DiagnosticsRuntime
  Owns perf logs, screenshots, SkullScope hooks, and stress diagnostics.

UI
  Owns tab/widget behavior in smaller modules instead of a monolithic UI file.
```

This plan follows the existing `Agentic/Plans/run-shell-extraction-plan.md` and
turns the first engine-evaluation issue into a concrete checklist.

## Why This Matters

Large central classes make the engine fragile in three ways:

1. A small feature often has to touch `Run` even when the feature logically
   belongs to replay, tools, rendering, diagnostics, or scene runtime.
2. Cross-subsystem state is easy to mutate in the wrong order because broad
   bags such as `RuntimeRenderHostBindings` expose too much.
3. Validation failures are harder to localize because orchestration and
   subsystem behavior are interleaved.

## Current Anchors To Inspect

- `SkullbonezSource/Runtime/Run.h`
- `SkullbonezSource/Runtime/RunFrame.cpp`
- `SkullbonezSource/Runtime/RunInput.cpp`
- `SkullbonezSource/Runtime/Scene/RunScene.cpp`
- `SkullbonezSource/Runtime/Editor/RunEditorTools.cpp`
- `SkullbonezSource/Runtime/Replay/RunReplayTools.cpp`
- `SkullbonezSource/Runtime/Render/RuntimeRenderHost.h`
- `SkullbonezSource/Runtime/Render/RuntimeRenderer.h`
- `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h`
- `SkullbonezSource/UI/UI.cpp`
- `tools/check_runtime_boundaries.py`

## Non-Goals

- Do not rewrite physics storage here. Use
  `Agentic/Plans/engine-evaluation-fix-02-physics-data-boundary-plan.md`.
- Do not move pass command recording into the render graph here. Use
  `Agentic/Plans/engine-evaluation-fix-03-render-graph-execution-plan.md`.
- Do not perform a broad style-only file split without moving ownership.
- Do not introduce stored `Run*`, `Run&`, or `RuntimeRenderHost` clones in new
  systems.
- Do not change validation baselines unless a behavior change is intentional and
  explicitly documented.

## Design Rules

- Move one ownership boundary at a time.
- Prefer explicit input/output structs over callbacks that expose mutable
  subsystem internals.
- Keep `Run` as the process-level caller while moving decisions and state into
  subsystem owners.
- Make compatibility accessors temporary, named, and covered by guardrails.
- Every source-bearing implementation slice must follow
  `Agentic/Reference/comment-style-guide.md`.
- Use the repo-local orchestrator skill before implementing this plan unless
  the user explicitly asks to bypass it.

## Phase 0: Refresh The Ownership Map

Purpose: prevent agents from cutting by file size instead of ownership.

Checklist:

- [x] Run the Agent Startup Contract from `AGENTS.md`.
- [x] Run `git status --short --branch` and record pre-existing dirty files as
      user-owned.
- [x] Read this plan, `Agentic/Plans/run-shell-extraction-plan.md`, and
      `Agentic/Plans/architecture_pass_2026-06-02.md`.
- [x] Generate a fresh largest-file list for `SkullbonezSource/Runtime`,
      `SkullbonezSource/UI`, and `SkullbonezSource/Scene`.
- [x] Inventory every `Run` data member and classify it as:
      process lifetime, scene, simulation, renderer, replay, tools,
      diagnostics, UI, compatibility, or should-be-removed.
- [x] Inventory every `RuntimeRenderHostBindings` field and classify it as:
      render world view, replay overlay view, tool overlay view, UI view,
      diagnostics view, or obsolete bridge.
- [x] Search for trivial `Run::*` wrappers that only forward to subsystem
      methods.
- [x] Search for subsystem files that store or include `Run` directly.
- [x] Write or update a dated report under `Agentic/Reports/` with the
      ownership map and line-count baseline.

Validation checklist:

- [x] Documentation-only inventory needs no repository validation.
- [x] If boundary tooling changes during this phase, run
      `tools\validate_fast.bat`.
- [x] If `tools/check_runtime_boundaries.py` changes, also run
      `tools\validate_select.bat runtime-boundaries`.

## Phase 1: Harden Runtime Boundary Guardrails

Purpose: make regressions fail before large movement begins.

Checklist:

- [x] Extend `tools/check_runtime_boundaries.py` to reject new stored `Run*` or
      `Run&` fields in runtime subsystem headers.
- [x] Add a guardrail for growth of broad render-host bindings unless the
      change is explicitly allowlisted with a dated reason.
- [x] Add a guardrail for new `Run` forwarding wrappers that only delegate to a
      subsystem.
- [x] Add synthetic positive and negative tests for each new guardrail.
- [x] Make the guardrail output name the owning plan and offending file.
- [x] Update `Agentic/README.md` or this plan only if the validation entry point
      changes.

Validation checklist:

- [x] `tools\validate_fast.bat`
- [x] `tools\validate_select.bat runtime-boundaries`
- [x] Confirm failures identify file, line or symbol, and the violated rule.

## Phase 2: Narrow Render Host Services

Purpose: remove the broad render-time view of runtime internals.

Checklist:

- [x] Split `RuntimeRenderHostBindings` into smaller render-facing views:
      `RenderWorldView`, `RenderSceneView`, `RenderReplayOverlayView`,
      `RenderToolOverlayView`, `RenderUiView`, and `RenderDiagnosticsView`.
- [x] Move replay overlay state queries behind `ReplayRuntime` methods.
- [x] Move editor, manipulator, launcher, and tracer overlay queries behind
      `RuntimeTools` methods.
- [x] Move screenshot/perf/diagnostic render inputs behind `DiagnosticsRuntime`
      or a diagnostics view.
- [x] Make pass code consume immutable or narrowly mutable views, not broad
      state bags.
- [x] Delete each migrated field from the broad binding struct in the same
      slice that migrates its call sites.
- [x] Update boundary tests so new broad binding fields fail validation.

Validation checklist:

- [x] `tools\validate_dx12_renderer.bat`
- [x] `tools\validate_full.bat` if scene, replay, tool, UI, or lifecycle order
      can change.
- [x] Confirm `dx12_validation.txt` has zero errors.
- [x] Confirm screenshot baselines match or document an intentional visual
      baseline update.

## Phase 3: Move Remaining Scene Lifecycle Decisions Out Of `Run`

Purpose: make scene load/reset/advance policy live in scene runtime code.

Checklist:

- [x] Identify the remaining scene lifecycle decisions still made directly in
      `Run` or `RunScene.cpp`.
- [x] Define explicit lifecycle events:
      `BeforeSceneUnload`, `AfterSceneCleared`, `BeforeScenePopulate`,
      `AfterScenePopulate`, and `AfterSceneActivated`.
- [x] Move scene queue/index validation into scene runtime ownership.
- [x] Move preserve/clear reset-state selection into scene runtime ownership.
- [x] Move adjacent-scene, browser, cinematic deck, generated-scene, and
      scene UI override decisions behind scene-owned APIs.
- [x] Keep process-level sequencing in `Run`, but move policy and side effects
      to scene runtime helpers.
- [x] Delete compatibility wrappers after call sites use scene APIs directly.
- [x] Add boundary checks for removed wrapper names where practical.

Validation checklist:

- [x] `tools\validate_full.bat`
- [x] If only boundary tooling and no runtime behavior changed, use
      `tools\validate_fast.bat` plus `tools\validate_select.bat runtime-boundaries`.
- [x] Confirm Profile and Debug builds are warning-free.
- [x] Confirm DX12 renderer validation reports zero InfoQueue errors.
- [x] Confirm `physics_regression_solver.csv` is byte-exact.

## Phase 4: Move Tool, Replay, And Diagnostics Decisions To Owners

Purpose: end the pattern where state has moved but decisions still live in
`Run`.

Checklist:

- [x] Move replay scrub, prediction, focus mask, ghost overlay, and replay
      launcher-visual decisions behind `ReplayRuntime`.
- [x] Move editor placement, manipulator, mouse-pickup, ray-test, launcher, and
      tool overlay frame decisions behind `RuntimeTools`.
- [x] Move screenshot automation, perf log lifecycle, stress diagnostics, and
      SkullScope entry points behind `DiagnosticsRuntime`.
- [x] Replace direct mutation from input/UI paths with command structs or narrow
      owner methods.
- [x] Remove compatibility accessors after each route is migrated.
- [x] Verify owner APIs express intent, not raw mutable field access.

Validation checklist:

- [x] `tools\validate_full.bat`
- [x] Add `tools\validate_interaction_clicks.bat` if editor/replay/launcher
      interaction semantics can change.
- [x] Add `tools\validate_ui_stress.bat` or `tools\validate_demo_stress.bat`
      only when the touched path needs stress evidence.

## Phase 5: Decompose Large UI And Runtime Tool Files By Ownership

Purpose: reduce file concentration only after ownership is clear.

Checklist:

- [x] Split `UI.cpp` by durable widget/tab ownership, not arbitrary line count.
- [x] Split `RunEditorTools.cpp` into editor placement, gizmo, manipulator,
      launcher, and overlay modules where ownership already exists.
- [x] Split `RunReplayTools.cpp` into replay import/export/query/scrub modules
      only after `ReplayRuntime` owns the behavior.
- [x] Keep includes local and avoid creating a new umbrella header with every
      runtime dependency.
- [x] Add or update project-filter validation rules for any new source files.
- [x] Apply the repository comment standard to every touched source-bearing
      file.
- [x] Run the comment-style audit skill on every touched source-bearing file
      before reporting done.

Validation checklist:

- [x] `tools\validate_fast.bat` for mechanical splits with no behavior change.
- [x] `tools\validate_full.bat` if runtime behavior, scene load, replay, or UI
      behavior can change.
- [x] `tools\validate_ui.bat` or focused UI validation if UI behavior changes.
- [x] Direct `tools\validate_project_filters.bat` if new `.cpp` or `.h` files
      are added.

## Final Acceptance Checklist

- [x] `Run.h` is visibly smaller and mostly process-lifetime composition state.
- [x] `RuntimeRenderHostBindings` is removed or reduced to narrow immutable
      render-facing views.
- [x] New replay, tool, scene, diagnostics, or UI behavior no longer requires a
      new `Run::*` wrapper.
- [x] Boundary validation rejects broad runtime regressions.
- [x] Large files were split by ownership, not just by line count.
- [x] Every touched source-bearing file was inspected with the comment-style
      audit skill.
- [x] Final PR-bound work ran the narrowest required gate, normally
      `tools\validate_full.bat` for this plan.
- [x] Final handoff reports commands, elapsed times, validation output summary,
      and any intentionally deferred files or wrappers.

## Agent Do-Not-Miss Checklist

- [x] Do not overwrite or format unrelated dirty files.
- [x] Do not store `Run` inside a subsystem to make extraction easier.
- [x] Do not move ownership and behavior-changing policy in the same large diff.
- [x] Do not weaken runtime-boundary checks to make a slice pass.
- [x] Do not claim validation success without command output.
- [x] Do not skip comment-style audit for touched `.cpp`, `.h`, `.hpp`, `.inl`,
      `.hlsl`, or substantial tool scripts.
- [x] Do not merge this with physics storage or graph callback work.

