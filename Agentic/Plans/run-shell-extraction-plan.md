# Run Shell Extraction Plan

Date: 2026-06-25
Status: Draft follow-up plan
Impact area: runtime architecture, renderer orchestration, scene system, replay, editor tools, diagnostics
Validation for this document-only change: none required

## Goal

Make `Run` a thin application shell instead of the place every engine system
eventually reaches.

The runtime has already been split into named subsystems, but the current shape
still leaves `Run` as the broad owner of scene state, diagnostics, replay, UI,
tools, world state, model storage, render host bindings, and renderer lifetime.
The professional-engine target is not "smaller files." The target is that a new
scene, replay, render, input, tool, or diagnostics feature can be implemented
inside the subsystem that owns the behavior without threading another helper or
callback through `Run`.

Target outcome:

```text
Run
  Owns process lifetime, startup/shutdown, top-level frame order, and final
  process-owned subsystem construction.
  Does not own subsystem-specific temporary state, behavior policy, or render
  pass service bridges.

RuntimeRenderer
  Consumes narrow render-facing views, not a broad `RuntimeRenderHost` over
  every Run-owned system.

SceneRuntime
  Owns scene load/reset/apply decisions and exposes scene lifecycle events to
  other systems through explicit contracts.

ReplayRuntime, RuntimeTools, DiagnosticsRuntime
  Own their interaction state and present narrow frame/update/render APIs to the
  runtime shell.
```

## Current Evidence

- `SkullbonezSource/Runtime/Run.h` owns a large composition-root state block:
  scene controller/coordinator, launch options, diagnostics, replay, timers,
  input, interaction, camera, simulation, UI, debug state, runtime tools,
  visualizers, world environment, `GameModelCollection`, command queue, render
  host, and renderer.
- `SkullbonezSource/Runtime/Render/RuntimeRenderHost.h` intentionally exposes a
  broad borrowed-state bridge for render passes, including systems, debug,
  timers, runtime settings, models, world, visualizers, replay states, editor
  state, launcher state, UI, input, camera, scene browser, and callbacks.
- Large implementation files still show behavior concentration even after
  decomposition:
  - `SkullbonezSource/Runtime/Editor/RunEditorTools.cpp`
  - `SkullbonezSource/Runtime/Replay/RunReplayTools.cpp`
  - `SkullbonezSource/Runtime/Scene/RunScene.cpp`
  - `SkullbonezSource/Runtime/RunFrame.cpp`
- `Agentic/Plans/runtime-run-decomposition-plan.md` completed important
  ownership movement. This plan is the follow-up that shrinks the remaining
  compatibility surface rather than restarting that work.

## Design Rules

1. Preserve behavior. Extraction commits should be boring and easy to revert.
2. Do not store `Run&`, `Run*`, or broad `RuntimeRenderHost`-style bags inside
   newly extracted systems.
3. Split by ownership of state and invariants, not by file length.
4. Prefer concrete subsystem APIs and plain input/output structs over broad
   virtual interfaces.
5. Keep compatibility wrappers temporary, named, and covered by boundary checks.
6. Each phase must leave less reason for another file to call back into `Run`.

## Non-Goals

- Do not rewrite physics storage here; use
  `Agentic/Plans/game-model-data-boundary-plan.md`.
- Do not replace global service access here; use
  `Agentic/Plans/global-service-context-plan.md`.
- Do not redesign UI layout or visual style.
- Do not finish the render graph as part of this plan unless a narrow render
  host removal slice requires a small graph-facing adapter.

## Phase 0: Ownership Map And Guardrails

Purpose: turn the remaining broad ownership into a measurable checklist.

Tasks:

1. Produce a short ownership map for each `Run` member:
   - process lifetime owner,
   - scene owner,
   - replay owner,
   - tools owner,
   - diagnostics owner,
   - render owner,
   - true composition-root state that should remain in `Run`.
2. Extend `tools/check_runtime_boundaries.py` so it can flag:
   - new stored `Run*` or `Run&` references,
   - new `RuntimeRenderHostBindings` fields,
   - new broad callbacks that return mutable subsystem internals,
   - new `Run::*` wrappers that only forward to a subsystem.
3. Add a synthetic failing case to the runtime-boundary validator for every new
   rule.

Validation:

