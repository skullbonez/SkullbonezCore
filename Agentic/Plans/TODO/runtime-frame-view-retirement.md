# Runtime Frame View Retirement

Date: 2026-07-26
Status: NOT STARTED — drafted from the 2026-07-26 from-source architecture
review of `nightrunner-26th-JUL-26` at tip `35f6de4e`. Registered in
`MASTER-PLAN.md` on 2026-07-26 as plan 5 of the Architecture Follow-Up Campaign
Round 5. 0/4 phases complete.
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

One calling convention for the frame turn. Either a phase receives the concrete
owners it uses, or the frame turn has a real owner that enforces phase order —
not a partition of `Run`'s members handed around as four structs while the
heaviest phase bypasses them.

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

- [ ] **FV0 — Census the frame surface and choose the endpoint.**
  For each of the twelve `Run` frame phases, record exactly which of the 23
  references it actually reads or writes, and which reach members directly
  instead. Produce the two candidate endpoints with evidence, and rule between
  them:
  - **(a) Concrete operands.** Delete all four views; each phase takes the owners
    it uses. FV0 must report, per phase, the resulting parameter count so ceiling
    breaches are known up front.
  - **(b) One frame-turn invariant owner.** A non-copyable phase-cursor type owns
    the legal walk (Input -> Simulation -> PrepareRender -> PublishModels ->
    RenderWorld -> OperatorUi -> PostDraw -> Screenshots -> FinishWork ->
    Present -> Complete), makes an out-of-order call lane-F fatal, stores values
    and a cursor only, and never retains an owner across phase calls — the shape
    GV2/GV3 ratified for `SceneLoadTransaction` and
    `SceneGeneratedControlTransaction`.
  Acceptance: the census shows per-phase reference usage; the ruling names one
  endpoint with the reason; if (a) is chosen, no phase exceeds 12 parameters, and
  if (b) is chosen, the exact phase-order and arbitration invariant is written
  before implementation.

- [ ] **FV1 — Make the convention uniform.**
  Whichever endpoint FV0 rules, apply it to `TickPhysics`, `UpdateLogic`,
  `AfterPhysicsStep`, `TickScreenshots`, `TickAutoCycle`, and `TickSceneAdvance`
  first — the phases that currently bypass the views. This is deliberately the
  first implementation step: if the endpoint cannot express the heaviest phase, it
  is the wrong endpoint and FV0 reopens. Acceptance: no frame phase mixes view
  access with direct member access; `Run::Execute` remains a phase schedule;
  physics CSV byte-exact; frame order and marker boundaries unchanged.

- [ ] **FV2 — Convert the remaining phases and delete or justify the views.**
  Apply the endpoint to `RunInputPhase`, `RunSimulationPhase`,
  `PrepareRenderPhase`, `PublishRenderModelsPhase`, `RenderWorldPhase`,
  `RenderOperatorUiPhase`, `RunPostDrawDiagnosticsPhase`, `FinishFrameWorkPhase`,
  `PresentFramePhase`, and `CompleteFramePhase`, plus the two external consumers
  named by `RuntimeFrameViews.h:30-31`
  (`InputFrameExecution.cpp`, `RuntimeStressController.cpp`). Any surviving view
  must state the owned invariant in its header and be exercised by a focused
  test; `RuntimeFrameViews.h:24`'s invariant text is corrected or deleted — it may
  not remain false. Acceptance: no operation receives every slice;
  `rg -n 'RuntimeFrame(Host|Interaction|Scene|Presentation)View' SkullbonezSource`
  either returns no rows or only rows for a ruled invariant owner; DX12 baselines
  unchanged; capture restart and stress paths behave identically.

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
- Open decision for the owner, recorded not assumed: endpoint (a) or (b). FV0
  produces the evidence and a recommendation; the owner may rule directly. The
  reviewer's judgement alone is not sufficient here because the choice changes the
  frame loop's calling convention permanently.

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
