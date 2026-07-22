# RuntimeRenderer Decomposition

Date: 2026-07-22
Owner: skullbonez
State: In progress; RR0-RR4 complete
Ledger tasks: 6 (RR0-RR5)

## Problem And Evidence (2026-07-22, main tip 0c5263e1)

`RuntimeRenderer` (`SkullbonezSource/Runtime/Render/RuntimeRenderer.h`,
2,639-line implementation) is the engine's next god object forming. Current
header facts:

- ~50 members spanning seven DX12 owner pointers, texture collection,
  cameras, terrain, skybox, window, engine config, world environment, three
  physics debug visualizers, fifteen pass objects, text batch, render-graph
  scratch, and replay presentation state
  (`m_consequenceGradeStrength` / `m_consequenceGradeLastTick`).
- A nine-argument constructor.
- `RenderUiText` takes eleven parameters including `RunTimerState`,
  `DiagnosticsRuntime`, `ReplayHudStatus`, and a replay overlay render
  context; `ExecuteUiTextThroughRenderGraph` takes thirteen. A renderer that
  needs timers, diagnostics runtime, and two replay types to draw text is an
  orchestration layer wearing a renderer's name.
- The API mixes process resource lifecycle, per-frame graph building, scene
  ray-tracing warmup, UI/HUD text composition, replay overlays, cinematic
  policy, and render-target preview snapshots on one class.

The per-pass graph-input records (`ObjectGraphInputs` etc.) and the one-live-
graph invariant from `render-graph-completion` are sound and are not the
problem; the hosting class's breadth is.

## Goal

`RuntimeRenderer` owns exactly: pass instances, backend-resource lifetime,
and frame-graph orchestration (begin/compile/finalize, world pass order).
UI/HUD text composition and replay presentation grading move to cohesive
owners with typed value boundaries. Member count, constructor arity, and the
widest method signatures drop measurably against the RR0 baseline.

## Non-Goals

- No render-pass behavior, pass-order, image, or barrier change. DX12
  screenshots stay within committed thresholds; InfoQueue stays at zero.
- No reintroduction of a render interface layer (render-interface-retirement
  closure stands; concrete DX12 owners remain non-polymorphic).
- No new context bag: extracted owners take named typed inputs, not the
  renderer's member set by another name.
- No render-graph architecture change (one live graph, declaration-only
  Present edge — `render-graph-completion` closure stands).

## RR0 Baseline Census (2026-07-22, starting tip 88d78670)

The authoritative starting surface is
`SkullbonezSource/Runtime/Render/RuntimeRenderer.h` (407 text lines) plus its
2,639-line implementation. CodeGraph was current at the census and reported
eight direct users; the counts below were confirmed against the actual header
and direct include rows.

### Data-member denominator

`RuntimeRenderer` starts with **46 data members**. Multi-line declarations
count once. Nested frame/context/input records are type declarations, not
owner state, and are excluded.

| Domain | Count | Exact members | RR ruling |
|---|---:|---|---|
| Concrete DX12 owner borrows and lifecycle log | 8 | `m_renderFrame`, `m_renderGraph`, `m_renderResources`, `m_renderTextures`, `m_renderGeometry`, `m_renderDiagnostics`, `m_renderRayTracing`, `m_lifecycleLog` | RR3 resource-lifecycle surface |
| World, scene, policy, and backend-lifetime resources | 11 | `m_assets`, `m_textures`, `m_cameras`, `m_terrain`, `m_skyBox`, `m_window`, `m_passResources`, `m_config`, `m_presentationSettings`, `m_world`, `m_primitiveBatches` | Keep only resources genuinely shared by renderer passes; RR3/RR4 decide survivors |
| Debug and GPU profiling | 5 | `m_collisionVisualizer`, `m_broadphaseVisualizer`, `m_physicsDebugVisualizer`, `m_profiler`, `m_renderGpuTiming` | Typed debug/pass borrows may remain; RR4 recounts |
| Replay consequence-grade animation | 2 | `m_consequenceGradeStrength`, `m_consequenceGradeLastTick` | Must leave the renderer in RR2 |
| DXR frame scratch | 1 | `m_dxrReflectionTransforms` | Renderer-owned bounded pass scratch |
| World/cinematic pass instances | 11 | `m_fullscreenQuadPass`, `m_skyPass`, `m_sceneTargetPass`, `m_shadowPass`, `m_reflectionPass`, `m_objectPass`, `m_terrainPass`, `m_waterPass`, `m_debugOverlayPass`, `m_volumetricPass`, `m_tonemapPass` | Core renderer authority; stays cohesive |
| UI text pass infrastructure | 2 | `m_textBatch`, `m_uiTextPass` | RR1 extraction candidate |
| Live frame-graph scratch and state | 5 | `m_renderPassGraphScratch`, `m_renderPassCompileScratch`, `m_frameGraphSnapshot`, `m_frameGraphRenderGraph`, `m_frameGraphFinalized` | One-live-graph invariant; stays with orchestration |
| UI-text ray-tracing borrow | 1 | `m_uiTextRayTracing` | RR1/RR3 remove or relocate with UI text resources |
| **Total** | **46** |  | RR4 acceptance denominator |

