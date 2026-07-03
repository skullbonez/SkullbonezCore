# Codebase Top 10 Cleanup Plan

Date: 2026-06-18
Status: Draft source-audit backlog
Impact area: architecture, runtime, DX12 renderer, physics, scene system, UI, tooling, diagnostics
Validation for this document-only change: none required

## Goal

Capture the ten ugliest source-audited cleanup targets found during the
repo-wide static review on 2026-06-18. This is not an instruction to implement
all ten items in one branch. Treat it as a ranked backlog of high-leverage
areas that should become focused plans or small extraction slices.

The review made no source changes and did not run validation scripts. The
current edit that created this document is documentation-only.

## Top 10 Cleanup Targets

### 1. Shrink `SkullbonezRun`

`SkullbonezRun` is still the engine's largest runtime responsibility magnet.
It owns render passes, UI, editor state, debug overlays, capture/perf hooks,
scene load/reset behavior, input routing, physics tick integration, ray tests,
and diagnostics handoff.

Evidence:

- `SkullbonezSource/SkullbonezRun.h:901` owns UI, debug systems, physics
  visualizers, world state, `GameModelCollection`, DXR reflection transforms,
  and all major pass objects.
- `SkullbonezSource/SkullbonezRun.h:931` starts a long private method surface
  for rendering, input, scene setup/load/reset, assets, capture, perf logging,
  physics tick, live style, editor gizmos, ray tests, and diagnostics.
- `Agentic/Plans/architecture_pass_2026-06-02.md` already calls out runtime
  ownership extraction as high priority.

Fix direction:

- Move cohesive behavior into existing runtime facades where possible.
- Prioritize shrinking `LoadScene()` and `TakeInput()` by ownership boundary,
  not by simple file splitting.
- Use `tools\validate_full.bat` for broad runtime movement at PR gate. Smaller
  isolated input or diagnostic moves may qualify for `tools\validate_fast.bat`.

### 2. Untangle Physics Authority From `GameModel`

Physics now has a stronger `PhysicsWorld`, but authoritative storage still
flows through `GameModelCollection` and its `std::vector<GameModel>`.
`GameModel` itself mixes rigid body state, collision shape, render material,
terrain/world pointers, response mailboxes, names, and fixed-object state.

Evidence:

- `SkullbonezSource/SkullbonezGameModelCollection.h:48` says the class should
  be a container/facade, but it still friends multiple physics and diagnostics
  systems.
- `SkullbonezSource/SkullbonezGameModelCollection.h:75` mixes add/clear,
  physics, rendering, shadows, snapshots, and body/render stream access.
- `SkullbonezSource/SkullbonezPhysicsWorld.cpp:312` reaches into
  `collection.m_gameModels` during `RunPhysics()`.
- `SkullbonezSource/SkullbonezGameModel.h:104` shows body, collider, render,
  terrain, material, and mailbox state living together.

Fix direction:

- Make rigid bodies and colliders the authoritative physics data.
- Keep render instance data as a projection from physics/game state rather than
  the storage root.
- Use `tools\validate_physics.bat` for behavior changes; add
  `tools\validate_perf.bat` when storage or hot-loop layout changes.

### 3. Finish Render Graph Ownership

The render graph exists, but it is not yet the sole owner of pass execution,
transient resources, or full resource-state tracking. Legacy DX12 helper paths
still emit concrete transitions and UAV barriers in several places.

Evidence:

- `SkullbonezSource/SkullbonezRenderGraph.cpp:221` documents that the compiler
  does not record GPU commands, allocate transient textures, optimize, or run
  async work.
- `SkullbonezSource/SkullbonezRenderGraph.cpp:253` treats `Unknown` initial
  access as a signal that legacy code owns real DX12 state.
- `SkullbonezSource/SkullbonezRenderBackendDX12.cpp:318` still has direct
  concrete graph transition execution.
- `SkullbonezSource/SkullbonezRenderBackendDX12.DXR.cpp:528` manually sequences
  DXR reflection SRV/UAV transitions and barriers.

Fix direction:

- Move from helper-owned barriers and diagnostic frame graphs to graph-owned
  pass callbacks and state tracking.
- Avoid another broad manual-barrier migration; the next useful slice is pass
  ownership.
