# Runtime Frame View Retirement

Date: 2026-07-26
Status: IN PROGRESS — FV0 closed on 2026-07-27 with the current 12-phase,
six-helper, and 21-consumer census; FV1 is binding. Drafted from the 2026-07-26
from-source architecture review of `nightrunner-26th-JUL-26` at tip `35f6de4e`. Registered in
`MASTER-PLAN.md` on 2026-07-26 as plan 5 of the Architecture Follow-Up Campaign
Round 5. 1/4 phases complete.
Impact area: `Runtime/RuntimeFrameViews.h`, `Runtime/App/Run.h`,
`Runtime/App/RunFrame.cpp`, `Runtime/App/InputFrameExecution.cpp`,
`Runtime/Capture/RuntimeStressController.cpp`
Owner: runtime
Priority: Medium — the highest-risk plan in the campaign and the one with the
clearest evidence, because the file states the invariant it breaks.

## Problem And Evidence (measured 2026-07-26)

`Runtime/RuntimeFrameViews.h:24` declares the invariant:

> No capability slice spans the complete frame surface; helpers receive only the
> slices required for their operation.

The same file then declares four non-copyable reference-carrying views totalling
23 references:

| View | References | Line |
|---|---:|---|
| `RuntimeFrameHostView` | 6 | `:84` |
| `RuntimeFrameInteractionView` | 6 | `:113` |
| `RuntimeFrameSceneView` | 7 | `:139` |
| `RuntimeFramePresentationView` | 4 | `:167` |

Those 23 references are `Run`'s member list. Two operations receive **all four**:

- `Run::RunInputPhase( host, interaction, scene, presentation, automation )` —
  declared `Runtime/App/Run.h:192`, defined `Runtime/App/RunFrame.cpp:380`.
- `Run::RenderOperatorUiPhase( host, interaction, scene, presentation, models,
  facts )` — declared `Run.h:208`, defined `RunFrame.cpp:569`.

A function holding all four slices has the authority of `Run&`. The split is
nominal, and the header's stated invariant is false as written.

Worse, the convention is not load-bearing. `Run::TickPhysics`
(`RunFrame.cpp:877`) does the single most consequential piece of frame work and
uses no view at all — it reaches `m_simulation`, `m_replayRuntime`,
`m_inputRouter`, `m_diagnosticsRuntime`, `m_runtimeTools`, `m_interaction`,
`m_sceneController`, and `m_config` directly as members. So the frame loop has
two calling conventions: views for some phases, direct member access for others,
with no rule distinguishing them. `Run::Execute` (`RunFrame.cpp:790`) builds all
four views every frame at `:825-828` regardless of which phases will use them.

The four sibling `Frame*PhaseResult` value structs (`Run.h:176-179`) plus four
sibling `Build*View` methods (`:183-186`) are, together, the extrusion signal
`AGENTS.md` defines: three or more sibling participant structs plus wide
operations plus ordering comments, with no type owning the frame-phase ordering
invariant. `run-execute-frame-phase-decomposition` (RX0-RX3, closed 2026-07-22)
produced the 74-line phase schedule, which is genuinely good; it did not resolve
who owns the ordering rule.

## Goal

One calling convention for the frame turn: every phase receives the concrete
owners it uses. No partition of `Run`'s members is handed around as four structs,
and no phase bypasses the convention. A phase's signature is the honest statement
of what that phase can affect.

## Non-Goals

- No regression of `run-execute-frame-phase-decomposition`. `Run::Execute` stays
  a short, readable phase schedule; this plan may not re-inline phase bodies back
  into it.
- No behavior change. Frame order, restart edges (`TickScreenshots` continue,
  `CompleteFramePhase` continue), allocation-phase scopes, and profiler marker
  boundaries are preserved exactly.
- No replacement service bag, callback pack, or `Run&` parameter. Passing `Run&`
  to a phase is an explicit closure failure, not a simplification.
- No scope on `RuntimeRenderBackendView`, which
  `render-backend-service-bag-removal` owns even though
  `RuntimeFramePresentationView:171` carries it. FV2 must sequence after that
  plan's removal or explicitly leave that one member in place for it.
- No new inheritance or virtual dispatch to express phases.

## Phases

- [x] **FV0 — Census the frame surface against the ratified endpoint.**
  **Owner ruling 2026-07-27: endpoint (a), concrete operands.** Delete all four
  views; each phase takes only the owners it uses. The rejected alternative was a
  frame-turn phase-cursor transaction. The owner's reason, recorded so a later
  reader does not relitigate it: a phase cursor earns its keep when many call
  sites re-encode the order — `SceneLoadTransaction` (GV2) had four — but
  `Run::Execute` is a single call site and already a short linear schedule, so
  the order is enforced by construction and the steps cannot be called out of
  turn. The defect here is only the fake capability split, so a transaction would
  add machinery to enforce something the schedule already guarantees. Do not
  introduce a frame transaction under this plan.

  Remaining FV0 work is measurement, not decision. For each of the twelve `Run`
  frame phases record exactly which of the 23 references it reads or writes, which
  reach members directly instead, and the resulting parameter count per phase.
  Acceptance: the census shows per-phase reference usage and post-removal
  parameter count; every phase is at or below the 12-parameter ceiling, or the
  over-ceiling phase is named with the decomposition that brings it under. A long
  but honest argument list is the accepted outcome here — reintroducing an
  aggregate to shorten one is a closure failure under the Invariant Ownership
  Rule.
  Closed 2026-07-27. The current surface is 23 required owner references plus
  one optional shader capability. FV0 measured all twelve top-level phase
  helpers, all six plan-named direct helpers, and the complete 21-consumer view
  blast radius. Five wide top-level rows and five wide direct helpers have
  named decompositions at or below the 12-parameter ceiling. Evidence:
  `../../Reports/2026-07-27/runtime-frame-view-retirement-fv0-census.md`.