### Constructor and wide-call denominators

The constructor starts at **10 parameters**: eight concrete DX12 owner
pointers, one `RenderWorldView`, and one `RunSceneState`. The world view already
groups stable owner borrows; RR3 must not replace the remaining arguments with
an unowned service bag.

Exactly **two ordinary methods have seven or more parameters**:

| Method | Arity | Domains mixed at the starting tip | Planned owner |
|---|---:|---|---|
| `RenderUiText` | 12 | DX12 diagnostics, UI render facts, UI pass state, timers, operator UI, render models, diagnostics runtime, replay HUD, replay overlay, cinematic config/policy, frame delta | RR1 UI-text composition owner; target <=6 |
| `ExecuteUiTextThroughRenderGraph` | 13 | The same 12 domains plus the ray-tracing owner borrow | RR1 UI-text pass owner/resource seam; target <=6 |

The constructor is tracked separately from the wide-method count. No other
public or private `RuntimeRenderer` method reaches seven parameters;
`EnsureUiTextResources` is the next widest at six.

### Direct include fan-in denominator

Direct fan-in is **8 files**:

| Direct includer | Reason for dependency |
|---|---|
| `Runtime/Render/RuntimeRenderer.cpp` | owner implementation |
| `Runtime/Run.h` | owns the single process renderer |
| `Runtime/InputFrameExecution.cpp` | applies renderer-facing input commands |
| `Runtime/OperatorCommandApplier.cpp` | applies concrete render-device/UI commands |
| `Runtime/RuntimeStressController.cpp` | applies bounded graphics-stress mutations |
| `Runtime/Scene/RunScene.cpp` | scene activation/resource warmup and reset sequencing |
| `Runtime/Scene/SceneRuntimeReset.cpp` | captures/restores presentation policy values |
| `Runtime/Replay/ReplayRestoreService.h` | replay restore transaction borrows the concrete renderer |

Only `RenderDevelopmentUi` is declaration-guarded by
`SKULLBONEZ_DEVELOPMENT_TOOLS`; no data member is conditionally compiled.
Replay currently crosses both the UI-text signatures and the consequence-grade
state, while resource lifecycle and frame-graph orchestration share the same
owner. These are the RR1-RR3 extraction seams; the one-live-graph order,
declaration-only Present edge, DX12 image thresholds, and zero-InfoQueue rule
are the validation-sensitive invariants.

## RR1 UI-Text Ownership Evidence (2026-07-22)

- `UiTextPassInputs` is the one named stack-only frame record at the public
  `RenderUiText` boundary, the private `ExecuteUiTextThroughRenderGraph`
  boundary, and the synchronous graph callback. Both formerly wide renderer
  methods fell from 12/13 parameters to one.
- `UiTextPass` now owns its fixed-capacity `TextBatch`, startup-bound profiler
  and GPU-timing links, and the optional ray-tracing presentation capability.
  RuntimeRenderer no longer stores `m_textBatch` or `m_uiTextRayTracing`; its
  member denominator is provisionally 44 before the authoritative RR4 recount.
- RuntimeRenderer retains the one-live-graph scheduling work: declare the late
  backbuffer write, attach the callback, compile transitions, and execute the
  new callback range. HUD, operator UI, replay HUD/overlay values, cinematic
  facts, diagnostics, models, timers, and frame delta cross as one record.
- Replay remains the owner of `ReplayHudStatus`; the operator composer requests
  that value once and places it in the frame record. The UI-text owner consumes
  values and does not borrow ReplayRuntime or reach back into replay authority.