- Use `tools\validate_dx12_renderer.bat` at PR gate and verify
  `dx12_validation.txt` remains zero-error.

### 4. Replace the Scene Parser's Hand-Written Compiler Shape

`SkullbonezTestSceneParser.cpp` has become a full bespoke DSL compiler with
manual token buffers, directive-local parsing, fixed field orders, and a large
dispatch table.

Evidence:

- `SkullbonezSource/SkullbonezTestSceneParser.cpp:509` parses vector values
  through fixed local buffers and `strtok_s`.
- `SkullbonezSource/SkullbonezTestSceneParser.cpp:1133` begins fixed-token
  object parsing for ball directives.
- `SkullbonezSource/SkullbonezTestSceneParser.cpp:1937` contains a large
  directive dispatcher spanning physics, UI, screenshots, camera, objects,
  debug settings, world settings, materials, and worker threads.
- `SkullbonezSource/SkullbonezTestScene.h:221` mirrors the parser growth with
  many `hasX` fields and scene option flags.

Fix direction:

- Keep deterministic plain-text scenes, but move directive bodies toward typed
  schema metadata and structured diagnostics.
- Split object, physics, cinematic, style, and capture directives into smaller
  handlers.
- Use `tools\validate_fast.bat` for parser-only changes; escalate to
  `tools\validate_full.bat` if load semantics can change.

### 5. Finish Componentizing `InGameUI`

The UI has tab structs and some component movement, but `InGameUI` still owns
window state, resources, hitboxes, input handling, cache state, tab switching,
render controls, cinematic controls, profiler toggles, and draw orchestration.

Evidence:

- `SkullbonezSource/UI/SkullbonezUI.h:168` exposes the broad UI facade API.
- `SkullbonezSource/UI/SkullbonezUI.h:203` starts a large private state block
  covering window, toggles, buttons, combos, slider specs, resources, tab
  state, active slider, and overlays.
- `SkullbonezSource/UI/SkullbonezUI.cpp:2492` starts the main `Draw()` path
  that handles cache, minimized state, tabs, profiler, scene, physics, editor,
  options, render, targets, cinematic, and footer drawing.
- `Agentic/Bugs.md:18` already notes that cinematic controls were added inline
  and should move to `UITabCinematic`.

Fix direction:

- Extract `UITabCinematic` first because the debt is already named.
- Continue tab-by-tab extraction of layout, input, and drawing into focused
  tab controllers.
- Use `tools\validate_fast.bat` at PR gate for UI-only refactors unless runtime
  launch or rendering behavior changes.

### 6. Harden Lightweight Agent Workflow Metadata

The retired repository-owned orchestrator is no longer the place to improve
agent process. The active process surface is now the lightweight skill contract
plus the repository validation map. Future cleanup should make that path easier
to audit without rebuilding a queue/state-machine tool.

Evidence:

- `AGENTS.md` defines the startup contract, plan implementation default,
  validation mapping, dirty-worktree policy, and commit expectations.
- `Agentic/Skills/orchestrator/SKILL.md` is the active coordination contract
  for fresh worker agents, rubber-duck review agents, validation, commits, and
  pushes.
- `tools/README.md` lists the available validation helpers.
- `Agentic/SessionState.md` remains the compact shared state agents read at
  startup.

Fix direction:

- Keep the orchestrator skill short, human-readable, and free of executable
  queue/state-machine behavior.
- Add machine-readable validation or workflow metadata only when it directly
  supports gate selection, report hygiene, or agent handoff checks.
- Use `tools\validate_fast.bat` for executable helper changes; documentation-only
  process edits require no repository validation.

### 7. Remove Static Render Helper State

`SkullbonezHelper` still uses static shader handles, mesh handles, instance
vectors, material texture state, and file-static batch flags. `AssetSystem`
also exposes a transitional active-system bridge for legacy singleton-style
helpers.

Evidence:

- `SkullbonezSource/SkullbonezHelper.h:57` declares static shader, mesh,
  instance, and clip-plane state.
- `SkullbonezSource/SkullbonezHelper.cpp:47` defines the static helper state.
- `SkullbonezSource/SkullbonezHelper.cpp:122` directly mutates backend blend
  and depth state through `Gfx()`.
- `SkullbonezSource/SkullbonezAssetSystem.h:151` calls out the active asset
  system as a transitional bridge.