- [ ] **FV1 — Make the convention uniform.**
  Apply concrete operands to `TickPhysics`, `UpdateLogic`,
  `AfterPhysicsStep`, `TickScreenshots`, `TickAutoCycle`, and `TickSceneAdvance`
  first — the phases that currently bypass the views. This is deliberately the
  first implementation step: if concrete operands cannot express the heaviest
  phase within the ceiling, that is discovered before the other ten phases are
  converted, and the decomposition it needs is recorded rather than worked around
  with a new aggregate. Acceptance: no frame phase mixes view
  access with direct member access; `Run::Execute` remains a phase schedule;
  physics CSV byte-exact; frame order and marker boundaries unchanged.

- [ ] **FV2 — Convert the remaining phases and delete the views.**
  Apply concrete operands to `RunInputPhase`, `RunSimulationPhase`,
  `PrepareRenderPhase`, `PublishRenderModelsPhase`, `RenderWorldPhase`,
  `RenderOperatorUiPhase`, `RunPostDrawDiagnosticsPhase`, `FinishFrameWorkPhase`,
  `PresentFramePhase`, and `CompleteFramePhase`, plus the two external consumers
  named by `RuntimeFrameViews.h:30-31`
  (`InputFrameExecution.cpp`, `RuntimeStressController.cpp`). Those two files are
  also the campaign's two largest extraction-scar sites (25 and 14 findings), and
  they destructure the views straight back into `m_`-named locals — so converting
  them is where the views' real purpose becomes visible. Coordinate with
  `extraction-scar-remediation` ES0 rather than renaming the same locals twice.
  `RuntimeFrameViews.h:24`'s invariant text is deleted with the views; under the
  ratified endpoint no view survives to restate it. Acceptance:
  `rg -n 'RuntimeFrame(Host|Interaction|Scene|Presentation)View' SkullbonezSource SkullbonezTests`
  returns no rows; `RuntimeFrameViews.h` is deleted or contains only
  `RuntimeUiTextFrameFacts` and forward declarations; DX12 baselines unchanged;
  capture restart and stress paths behave identically.

- [ ] **FV3 — Reconcile, review, and hand off.**
  Complete the comment audit for every touched file, with particular attention to
  `Run.h`'s Mental model block and `RuntimeFrameViews.h` — both currently describe
  the retired convention. Obtain one independent ownership review answering: can
  any single operation still reach the whole frame surface, does any phase retain
  an owner beyond its call, and did the views return under another name.
  Acceptance: review clear; `validate_full.bat`, three consecutive
  `validate_dx12_renderer.bat` runs, `run_graphics_stress.bat 1`,
  `validate_physics.bat`, and `validate_replay_visual_fidelity.bat` pass with no
  baseline, golden, or config change.

## Dependencies And Decisions

- Depends on `governance-shape-to-judgment-conversion` G1, specifically the new
  Capability Slice Ownership rule, which is this plan's acceptance test.
- Sequence after `render-backend-service-bag-removal` so FV2 does not have to
  preserve `RuntimeFramePresentationView::renderBackendView`. If the owner
  reorders, FV0 must record the compensating decision.
- **Ratified 2026-07-27: endpoint (a), concrete operands.** No open decision
  remains in this plan. The frame-turn transaction alternative is rejected for the
  reason recorded in FV0 and must not be reintroduced by a reviewer or a later
  phase.

## Acceptance

- No frame operation can reach the complete frame surface.
- One calling convention across all twelve phases; `TickPhysics` no longer an
  exception.
- `RuntimeFrameViews.h` contains no false invariant.
- Zero behavior change: physics byte-exact, DX12 baselines unchanged, replay
  fidelity frame-exact.

## Validation

Per the File To Validation Mapping, `Run*` and `Runtime/*` require
`validate_full`; the render and physics paths add their own gates:

- `tools\validate_full.bat`
- `tools\validate_dx12_renderer.bat` run three consecutive times (upload-buffer
  and frame-allocator danger zone), then `tools\run_graphics_stress.bat 1`
- `tools\validate_physics.bat`
- `tools\validate_replay_visual_fidelity.bat` — one engine process, one
  prediction generation, no golden refresh
- `tools\validate_perf.bat` — the per-frame view construction at
  `RunFrame.cpp:825-828` is removed or changed