- `tools\validate_fast.bat`
- `tools\validate_select.bat runtime-boundaries`

## Phase 1: Narrow Render Host Services

Purpose: make renderer pass code depend on render-facing contracts instead of a
wide view over runtime state.

Tasks:

1. Split `RuntimeRenderHostBindings` into smaller view structs:
   - `RenderWorldView`
   - `RenderSceneView`
   - `RenderReplayOverlayView`
   - `RenderToolOverlayView`
   - `RenderUiView`
2. Move callback logic that belongs to replay overlays into `ReplayRuntime`.
3. Move callback logic that belongs to tool/editor overlays into `RuntimeTools`.
4. Move texture lookup/select paths behind a renderer or asset-service view,
   leaving no pass-level reason to call back into `Run` for texture handles.
5. Keep `RuntimeRenderHost` as a compatibility adapter only until every pass
   consumes the narrower services.

Validation:

- `tools\validate_dx12_renderer.bat`
- Use `tools\validate_full.bat` if scene/replay/tool render behavior changes.

## Phase 2: Move Remaining Tool And Replay Behavior Out Of `Run`

Purpose: end the pattern where replay/tools own state but `Run` still owns the
decision flow.

Tasks:

1. Move replay scrub, focus-mask, prediction-ghost, and launcher-visual
   render-state decisions behind `ReplayRuntime` APIs.
2. Move editor, manipulator, mouse-pickup, ray-test, and launcher frame/update
   decisions behind `RuntimeTools` APIs.
3. Replace direct state mutation from `Run` with command structs where input or
   UI requests a tool/replay action.
4. Delete compatibility accessors after each route is migrated.

Validation:

- `tools\validate_full.bat`
- Add focused launch or UI stress validation only when the touched interaction
  path requires it.

## Phase 3: Move Scene Lifecycle Side Effects Out Of `Run`

Purpose: make scene loading, reset, generated setup, authored setup, defaults,
and browser/deck movement belong to scene runtime code.

Tasks:

1. Promote the scene load/reset snapshot and restore logic into scene runtime
   ownership.
2. Move scene browser selection, adjacent scene loading, cinematic deck
   movement, and UI model-count overrides into a scene-facing service.
3. Replace `Run`-owned scene callbacks with explicit lifecycle events:
   - `BeforeSceneUnload`
   - `AfterSceneCleared`
   - `BeforeScenePopulate`
   - `AfterScenePopulate`
   - `AfterSceneActivated`
4. Have replay, tools, diagnostics, renderer, and simulation subscribe through
   explicit update calls from the runtime shell, not hidden callbacks.

Validation:

- `tools\validate_full.bat`

## Phase 4: Collapse The Compatibility Surface

Purpose: leave `Run` as a visible shell, not a facade that hides the old shape.

Tasks:

1. Delete wrappers that only forward to subsystem methods.
2. Delete obsolete `RunInternal` state structs after their owners exist.
3. Remove temporary render-host callbacks once narrow services are in place.
4. Update runtime-boundary checks so regressions fail during `validate_fast`.
5. Keep only process-level methods on `Run`:
   - construction/destruction,
   - initialization/shutdown,
   - frame tick,
   - top-level event routing,
   - final owner wiring.

Validation:

- `tools\validate_fast.bat`
- `tools\validate_full.bat`

## Success Criteria

- `Run.h` no longer exposes subsystem internals as the default integration path.
- `RuntimeRenderHost` is removed or reduced to a small immutable render context.
- New replay/tool/scene/render features do not require adding a `Run::*`
  method.
- Runtime-boundary validation catches new broad `Run` dependencies.
- Full validation passes with zero warnings, zero DX12 validation errors, and
  byte-exact physics baseline output.

## Risks

| Risk | Mitigation |
|------|------------|
| Behavior changes hide inside extraction | Keep one owner movement per commit and use full validation at phase gates. |
| New subsystem APIs become broad `Run` copies | Add boundary checks before large movement begins. |
| Render host narrowing breaks overlay order | Validate with DX12 renderer gate and compare screenshots. |
| Scene lifecycle movement breaks replay/tool cleanup | Use explicit lifecycle events and full validation. |

## Handoff Notes

Implement this through the repo-local orchestrator skill when turning the plan
into code changes. This document is a focused architecture follow-up, not a
request to batch every phase into one branch.