Fix direction:

- Make helper state instance-owned by render/runtime context.
- Move mesh/material caches under asset-system ownership.
- Use `tools\validate_dx12_renderer.bat` for renderer asset/helper behavior
  changes.

### 8. Split the Wide `IRenderBackend`

`IRenderBackend` is currently a single engine-facing facade for device
lifecycle, window state, draw state, resource creation, screenshots,
diagnostics, DXR, GPU timers, platform markers, dynamic vertex buffers, debug
lines, and instancing.

Evidence:

- `SkullbonezSource/SkullbonezIRenderBackend.h:85` covers lifecycle/window
  behavior.
- `SkullbonezSource/SkullbonezIRenderBackend.h:130` covers shader, mesh,
  framebuffer, texture, and capture resource creation.
- `SkullbonezSource/SkullbonezIRenderBackend.h:172` covers draw-call
  diagnostics and tracing.
- `SkullbonezSource/SkullbonezIRenderBackend.h:202` exposes DXR methods on
  the generic backend interface.
- `SkullbonezSource/SkullbonezIRenderBackend.h:254` exposes dynamic vertex
  buffer, debug line, and instanced mesh APIs.

Fix direction:

- Split into focused interfaces only as graph/backend/tooling work needs the
  narrower contract.
- Good candidate boundaries: device/resources, frame commands, diagnostics,
  ray tracing, profiling, and debug draw.
- Use `tools\validate_dx12_renderer.bat` for backend interface work.

### 9. Promote Known Physics Bugs Into Validation Coverage

Several documented physics issues are still open: stacking drift/topple,
terrain micro-bounce/jitter, and box-ball interpenetration in `at_rest.scene`.
These are risky because deterministic baselines can accidentally normalize bad
behavior if the scenes are not first-class validation targets.

Evidence:

- `Agentic/Bugs.md:105` documents stacking drift/topple in `stacking.scene`.
- `Agentic/Bugs.md:115` documents terrain micro-bounce/jitter.
- `Agentic/Bugs.md:125` documents box-ball interpenetration around frame 570
  of `at_rest.scene`.
- `tools/validate_physics.bat:57` uses `physics_regression_solver.scene`, so
  known bug scenes need an explicit coverage decision before solver changes.

Fix direction:

- Add narrow diagnostic or validation coverage before changing solver behavior.
- Use SkullScope for focused investigation instead of ingesting large raw CSV
  or NDJSON artifacts.
- Solver or baseline work must finish with `tools\validate_physics.bat`.

### 10. Make Profiler Accounting Harder to Misread

Profiler/debug visibility has known blind spots around unbucketed physics time
and VSync-dominated views. The profiler also derives hierarchy from
slash-delimited path names, which makes parent/child accounting fragile.

Evidence:

- `Agentic/Bugs.md:54` notes that profiler tree accounting hides unbucketed
  physics time and can make time appear under `VsyncWait`.
- `Agentic/Bugs.md:67` requests a visual mode that excludes VSync wait because
  it dominates bar scale.
- `SkullbonezSource/SkullbonezProfiler.cpp:176` resolves parent indices from
  slash-delimited scope names.
- `SkullbonezSource/UI/UITabProfiler.cpp:418` default-expands frame, UI,
  physics, and draw trace roots.

Fix direction:

- Track unbucketed time explicitly.
- Add a VSync-excluded view for performance analysis.
- Make parent/child accounting explicit rather than relying only on path-name
  reconstruction.
- Use `tools\validate_fast.bat` for profiler/UI-only changes; escalate if
  platform profiler markers or runtime launch behavior changes.

## Suggested Sequencing

1. Start with named, narrow extractions: `UITabCinematic`, profiler accounting,
   and parser directive handlers.
2. Then attack structural boundaries: `SkullbonezRun`, physics storage, render
   graph ownership, and helper/global state.
3. Treat backend interface splitting and agent workflow metadata as enabling
   work for specific follow-up tasks, not cleanup for its own sake.

## Notes

- Documentation-only changes require no validation.
- Implementation work from this plan should use
  `Agentic/Skills/orchestrator/SKILL.md` unless the user explicitly asks to
  bypass it.
- Dirty worktrees must be checked before each implementation slice, and
  unrelated user-owned files must be left alone.
