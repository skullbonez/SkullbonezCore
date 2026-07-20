# Render Graph Completion

Status: Active — 3/6 tasks (G0-G2 complete; G3-G5 pending)
Owner: repository owner; registered 2026-07-20 as campaign plan 5 of 8
Evidence: `../../Reports/2026-07-20/engine-architecture-review.md` (finding E)
Ledger: G0-G5

## Objective

**Owner decision 2026-07-20: finish the migration.** RenderGraph becomes the
owner of pass scheduling and barrier emission; the hand-written backend
barrier path and the per-pass direct/callback dual execution scaffolding are
deleted. Freezing the graph as a diagnostics layer was considered and
rejected. At closure there is exactly one way a frame pass executes and
exactly one authority for resource transitions, with recorded exceptions
only where DX12 reality demands them (e.g. present, backbuffer capture).

## Problem / Evidence

`RenderGraph.h:128-133`: barrier policy is `DiagnosticOnly`/`HandoffValidated`
— the graph documents intent while backend helpers own live transitions.
`RuntimeRenderer.h:184-205`: per-pass `callbackOwned` booleans and parallel
direct/graph execution paths mean every pass change reasons about two worlds.
Pass wrappers are parameter avalanches (`ExecuteReflectionThroughRenderGraph`
10 positional args, 5 bools). The graph already has fixed-capacity resources,
transient aliasing plans, dry-run validation, and callback execution — the
expensive foundations exist; only authority transfer remains.

## Non-Goals

- No visual change: DX12 screenshot baselines and replay visual fidelity are
  the oracle for every slice; zero refresh authorized.
- No pass reordering: the stable shadows → sky → reflection → scene target →
  objects → terrain → water → tornado → ghosts → overlay → post → UI/text
  order is preserved.
- No HAL statefulness retirement (that is `render-hal-modernization`); this
  plan may add typed pass-input structs but does not change
  `IRenderCommandContext` semantics.
- No multi-queue (compute/copy) scheduling; `RenderGraphQueueType` stays
  declared-but-Graphics-only.
- No new inheritance: pass payloads remain the existing typed-payload
  callback mechanism (`SetPassCallback<Callback, Payload>`).

## Binding Decisions

1. End state: every frame pass is `RenderGraphPassExecutionOwner::Callback`;
   `DeclarationOnly` survives only for declared-external bookkeeping rows
   (present/backbuffer), each with a recorded reason.
2. Barrier authority transfers resource-class by resource-class:
   graph-managed transients first, then owned render targets
   (shadow/reflection/scene/cinematic), then backbuffer edges. The backend's
   hand-written transition helpers are deleted per class as the graph takes
   ownership; a class is not "migrated" while the hand path still executes.
3. `RenderGraphBarrierPolicy` is retired at closure (the enum and both
   values deleted) — after authority transfer it can only describe the dual
   state this plan removes.
4. The per-pass `callbackOwned`/`*GraphResult` diagnostic flags in
   `RuntimeRenderer` are deleted at closure per their own recorded deletion
   condition (`RuntimeRenderer.h:180-183`).
5. Ride-along ratified by the owner: as each `Execute*ThroughRenderGraph`
   wrapper is touched, its positional bool avalanche is replaced by a typed
   per-pass input struct. No separate signature campaign.
6. Every slice runs the DX12 gate plus the mandatory bounded stress run
   (MASTER inventory rule 10). Barrier-authority slices additionally run the
   renderer gate three consecutive times (Danger Zones: upload/frame
   allocator and barrier risk).
7. DX12 validation layer at zero errors is a per-slice hard gate; a single
   validation message reverts the slice.

## G0 Authority Inventory And Migration Map

Inventory tip: `nightrunner-20th-july` after `run-execute-deaccretion` X2.
`RuntimeRenderer` creates a fresh graph per wrapper, assigns a typed callback,
compiles it, dry-runs it, and then executes the callback. Consequently every
listed frame pass already has `RenderGraphPassExecutionOwner::Callback`; there
is no live direct pass invocation beside the callback. The remaining duality is
barrier authority: callbacks still enter backend helpers that emit transitions
independently of the compiled graph. The `callbackOwned` results are diagnostic
proofs of callback execution, not alternate execution branches.

### Pass And Barrier Authority

