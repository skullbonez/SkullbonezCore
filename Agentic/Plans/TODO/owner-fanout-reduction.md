# Owner Fan-Out Reduction — Scene Lifecycle And Frame-View Decoupling

Date: 2026-07-22
Status: Registered — 0/6 phases complete
Impact area: Runtime shell (`Run*`, `RuntimeFrameViews.h`), scene system
(`SceneController`, `SceneRuntime*`), reactive owner frame entries
Owner: runtime shell / scene lifecycle

## Problem And Evidence (2026-07-22 census)

The god objects are gone, but the god *graph* remains. Coupling was made
typed and legible without being reduced:

- `SceneController::Load`/`ExecutePending` borrow four participant structs
  carrying 18 concrete owners (the header's own comment: "6 policy, 3 host,
  5 interaction, and 4 presentation"), plus `SceneLoadConsumerOutputs`
  applied afterward at four more excluded owners (`Window`, `InGameUI`,
  `RuntimeValidationHarness`, launch options). A scene transition touches
  ~22 owners.
- `RuntimeFrameViews.h` defines four per-frame borrow structs plus
  `RuntimeUiTextFrameFacts`, each with hand-written constructors and deleted
  copies re-listing every field three times.
- Integrating one new subsystem owner currently touches `Run.h`, the `Run`
  constructor, one or more view structs, one or more `SceneLoad*` structs,
  and every construction call site — five-plus files for an owner that only
  needs to *react* to a scene load, not participate in it.

Most of the 18 load participants do not transact during the load. They are
reset/notified consumers: timers, overlays, camera state, attached camera,
interaction workspace, tools, replay presentation. They sit in the borrow
graph only so the load can call their reset entry points.

## Goal

Reactive owners leave the scene-load borrow graph entirely. Scene loading
borrows only owners that transact during the load (config, launch policy,
assets, worker pool, scene world/physics, renderer resource rebuild).
Reactive owners observe a scene-lifecycle *value ledger* at their own frame
entry points and self-reset. Integration cost for a new reactive owner is
documented and bounded at three files or fewer.

## Mechanism (house-rule compliant)

Extend the existing
`SceneController::RecordLifecycleEvent( SceneRuntimeLifecycleEvent,
SceneLifecycleConsumerMask )` seam into the single decoupling instrument:

- SceneController owns a monotonically increasing scene generation stamp
  plus a small value packet per lifecycle event (kind, preserve flags,
  generation).
- Each reactive owner stores its last-seen generation and performs its own
  reset at its established frame entry point when the generation advances.
- No callbacks, no subscriber list, no `std::function`, no allocation, no
  owner pointers crossing the seam. Application points are fixed positions
  in the existing frame order, so behavior stays deterministic.

This is the same pattern the repository already trusts at the
`OperatorEditorExchange` and `SceneLoadConsumerOutputs` boundaries, promoted
from special case to convention.

## Non-Goals

- No ECS, job-system, or component storage work (separately planned).
- No dynamic event bus, observer registry, or callback pack (banned shapes).
- No change to physics step order, replay recording semantics of scene
  requests, or the scene request ring.
- No change to which owner *owns* any state; only to who is present in the
  load/frame borrow graphs.
- `SceneFrameProceedPolicy` sampling and the one-sample-per-frame invariant
  are untouched.

## Phases

- [ ] OF0. Census and classification. Enumerate every owner reachable from
  `Load`, `ExecutePending`, `ApplySceneLoadConsumerOutputs`, and the four
  frame views. Classify each as transactional (mutates during the load
  transaction itself) or reactive (reset/notify only), with file:line
  evidence per owner. Record the target participant list and the target
  ledger packet fields. Documentation-only; no validation.
- [ ] OF1. Lifecycle ledger. Promote `RecordLifecycleEvent` into the
  generation-stamp value protocol. Migrate two pilot reactive owners
  (`RunTimerState` restart policy and `RuntimeOverlayDiagnostics`) out of
  the load participant structs onto ledger observation. Focused tests pin
  the reset-once-per-generation contract, including failed-load and
  same-count reset edges.
- [ ] OF2. Migrate the remaining reactive owners (camera state, attached
  camera, interaction workspace, runtime tools, replay presentation reset,
  input router context) onto the ledger. Shrink
  `SceneLoadInteractionParticipants` and
  `SceneLoadPresentationParticipants` to true transactional participants
  only. If this slice touches `Runtime/Replay/*`, that task additionally
  runs the replay visual-fidelity gate under inventory rule 11.
- [ ] OF3. Consumer-output collapse. Re-derive which
  `SceneLoadConsumerOutputs` fields the ledger now covers; retain only
  effects that genuinely cannot be self-served (window title, UI browser
  refresh are expected survivors). Delete the covered fields and their
  application code.
- [ ] OF4. Frame-view diet. For each helper consuming a frame view, list the
  fields it actually reads; delete unused fields, convert
  `RuntimeUiTextFrameFacts` borrowed members to value snapshots where the
  consumer copies anyway, and fold any view struct reduced to two or fewer
  members into direct parameters.
- [ ] OF5. Rerun the OF0 census from final source and publish the delta.
  Write the new-owner integration checklist (reactive owner: ≤3 files) into
  `Agentic/Reference/runtime-reference.md`. Independent rubber-duck
  ownership review of the ledger and surviving participant graph. Final
  broad gate.

## Dependencies And Decisions

- Builds on the 2026-07-11 binding ruling (no `SimulationController`, no
  unified `EntityId`); this plan does not revisit it.
- Sequenced after `render-interface-retirement` (mechanical, independent)
  and before `replay-subsystem-consolidation`, whose presentation-reset seam
  should land on the final ledger shape rather than migrate twice.
- Decision for OF0 to record: whether automation-build owners
  (`InteractionAutomationController`) join the ledger or remain explicit
  participants behind their macro.

## Acceptance

- Scene-load transactional participants: 18 owners → ≤10, each survivor
  justified in one sentence as transactional, recorded in the OF5 census.
- No new callback pack, forwarding facade, context bag, or `void*` appears
  (god-object rule 8 review shape).
- A reactive owner can be added by touching ≤3 files, demonstrated in the
  OF5 checklist with the pilot owners as evidence.
- All behavior gates pass byte-stable: physics CSV exact, DX12 baselines
  unchanged, replay untouched except where rule 11 was run and passed.

## Validation

Per the file-to-gate mapping, all tasks touch `Run*`/`Runtime/*`:
`tools\validate_full.bat` per PR-bound task. OF1/OF2 add their focused
lifecycle tests to the normal doctest lane first
(`tools\validate_tests.bat` during iteration is not required; the full gate
is the PR gate). Any task whose diff includes `Runtime/Replay/*` also runs
`tools\validate_replay_visual_fidelity.bat` exactly once (rule 11).