- No UI-text path signature exceeds six parameters. `EnsureGpuResources` is
  six; release, predicate, capability, render, and renderer scheduling calls are
  one or two. No callback pack, context/service bag, heap/growth path, new owner
  reach-back, downward Replay include, or replay allocation privilege appeared.
- Touched-source comment audit: `RuntimeRenderPasses.h`, `RuntimeRenderer.h/.cpp`,
  `OperatorEditorFrameComposer.cpp`, and `UiTextPass.cpp` checked 5/5; this
  touched-file pass needs no checklist path, with zero deferred/unchecked files
  and no wording awaiting owner approval.
- Focused builds passed: Profile 11.8 s before formatting, final formatted
  Profile 10.8 s, and Automation 11.5 s, all with zero warnings/errors.
- `tools\validate_dx12_renderer.bat` passed in 55.8 s with zero InfoQueue
  errors and all three committed images accepted. `tools\run_graphics_stress.bat 1`
  completed crash-free in 61.1 s and stopped only by its recorded PID timeout.
- Final `tools\validate_full.bat` passed in 122.6 s: CPU/coverage and all five
  runtime lanes passed, DX12 again reported zero errors and accepted committed
  images, physics retained hash `0x953D97A226665242`, and the 44,401-line CSV
  matched byte-exactly. No baseline, golden, screenshot, or replay artifact was
  refreshed.

## RR2 Replay Grade Ownership Evidence (2026-07-22)

- `ReplayPresentation` now owns `m_consequenceGradeStrength` and its
  `steady_clock` anchor. `AdvanceConsequenceGrade` preserves the original
  clamped 0.10-second delta and one-second approach policy.
- `ReplayRuntime::AdvanceConsequenceGrade` is the subsystem domain command;
  `RunRender` invokes it immediately before world entry and preserves the old
  text-only pause. `FrameEntryContext` crosses one copied `[0,1]` strength.
- RuntimeRenderer retains only render policy: clamp the copied scalar, apply the
  existing cinematic grade values, and schedule cinematic rendering while the
  strength is visible. It has zero consequence-grade data members or clocks;
  its provisional member denominator is 42 before RR4's authoritative recount.
- No replay store, growth registration, phase gate, cap, high-water counter, or
  reserve inventory changed. No downward Replay include, context/callback pack,
  broad owner borrow, compatibility spelling, or heap/growth call appeared.
- Touched-source comment audit: `ReplayPresentation.h/.cpp`,
  `ReplayRuntime.h/.cpp`, `RunRender.cpp`, and `RuntimeRenderer.h/.cpp` checked
  7/7; this touched-file pass needs no checklist path, with zero deferred or
  unchecked files and no wording awaiting owner approval.
- Focused Profile build passed in 11.2 s. The focused replay doctest selection
  passed 52/52 cases and 786 assertions in 2.1 s.
- `tools\validate_dx12_renderer.bat` passed in 56.6 s with zero InfoQueue
  errors and unchanged accepted images. `tools\run_graphics_stress.bat 1`
  completed crash-free in 61.1 s and stopped only by PID 61936's timeout.
- The one permitted `tools\validate_replay_visual_fidelity.bat` invocation
  passed in 445.6 s: launcher shape proved one engine process and one prediction
  generation; the authoritative 2,401-tick run, causal reveal/durable artifact,
  and every negative/false-pass control passed.
- Final `tools\validate_full.bat` passed in 108.0 s: CPU/coverage and all five
  runtime lanes passed, DX12 again reported zero errors and accepted committed
  images, physics retained hash `0x953D97A226665242`, and the 44,401-line CSV
  matched byte-exactly. No baseline, golden, screenshot, replay artifact, or
  provenance metadata changed.

## RR3 Resource-Lifecycle Evidence (2026-07-22)

- `RenderResourceLifecycle` is the concrete backend-epoch owner under
  `Runtime/Render/`. It owns the established `RuntimeRenderBackendView`,
  lifecycle log, texture collection, skybox, pass-resource aggregate,
  primitive-batch cache, GPU timing owner, and `UiTextPass`; its asset, terrain,
  and config references are the long-lived owners required to rebuild those
  resources. Renderer-only accessors are private and granted only to
  `RuntimeRenderer`, so external lifecycle consumers cannot acquire raw backend
  authority through the new surface.
