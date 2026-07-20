# Render Graph Completion

Status: Registered — 0/6 tasks (G0-G5)
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
- No pass reordering: the stable sky → shadows → reflection → scene target →
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

## Tasks

- [ ] G0 — Authority inventory and migration map: for every pass, record
  execution owner (direct vs callback), every hand-written barrier the
  backend emits on its behalf, and the resource class each barrier belongs
  to; define the class-by-class migration order and the expected exception
  list. Output: migration table committed into this plan. No validation
  (documentation).
- [ ] G1 — Transient resource class: graph-compiled transitions become the
  live barrier path for graph-managed transients (volumetric, tonemap
  inputs); delete the corresponding hand-written transitions. Typed input
  structs for the touched wrappers. Validation:
  `tools\validate_dx12_renderer.bat` ×3 consecutive +
  `tools\run_graphics_stress.bat 1`.
- [ ] G2 — Producer target class: shadow, reflection, scene/cinematic
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
