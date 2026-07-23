# Wide Signature Decomposition Round 2

Date: 2026-07-23
Owner: skullbonez
State: In progress
Ledger tasks: 5 (D0-D4)

## Problem And New Evidence

The first `wide-signature-reduction` campaign correctly rejected broad context
bags, but its accepted-with-reason rulings left eight functions at 16 or more
parameters. The owner's 2026-07-23 follow-up explicitly reopens those eight
rows under a stronger question: can responsibility or value lifetime be made
smaller without hiding mutable owners?

The repeatable threshold-16 scan identifies:

| Arity | Function | Area |
|---:|---|---|
| 24 | `ReplayAuthoring::TickCauseTreeInput` | Replay input |
| 24 | `ReplayAuthoring::TickVelocityEditInput` | Replay input |
| 22 | `UpdateReplayPrediction` | Replay prediction |
| 21 | `ReplayRuntime::TickScrubberInput` | Replay input |
| 19 | `ReplayPrediction::UpdateFrame` | Replay prediction |
| 17 | `ReplayPresentationOperations::ActivateLoadedPresentation` | Replay activation |
| 16 | `SceneTab::HandleContentClick` | UI input |
| 16 | `ApplySceneLoadConsumerOutputs` | Scene/runtime composition |

CodeGraph caller/source review supplies new evidence beyond the W1 rulings:

- three Replay input functions unpack the existing owner-free
  `ReplayWorkspaceFrameInput` and forward its fields again;
- the prediction 22/19 pair is a one-caller forwarding chain whose non-owner
  inputs already form one per-frame request lifetime;
- the scene-load function has an existing ordering boundary between runtime
  owner reactions and external window/UI/validation presentation;
- the UI handler contains three independently ordered hit-test actions; and
- loaded-presentation activation can group scalar activation policy without
  grouping or retaining any mutable owner.

## Goal

Remove every current 16-or-more-parameter function through responsibility
decomposition or bounded domain values. The final tracked-source scan must
contain zero functions at threshold 16 while keeping mutable owners explicit
and preserving behavior, Replay allocation policy, deterministic artifacts,
and dependency direction.

## Non-Goals

- No generic parameter object, context, services, bindings, callback pack, or
  stored host reference.
- No aggregate may retain mutable subsystem owners.
- No behavioral baseline, golden, screenshot, Replay artifact, physics CSV,
  schema, configuration, or reserve-policy refresh.
- No attempt to reduce every threshold-7 survivor; this owner-requested pass is
  bounded to the eight threshold-16 rows and any direct replacement shape.

## Phases

- [x] D0 — Reopen and design. Reproduce the eight-row inventory, inspect every
  definition and caller through CodeGraph/current source, and ratify the
  owner-safe decomposition above.
- [x] D1 — UI and scene composition. Split the Scene-tab handler by action and
  split scene-load runtime reactions from external presentation outputs. Prove
  both original 16-parameter rows are absent.
- [ ] D2 — Replay input and activation. Reuse `ReplayWorkspaceFrameInput`, add
  only bounded read-only cause/velocity source views and scalar activation
  policy, and keep mutable Replay/camera/input owners explicit.
- [ ] D3 — Replay prediction. Replace the 22/19 forwarding chain with one
  value-only per-frame request and remove the redundant helper authority.
- [ ] D4 — Closure. Run the threshold-16 scan, touched-file comment audit,
  dependency/Replay-boundary proofs, one independent no-bag review, and all
  required final gates. Record and archive closure evidence.

## Acceptance

- `python tools\inventory_wide_signatures.py --threshold 16 --format json`
  returns zero rows from final tracked source.
- Every new value/view has a single synchronous producer/consumer lifetime and
  contains no mutable owner reference.
- No replacement forwarding facade retains the same authority under a new
  spelling.
- The touched-file comment audit is complete with zero silent deferrals.
- `tools\validate_full.bat` and the mapped Replay gates pass without artifact
  refresh; allocation and downward-Replay-include proofs remain clean.

## Validation

- D1-D3 iteration: focused Profile build only when needed to answer a compile
  question; repository validation remains deferred.
- D4: `tools\validate_full.bat`,
  `tools\validate_replay_allocation_policy.bat`,
  `tools\validate_replay_v2_artifact.bat`,
  `tools\validate_replay_visual_fidelity.bat`, and
  `tools\validate_replay_scrub.bat`.

## Evidence

- D1 threshold-16 scan: 8 → 6 rows; both UI/scene targets absent.
- D1 touched-file comment audit: 9/9 checked, 0 deferred.
- D1 focused Profile build: PASS in 11.5 s with zero warnings/errors after
  removing one newly exposed unused combo-result parameter.
- D1 `tools\validate_full.bat`: PASS in 135.1 s; CPU/coverage umbrella and
  five runtime lanes pass, zero DX12 errors, accepted screenshots, and the
  44,401-line physics CSV is byte-exact.