| Stable order / pass | Current execution owner | Declared graph resources | Live handwritten transition on the pass's behalf | Resource class / migration slice |
|---|---|---|---|---|
| `ShadowMapPass` | Callback | Write `TerrainShadowMapDepth` and `ObjectShadowMapDepth` as depth | Each shadow framebuffer `Bind` changes color SRV -> RT and depth SRV/DepthRead -> DepthWrite; `Unbind` restores RT -> SRV and DepthWrite/DepthRead -> SRV | Owned shadow targets / G2 |
| `SkyboxPass` (non-cinematic) | Callback | Write `SwapchainBackbuffer` as render target | `ClearBackbuffer` normally changes Present -> RenderTarget before this wrapper; `Dx12FrameOwner::PrepareDraw` is the implicit fallback | Backbuffer edge / G4 |
| `DxrReflectionPass` | Callback | Read object shadow; write `DxrReflectionTexture` as UAV | `DispatchReflectionRays` changes reflection SRV -> UAV, emits a UAV ordering barrier, then changes UAV -> SRV | Owned DXR reflection target / G2 |
| `RasterReflectionPass` | Callback | Read object shadow; write `RasterReflectionColor` as RT and `RasterReflectionDepth` as depth | Reflection framebuffer `Bind`/`Unbind` emits the symmetric color and depth transition pairs | Owned reflection target / G2 |
| `CinematicSceneBegin` | Callback | Write `CinematicSceneColor` as RT and `CinematicSceneDepth` as depth | HDR framebuffer `Bind` changes color SRV -> RT and tracked depth SRV/DepthRead -> DepthWrite | Owned cinematic scene target / G2 |
| `ObjectOpaquePass` | Callback | Optional object-shadow read; write current frame color/depth | No pass-local barrier; it inherits the RT/depth state established by backbuffer preparation or cinematic target bind | World target use / G3; producer transition retires in G2/G4 |
| `TerrainPass` | Callback | Optional terrain/object-shadow reads; write current frame color/depth | No pass-local barrier; same inherited target state | World target use / G3 |
| `WaterPass` | Callback | Optional DXR/raster reflection read; write current frame color/depth | No pass-local barrier; reflection producers have already restored their texture to SRV | World target use / G3 |
| `TornadoVisualPass` | Callback | Write current frame color/depth | No pass-local barrier; same inherited target state | World target use / G3 |
| `ObjectTransparentPass` | Callback | Optional object-shadow read; write current frame color/depth | No pass-local barrier; same inherited target state | World target use / G3 |
| `ReplayPredictionGhostPass` | Callback | Optional object-shadow read; write current frame color/depth | No pass-local barrier; same inherited target state | World target use / G3 |
| `DebugOverlayPass` | Callback | Write current frame color/depth | No pass-local barrier; same inherited target state | World target use / G3 |
| `VolumetricLightPass` (optional) | Callback | Read cinematic scene color/depth; write transient `VolumetricLight` as RT | HDR framebuffer `Unbind` restores scene RT/depth to SRV. `Dx12GraphTransientPool::BeginRenderTarget` changes transient SRV -> RT and `EndRenderTarget` changes RT -> SRV | Graph transient / G1; scene-target release / G2 |
| `ToneMapPass` | Callback | Read scene color/depth and optional volumetric transient; write backbuffer RT | When volumetric is absent, HDR framebuffer `Unbind` restores scene RT/depth to SRV. Backbuffer `PrepareDraw` can still force RT before the fullscreen draw | Transient consumer / G1; scene target / G2; backbuffer / G4 |
| `UiTextPass` | Callback | Write `SwapchainBackbuffer` as RT | `Dx12FrameOwner::PrepareDraw` can force Present/CopySource -> RT before UI/text draws | Backbuffer edge / G4 |

The table records the current execution order. Disabled passes disappear
without moving their neighbours; cinematic sky is part of
`CinematicSceneBegin`, while ordinary sky is `SkyboxPass`. No migration slice
may reorder callbacks.

### Backend Transition Site Inventory

| Site | Current authority | Disposition |
|---|---|---|
| `Dx12GraphTransientPool::BeginRenderTarget` / `EndRenderTarget` | Emits graph-transient SRV <-> RT transitions by local `currentAccess` | G1 replaces these local transition decisions with the compiled graph's ordered transitions; target binding remains a backend operation |
| `FramebufferDX12::Bind` / `Unbind` | Emits color SRV <-> RT and tracked depth SRV/DepthRead <-> DepthWrite transitions | G2 migrates shadow, raster-reflection, and cinematic target instances class by class, then removes transition emission from framebuffer binding |
| `RenderBackendDX12::DispatchReflectionRays` | Emits reflection SRV -> UAV, UAV ordering, and UAV -> SRV barriers | G2 moves all three into graph execution; the UAV ordering barrier is required and is not an exception |
| `Dx12FrameOwner::TransitionBackbuffer`, `PrepareDraw`, `ClearBackbuffer`, and `PresentBackbuffer` | Tracks and transitions the current swapchain image | G4 transfers normal frame RT edges to graph execution; Present remains an explicit frame-edge exception |
| `Dx12ImGuiRendererOwner` development viewport copy | Temporarily changes the backbuffer to CopySource and the viewport texture CopyDest -> SRV | Retained frame-edge exception: editor-only copy occurs after world presentation and owns both resources as one bounded copy operation; record final reason in G4/G5 |
| `Dx12BackbufferCapture` readback | Temporarily changes backbuffer RT/Present -> CopySource and restores the prior state | Retained frame-edge exception: cold synchronous capture/readback is outside normal pass scheduling; record final reason in G4/G5 |
| Shutdown/resize backbuffer reconciliation | Ensures a swapchain image is Present before release/recreation | Retained lifecycle exception; it does not execute a frame pass |
| Texture upload/mip generation, mesh and dynamic-geometry upload, BLAS/TLAS build barriers | Resource creation/upload or acceleration-structure ordering outside the frame-pass texture graph | Out of scope, retained under their concrete resource owners; none may be used as a frame-pass escape hatch |

