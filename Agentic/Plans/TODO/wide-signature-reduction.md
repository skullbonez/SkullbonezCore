# Wide Signature Reduction

Date: 2026-07-22
Owner: skullbonez
State: Registered, not started
Ledger tasks: 5 (W0-W4)

## Problem And Evidence (2026-07-22, main tip 0c5263e1)

The engine's no-context-bag policy pushed coupling into signatures instead of
removing it. Spot evidence at registration (the full inventory is W0's job,
deliberately not performed at plan-writing time):

- `ApplySceneLoadConsumerOutputs` in
  `SkullbonezSource/Runtime/Scene/SceneController.h` takes 15 parameters.
- `RuntimeRenderer::RenderUiText` takes 11;
  `ExecuteUiTextThroughRenderGraph` takes 13 (owned by
  `runtime-renderer-decomposition` RR1, excluded here to avoid double
  ownership).
- `RuntimeRenderer`'s constructor takes 9; `PhysicsEngine::Step` takes 6-7
  across overloads.

Prior related closures this plan must reconcile with rather than repeat:

- `wide-call-desc-struct-pass` (2026-07-15): converted ≥12-argument calls;
  31 surviving ≥12-arg names carry individual recorded reasons; 7-11-arg
  rows kept their existing dispositions by owner ruling.
- `runtime-signature-decomposition` (round 4): eleven owner-plumbing calls
  moved to stack-only frame views at 4-6 arguments.
- `owner-fanout-reduction` (2026-07-22): scene-load inputs 18 → 10,
  consumer outputs 20 → 11, view-parameter slots 68 → 36.

The tree has changed materially since the 2026-07-15 inventory (five
campaigns closed), and the old ≥12 threshold left the 7-11 band unruled.
This plan sets the stage for a fresh deep dive; it does not pre-judge which
signatures are defects.

## Goal

A current-tip, tool-generated wide-signature inventory with a ratified
classification rubric; owner rulings for every row; and refactor waves that
implement only the rows ruled as defects — using named domain value records,
never a context bag. Rows ruled load-bearing get recorded reasons that
future reviews cite instead of re-litigating.

## Non-Goals

- No context objects, service bags, callback packs, or `*Context` catch-alls
  (standing rules apply verbatim). The remedy for a wide signature is a
  named domain value record or a genuine ownership fix, or an accepted
  reason.
- No behavior change anywhere in the plan; all refactors are call-shape
  moves proven by unchanged gates.
- No re-opening of the 31 individually reasoned ≥12-arg survivors without
  new evidence; the fresh inventory noting a survivor unchanged is not new
  evidence by itself.
- UI-text signatures owned by `runtime-renderer-decomposition` RR1 are out
  of scope here.

## Phases

- [ ] W0 — Inventory (the deep dive). Build or adapt a repeatable sweep
  (script under `tools/`, or a recorded rg/clang-query method) that lists
  every function/method/constructor in `SkullbonezSource/` with ≥7
  parameters: signature, file:line, arity, parameter kinds (owner borrows vs
  values vs flags), call-site count, and any prior-inventory disposition.
  Committed as a dated table in this plan. Documentation/tooling task.
- [ ] W1 — Rubric and rulings. Ratify the classification rubric
  (transaction boundary with intentional explicit width; missing domain
  value record; flag-soup needing an enum/policy value; genuine ownership
  smell routed to an owning plan; accepted-with-reason) and apply it with
  the owner to every W0 row. Rows routed to other plans are cross-linked,
  not duplicated. Documentation task.
- [ ] W2 — Refactor wave A. Implement the highest-value dedup of ruled
  defect rows outside render/replay-gated files (scene, runtime, physics
  cold paths), one logical signature family per commit, each with its
  mapped gate.
- [ ] W3 — Refactor wave B. Implement the remaining ruled defect rows,
  including any render- or replay-gated files with their mandatory extra
  gates (DX12 stress per rule 10; replay mega gate per rule 11 where
  replay-facing).
- [ ] W4 — Closure. Re-run the W0 sweep from final source; every remaining
  ≥7-arg row must be ruled (accepted reason or cross-linked plan). One
  independent review over the inventory honesty and the no-bag rule.
  Final broad gate.

## Dependencies And Decisions

- Fifth (last) in the round-2 campaign binding order: the inventory must
  measure the tree after the physics, frame-phase, renderer, and replay
  plans reshape it, or W0's rows go stale immediately.
- Owner decision ratified at registration: threshold is ≥7 parameters for
  inventory (rulings may still accept width); the 2026-07-15 ≥12 survivors'
  reasons carry forward unless W0 surfaces new evidence.
- W2/W3 wave contents are populated from W1 rulings by design; the ledger
  task count stays fixed at 5 regardless of how many rows each wave carries.

## Acceptance

- W0 sweep is repeatable (recorded command produces the committed table).
- Every row at W4 has exactly one disposition: refactored, accepted with
  reason, or cross-linked to an owning plan.
- Zero new context bags; every introduced record is a named domain value
  with a single writer.
- All mapped gates pass with zero behavioral baseline, golden, screenshot,
  or replay refresh.

## Validation

- W0/W1: documentation/tooling only. If W0 lands a script under `tools/`,
  run `tools\validate_fast.bat` plus the script itself per the tools
  mapping; a pure recorded-command method needs no validation.
- W2/W3: the smallest mapped gate per touched file family, cumulative when
  rows span areas; DX12-touching commits add
  `tools\validate_dx12_renderer.bat` + `tools\run_graphics_stress.bat 1`;
  replay-facing commits run the rule-11 mega gate once per task.
- W4 closure: `tools\validate_full.bat` from final source.
