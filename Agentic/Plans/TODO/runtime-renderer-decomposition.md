# RuntimeRenderer Decomposition

Date: 2026-07-22
Owner: skullbonez
State: Registered, not started
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

## Phases

- [ ] RR0 — Baseline census. Record member inventory (count and domain),
  constructor arity, the ≥7-argument method inventory, and include fan-in
  for `RuntimeRenderer.h` at the starting tip. These numbers are the
  acceptance denominators.
- [ ] RR1 — UI text composition extraction. Move `RenderUiText` /
  `ExecuteUiTextThroughRenderGraph` composition (HUD, operator UI text,
  replay HUD status assembly) into a cohesive UI-text pass owner that
  receives one named per-frame value record; `RuntimeRenderer` retains only
  the graph scheduling call. Target: no UI-text path signature above six
  parameters.
- [ ] RR2 — Replay presentation grading relocation. Move consequence-grade
  fade state and its wall-clock animation to the replay presentation
  boundary (`ReplayPresentation` domain), crossing into the renderer as a
  per-frame value in the existing frame-entry record. Replay-facing task:
  runs the rule-11 mega gate.
- [ ] RR3 — Resource-lifecycle seam. Separate process/scene resource
  lifecycle (`InitialiseProcessResources`, `EnsureUiTextResources`,
  ray-tracing warmup, release paths) from frame orchestration behind a
  cohesive resource-lifecycle surface on the renderer, shrinking the
  constructor toward owner views already established by the frame views.
  Preview-snapshot projection moves with whichever seam RR0 shows it
  belongs to.
- [ ] RR4 — Member and signature shrink. With RR1-RR3 landed, delete
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