### Class-By-Class Transfer Order

1. **G1 — graph transients.** Feed the compiled transition sequence to the
   DX12 executor for materialized transient resources. Remove local
   `Dx12GraphTransientPool` state-based barrier emission while retaining
   descriptor allocation and render-target binding. Replace touched wrapper
   arguments with typed inputs.
2. **G2 — producer targets.** Register concrete native resources for shadow,
   raster/DXR reflection, and cinematic scene color/depth, execute their
   compiled transitions, and delete equivalent framebuffer/DXR barriers per
   completed class. A producer class is incomplete if either path can emit.
3. **G3 — world target users.** Consolidate objects, terrain, water, tornado,
   replay ghosts, and debug overlay into the single callback-owned graph
   schedule with typed inputs. Their declarations become the proof that target
   state spans adjacent world passes; no direct fallback or hidden target
   transition may remain.
4. **G4 — UI/text and frame edges.** Put normal backbuffer RT acquisition under
   graph authority, retain only explicitly recorded Present/capture/editor-copy
   and lifecycle exceptions, then delete `RenderGraphBarrierPolicy`,
   `callbackOwned`, and the diagnostic result scaffolding.
5. **G5 — closure.** Reconcile this inventory against source, finalize the
   exception reasons, extend the architecture tests, and prove the single
   execution/barrier path with full, perf, and stress evidence.

## G1 Transient Barrier Transfer Evidence

The compiler's two `VolumetricLight` transitions are now the live DX12 path.
`Dx12GraphTransientPool::ExecuteTransitions` consumes the compiled list for the
active callback pass, resolves the materialized physical slot, verifies the
compiler's before-state against tracked physical state, emits exactly one native
barrier, and advances the tracked state. `BeginRenderTarget` and
`EndRenderTarget` now change descriptors/targets only and contain no transition
call. `CinematicPostGraphInputs` is the typed wrapper input for the touched post
chain. Runtime evidence recorded a valid reused 632x340 RGBA16F binding and
exactly one compiled transition in each callback:

```text
VolumetricLightPass: PixelShaderResource -> RenderTarget; emitted=1
ToneMapPass: RenderTarget -> PixelShaderResource; emitted=1
materialization_failed=false; pool_size=1; reused_this_compile=1
```

Validation at the unchanged G1 tip:

- Direct Automation build: PASS in 19.2 s, zero warnings/errors.
- `tools\validate_dx12_renderer.bat` run 1: PASS in 78.3 s; zero DX12
  validation errors and all three screenshot baselines matched.
- Runs 2 and 3: PASS in 55.0 s and 55.1 s with the same zero-error,
  byte-identical visual result.
- `tools\run_graphics_stress.bat 1`: PASS in 61.58 s; PID 51540 ran for the
  bounded minute and closed by the PID-scoped timeout without a crash.
- The initial formal attempt stopped before build/runtime on formatting; the
  two implementation files and one header were formatted, and the required
  three-consecutive sequence restarted from zero.
- Touched-source comment audit: 7/7 checked, 0 deferred. Every touched source
  file has the required learning header and the new barrier/state hazards are
  documented beside the authority checks.

## G2 Producer Target Transfer Evidence

Shadow, raster/DXR reflection, and cinematic scene resources now publish exact
native tokens through opaque engine texture handles. Producer callbacks execute
compiled SRV -> output transitions; callback-owned publish nodes execute the
matching output -> SRV edges. The DXR publish edge also emits the required UAV
ordering barrier before its compiled UAV -> SRV transition. Hand authority is
deleted: `FramebufferDX12::Bind`/`Unbind` changes descriptors only,
`DispatchReflections` contains no barrier, and both former local state trackers
are gone. Framebuffer depth and DXR reflection textures start shader-readable,
so first-frame and steady-frame graph before-states are identical.