- Process initialization, UI-text resource setup, scene DXR warmup, render-target
  preview projection, and lifecycle-owned release commands moved into the new
  owner. `RuntimeRenderer` retains the ordered cross-owner drain/release recipe
  because pass consumers must still release between lifecycle phases; it does
  not duplicate the moved resource state or call back into `Run`.
- The `RuntimeRenderer` constructor fell from 10 parameters to three: the
  established backend owner view, the established world owner view, and scene
  state used by lifecycle diagnostics. Its provisional member count is 26
  before RR4's authoritative recount, down from the RR0 baseline of 46.
- The existing cold skybox and primitive-batch allocation-policy rows moved to
  the owning implementation without changing phase, cap, or exception count.
  The allocation self-test/repository scan passed in 9.3 s over 427 files with
  30 direct-heap, 129 dynamic-member, and 646 growth findings all accounted for
  and zero allowlist errors.
- Touched-source comment audit: `RenderResourceLifecycle.h/.cpp`,
  `RuntimeRenderer.h/.cpp`, `Run.cpp`, `RunRender.cpp`, `RunScene.cpp`, and
  `OperatorEditorFrameComposer.cpp` checked 8/8; this touched-file pass needs no
  checklist path, with zero deferred/unchecked files. The one-line project
  filter-prefix registration is a trivial metadata edit, not a substantial
  tool-script body change.
- The final focused Profile build passed in 17.1 s with zero warnings/errors.
  `tools\validate_fast.bat` passed in 58.2 s after the repository formatter and
  project-filter metadata were reconciled.
- `tools\validate_dx12_renderer.bat` passed in 24.7 s with zero InfoQueue
  errors and all three committed images accepted. `tools\run_graphics_stress.bat 1`
  completed crash-free in 60.9 s and stopped only by PID 47480's scoped timeout.
- Final `tools\validate_full.bat` passed in 121.2 s: CPU/coverage and all five
  runtime lanes passed, DX12 again reported zero errors and accepted committed
  images, physics retained hash `0x953D97A226665242`, and the 44,401-line CSV
  matched byte-exactly. No baseline, golden, screenshot, replay artifact, or
  provenance metadata changed.

## RR4 Authoritative Shrink Census (2026-07-22)

The authoritative post-extraction surface is `RuntimeRenderer.h` at 319 text
lines plus its 2,341-line implementation. CodeGraph was current, and every
count below was confirmed against the actual declarations and direct include
rows.

| Measure | RR0 | RR4 | Delta |
|---|---:|---:|---:|
| `RuntimeRenderer` data members | 46 | 26 | -20 (-43%) |
| Constructor parameters | 10 | 3 | -7 (-70%) |
| Ordinary methods with at least seven parameters | 2 | 0 | -2 (-100%) |
| Direct `RuntimeRenderer.h` includers | 8 | 8 | 0 |

The 26 members are one `RenderResourceLifecycle`; four world/policy borrows or
values (`m_cameras`, `m_window`, `m_presentationSettings`, `m_world`); four
debug/profiling borrows; one bounded DXR transform array; eleven cohesive pass
instances; and five one-live-graph scratch/state members. Every survivor has a
direct construction, frame, pass, policy, diagnostics, or teardown use; no dead
renderer member remains.

The widest surviving ordinary methods have arity three:
`BuildModelFrameView`, `BuildRenderFrameContext`, and
`FinalizeFrameGraphInternal`. Each takes one cohesive operation's values; there
is no surviving seven-or-more-argument method requiring an exception reason.
The constructor's three parameters are the established backend owner view,
world owner view, and scene state for lifecycle diagnostics.

Direct fan-in remains the same eight files recorded in RR0. `Run.h` owns the
renderer; input, command, and stress consumers apply renderer policy;
`RunScene.cpp` performs scene activation; `SceneRuntimeReset.cpp` and
`ReplayRestoreService.h` participate in typed restore transactions; and
`RuntimeRenderer.cpp` is the implementation. No new renderer includer was
introduced by the resource owner.

RR4 also narrowed the lifecycle owner's retained backend record from the full
process `RuntimeRenderBackendView` to seven exact backend-epoch capabilities:
frame, graph, resource creation, texture, geometry, diagnostics, and optional
ray tracing. Capture, shader-development, development-UI, and the device borrow
do not survive there; the device is borrowed only by the lifecycle log.

