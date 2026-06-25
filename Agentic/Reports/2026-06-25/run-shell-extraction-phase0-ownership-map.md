# Run Shell Extraction Phase 0 Ownership Map

Date: 2026-06-25  
Branch: `nightrunner-25th-june`  
Plan: `Agentic/Plans/run-shell-extraction-plan.md`  
Status: Phase 0 ownership map and guardrails complete; extraction phases remain open.

## Scope

This map classifies the remaining `Run` composition-root state after the
runtime run decomposition work. The goal is to keep `Run` responsible for
process lifetime and frame ordering while making remaining compatibility
surfaces visible.

Documentation-only validation: none required for this report. Guardrail tooling
added separately is covered by `tools\validate_fast.bat`.

## Process Lifetime And Composition Root

These members are currently acceptable in `Run` because they define process
startup, shutdown, frame order, or cross-subsystem composition:

| Member | Classification | Notes |
|--------|----------------|-------|
| `m_sceneController` | Scene owner | Owns scene queue and active scene-run state. |
| `m_sceneCoordinator` | Scene owner | Coordinates scene load/reset/advance decisions. |
| `m_sceneBrowser` | Scene owner / UI bridge | Scene discovery and selected live style. Candidate for future scene runtime service. |
| `m_launchOptions` | Process/session policy | CLI/startup policy reapplied across scene loads. |
| `m_defaultCinematicRender` | Process/session baseline | Engine config baseline restored for generated demo scene styles. |
| `m_startup` | Process/session baseline | Startup capacity and worker defaults. |
| `m_sceneUIOverrides` | Scene/UI bridge | Preserves selected live UI overrides across reset. Candidate for scene service ownership. |
| `m_runtimeSettings` | Runtime session policy | Scene/app runtime swap policy toggles. |
| `m_timers` | Frame-order state | Frame/simulation timing and rolling timing values. |
| `m_runtimeCommands` | Frame boundary intent | Deferred runtime/tool command intent at frame boundary. |
| `m_engineContext` | Compatibility binding view | Borrowed view over runtime-owned systems; not an owner. |
| `m_runtimeViewModel` | Presentation snapshot | Scalar runtime snapshot for UI/diagnostics. |

## Subsystem Owners In Run

These are top-level subsystem owners today. The long-term direction is not to
remove all of them from `Run`, but to make their public APIs narrow enough that
new behavior lands inside the owning subsystem.

| Member | Owner Category | Current Status |
|--------|----------------|----------------|
| `m_diagnosticsRuntime` | Diagnostics owner | Owns capture, perf, queryable physics diagnostics, and stress harness state. |
| `m_systems` | Mixed runtime systems | Window, camera, texture, terrain, and pass resource ownership remain bundled. Split under service-context work. |
| `m_runtimeInput` | Input owner | Semantic input action/mode state. Routing is still coordinated by `Run::TakeInput()`. |
| `m_interaction` | Interaction owner | Authoritative workspace, world-input owner, gesture, and frame policy. |
| `m_interactionAutomation` | Runtime test harness | CLI interaction script state for regression scenes. |
| `m_camera` | Camera compatibility state | Camera label/input state still overlaps interaction policy. |
| `m_attachedCamera` | Camera tool state | Non-serialized attach camera target/offset state. |
| `m_simulation` | Simulation frame policy | Timestep policy and physics accumulators. |
| `m_replayRuntime` | Replay owner | Owns replay recorders, branch provenance, render-pose state, and replay interaction state. |
| `m_solverReplayMismatch` | Replay diagnostics bridge | Throttles live-vs-solver mismatch reports. Candidate for replay diagnostics ownership. |
| `m_liveStyle` | Style/capture harness | Live style tweak/capture state. Candidate for scene/style service ownership. |
| `m_UI` | UI owner | In-game diagnostics window. |
| `m_debug` | Debug/session toggles | Runtime debug and overlay toggles. |
| `m_runtimeTools` | Tool owner | Launcher, editor, manipulator state, and transient render feedback. |
| `m_broadphaseVisualizer` | Physics debug view | Debug overlay; candidate for diagnostics/tools ownership. |
| `m_collisionVisualizer` | Physics debug view | Debug overlay; candidate for diagnostics/tools ownership. |
| `m_physicsDebugVisualizer` | Physics debug view | Debug overlay; candidate for diagnostics/tools ownership. |
| `m_cWorldEnvironment` | World owner | Fluid, gravity, terrain bounds; should move toward `WorldContext`. |
| `m_cGameModelCollection` | World data facade | Scene bodies and solver-visible object state; addressed by game-model data boundary plan. |
| `m_renderHost` | Render compatibility adapter | Broad render-facing bridge. Guardrails now prevent growth. |
| `m_renderer` | Render owner | Runtime render passes and frame render ordering. |

## Compatibility Surface To Shrink

| Surface | Reason It Remains | Next Owner |
|---------|-------------------|------------|
| `RuntimeRenderHostBindings` and callbacks | Render passes still need a broad borrowed view over runtime state. | Narrow render views under run-shell Phase 1. |
| `Run::TakeInput()` and pointer routing helpers | Input is partly centralized but still dispatches several tool/replay branches. | Interaction controller plus runtime tool/replay routers. |
| Scene lifecycle helpers on `Run` | Scene load/reset still coordinates world, replay, UI, diagnostics, and renderer side effects. | `SceneRuntimeCoordinator` / scene lifecycle events. |
| Replay path, prediction, cause-tree, velocity functions | `ReplayRuntime` owns state, but decision flow and render/tool bridges remain in `Run`. | `ReplayRuntime` APIs and replay tool handlers. |
| Editor/manipulator/launcher functions | `RuntimeTools` owns state, but some input/update code remains in `Run` methods. | `RuntimeTools` handlers and interaction commands. |
| Texture/config/window/global service access | Mixed in runtime/render/world paths. | Global service context plan. |
| `GameModelCollection` access | Still the world-data compatibility facade. | Game model data boundary plan. |

## Guardrails Now In Place

`tools/check_runtime_boundaries.py` now fails the fast gate for:

- render pass class definitions regrowing in `Run.h`
- replay recorder fields regrowing in `Run.h`
- tool transient fields regrowing in `Run.h`
- scene object population helper declarations regrowing in `Run.h`
- runtime subsystem headers storing `Run*` or `Run&`
- new `RuntimeRenderHostBindings` fields
- new `RuntimeRenderHostCallbacks` typedefs or fields
- new mutable render-host callback returns, except the existing cinematic config
  compatibility callback
- direct camera-mode writes outside the interaction bridge
- direct world-interaction owner writes outside the transition bridge
- new duplicated `TryPick*Model*` runtime pick helper declarations or
  definitions outside `RuntimePickService`

## Next Slices

1. Split `RuntimeRenderHostBindings` into narrower read views without changing
   pass behavior.
2. Move replay overlay callback decisions behind `ReplayRuntime`.
3. Move tool/editor overlay decisions behind `RuntimeTools`.
4. Promote scene lifecycle side effects into explicit scene events.
5. Retire compatibility wrappers only after their owner APIs are stable.

## Validation Impact

- This ownership map is documentation-only: no validation required.
- Guardrail tooling changes: `tools\validate_fast.bat` plus the changed script.
- Render-host narrowing: `tools\validate_dx12_renderer.bat`; use
  `tools\validate_full.bat` if scene/replay/tool runtime behavior changes.
- Scene lifecycle movement: `tools\validate_full.bat`.