The cinematic post graph recorded three emitted transitions before volumetric
(scene color, scene depth, transient producer) and one before tonemap (transient
consumer), with materialization successful. The redundant legacy volumetric
framebuffer/fallback was removed; allocation failure now disables the optional
effect after its Lane R diagnostic while the graph still publishes scene inputs.
`ShadowGraphInputs` and `ReflectionGraphInputs` replace the touched wrappers'
positional parameter sets.

Validation at the unchanged G2 tip:

- Direct Automation build: PASS in 19.2 s, zero warnings/errors.
- `tools\validate_dx12_renderer.bat` runs 1-3: PASS in 79.7 s, 55.1 s,
  and 55.7 s; every run reported zero DX12 validation errors and matched all
  three committed screenshot baselines.
- `tools\run_graphics_stress.bat 1`: PASS in 61.96 s; PID 42632 completed the
  bounded minute and closed by PID-scoped timeout without a crash.
- Static retirement proof: no `ResourceBarrier`, framebuffer depth-state
  tracker, or DXR reflection-state tracker remains in the retired hand paths.
- Touched-source comment audit: 14/14 checked, 0 deferred; barrier ownership,
  native-token lifetime, UAV ordering, and bind-only framebuffer invariants are
  documented beside the relevant code.

## Tasks

- [x] G0 — Authority inventory and migration map: for every pass, record
  execution owner (direct vs callback), every hand-written barrier the
  backend emits on its behalf, and the resource class each barrier belongs
  to; define the class-by-class migration order and the expected exception
  list. Output: migration table committed into this plan. No validation
  (documentation).
- [x] G1 — Transient resource class: graph-compiled transitions become the
  live barrier path for graph-managed transients (volumetric, tonemap
  inputs); delete the corresponding hand-written transitions. Typed input
  structs for the touched wrappers. Validation:
  `tools\validate_dx12_renderer.bat` ×3 consecutive +
  `tools\run_graphics_stress.bat 1`.
- [x] G2 — Producer target class: shadow, reflection, scene/cinematic
  targets move to graph-owned transitions and callback execution; delete
  the hand path per class. Typed input structs for touched wrappers.
  Validation: `tools\validate_dx12_renderer.bat` ×3 +
  `tools\run_graphics_stress.bat 1`.
- [ ] G3 — World pass class: objects, terrain, water, tornado visual, replay
  ghosts, debug overlay become callback-owned with graph transitions; delete
  their direct execution fallbacks. Replay-facing presentation/submission is
  touched here, so the replay fidelity gate applies. Validation:
  `tools\validate_dx12_renderer.bat` ×3 + `tools\run_graphics_stress.bat 1`
  + `tools\validate_replay_visual_fidelity.bat` (one invocation, one engine
  process, zero golden refresh).
- [ ] G4 — UI/text and frame-edge class: UI/text pass and
  backbuffer/present edges resolve; remaining `DeclarationOnly` rows each
  get a recorded reason. Delete `RenderGraphBarrierPolicy`, the
  `callbackOwned` flags, and the dual-path scaffolding per binding
  decisions 3-4. Validation: `tools\validate_dx12_renderer.bat` ×3 +
  `tools\run_graphics_stress.bat 1`.
- [ ] G5 — Closure: grep proofs (no `callbackOwned`, no
  `RenderGraphBarrierPolicy`, no direct-execution fallbacks), exception
  table finalized, independent rubber-duck review (single end-of-plan),
  DX12-architecture CPU test target updated to assert the new single-path
  contract. Validation: `tools\validate_full.bat` +
  `tools\validate_perf.bat` (graph execution replaced direct calls on the
  frame hot path) + `tools\run_graphics_stress.bat 1` at closure tip.

## Acceptance

- One execution path and one barrier authority per resource class; recorded
  exceptions only at frame edges.
- Zero DX12 validation errors across all slices; screenshot baselines and
  replay visual fidelity byte-identical with zero refresh.
- Dual-path scaffolding, `RenderGraphBarrierPolicy`, and `callbackOwned`
  diagnostics deleted (their recorded deletion condition is satisfied).
- Perf gate at closure shows no frame-time regression outside noise
  (record numbers).
- Independent review clear.

## Validation Summary

Every DX12 slice: `validate_dx12_renderer` (×3 for barrier-authority
slices) + `run_graphics_stress.bat 1` with recorded command, runtime, and
exit evidence. G3 adds the one-invocation replay fidelity mega gate.
Closure: `validate_full` + `validate_perf` + stress.