- Touched-source comment audit: `RenderResourceLifecycle.h/.cpp` and
  `RuntimeRenderer.cpp` checked 3/3; no checklist path was required and zero
  files were deferred or unchecked.
- Focused Profile build passed in 17.1 s with zero warnings/errors.
  `tools\validate_dx12_renderer.bat` passed in 39.8 s with zero InfoQueue
  errors and all three committed images accepted.
- `tools\run_graphics_stress.bat 1` completed crash-free in 60.9 s and stopped
  only by PID 46552's scoped timeout. Final `tools\validate_full.bat` passed in
  118.7 s with the CPU/coverage umbrella, all five runtime lanes, unchanged DX12
  images, physics hash `0x953D97A226665242`, and the 44,401-line CSV byte-exact.
  No baseline, golden, screenshot, replay artifact, or provenance metadata
  changed.

## Phases

- [x] RR0 — Baseline census. Record member inventory (count and domain),
  constructor arity, the ≥7-argument method inventory, and include fan-in
  for `RuntimeRenderer.h` at the starting tip. These numbers are the
  acceptance denominators.
- [x] RR1 — UI text composition extraction. Move `RenderUiText` /
  `ExecuteUiTextThroughRenderGraph` composition (HUD, operator UI text,
  replay HUD status assembly) into a cohesive UI-text pass owner that
  receives one named per-frame value record; `RuntimeRenderer` retains only
  the graph scheduling call. Target: no UI-text path signature above six
  parameters.
- [x] RR2 — Replay presentation grading relocation. Move consequence-grade
  fade state and its wall-clock animation to the replay presentation
  boundary (`ReplayPresentation` domain), crossing into the renderer as a
  per-frame value in the existing frame-entry record. Replay-facing task:
  runs the rule-11 mega gate.
- [x] RR3 — Resource-lifecycle seam. Separate process/scene resource
  lifecycle (`InitialiseProcessResources`, `EnsureUiTextResources`,
  ray-tracing warmup, release paths) from frame orchestration behind a
  cohesive resource-lifecycle surface on the renderer, shrinking the
  constructor toward owner views already established by the frame views.
  Preview-snapshot projection moves with whichever seam RR0 shows it
  belongs to.
- [x] RR4 — Member and signature shrink. With RR1-RR3 landed, delete
  now-unneeded members/borrows, re-count the RR0 inventory, and record the
  deltas. Any surviving ≥7-argument method gets an individual recorded
  reason (mirroring the wide-call inventory discipline).
- [ ] RR5 — Closure. Independent ownership review over the logical renderer
  surface (`RuntimeRenderer.*`, `RuntimeRenderPasses.*`, extracted owners,
  `RunRender.cpp`) confirming no forwarding facade, no context bag, no
  authority reach-back, and honest RR0-vs-RR4 deltas. Final gates below.

## Dependencies And Decisions

- Third in the round-2 campaign binding order; rebases on
  `run-execute-frame-phase-decomposition` RX1 for `RunRender.cpp` overlap.
- Owner decision ratified at registration: extraction owners live under
  `Runtime/Render/`; replay grading state belongs to the Replay presentation
  domain, not the renderer.
- RR2 must not refresh the replay golden manifest (inventory rule 11;
  refresh requires explicit owner approval).

## Acceptance

- RR4 records material reductions against RR0 in member count, constructor
  arity, and widest-signature inventory, with per-survivor reasons.
- Replay types no longer appear in UI-text signatures; renderer holds no
  replay animation state.
- Zero DX12 validation errors; unchanged committed screenshots; crash-free
  bounded stress per DX12 rule on every DX12-touching task.
- Independent review is clear of god-object, bag, and forwarding findings.

## Validation

- Every DX12-touching task pre-commit: `tools\validate_dx12_renderer.bat`
  plus `tools\run_graphics_stress.bat 1` with recorded command, runtime, and
  exit evidence (inventory rule 10).
- RR2 additionally runs `tools\validate_replay_visual_fidelity.bat` — one
  invocation, one engine process, one prediction generation (rule 11).
- RR5 closure: `tools\validate_full.bat` plus the three-repeat
  `validate_dx12_renderer` discipline used by prior renderer closures.
- Zero behavioral baseline, golden, or screenshot refresh anywhere in the
  plan.
