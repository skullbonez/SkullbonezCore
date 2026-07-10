# Replay Architecture And Right-Sizing

Date: 2026-07-10
Status: In progress - 4/6 phases complete
Impact area: replay runtime, prediction, scrub/restore, replay UI, memory policy,
Run decomposition
Owner: replay subsystem

## Problem

R0 measured 28 replay source files / 23,814 lines. After R1, the current scope
is 24 files / 23,347 lines, with `RunReplayTools.cpp` still above 4,700 lines.
Replay is larger than the DX12 backend and
is the only subsystem allowed controlled runtime allocation growth. The prior
right-sizing work improved ribbon memory and removed one legacy fallback, but
the owning plan was deleted while review and feature plans still depended on
it.

The remaining cost is architectural:

- `Run` still owns replay input handlers, inspection-camera transitions,
  overlay emission, probes, restore coordination, and prediction presentation.
- `ReplayRuntime` is itself a 3,000+ line implementation and carries dense
  mutable UI/prediction/recording state.
- Replay code reaches through `GameModelCollection` for some presentation and
  restore paths.
- New fracture replay work would add another visual-sample family before the
  retained-sample and owner boundaries are stable.

## Goal

Replay has explicit owners for recording, restore, prediction, interaction,
and presentation. `Run` wires those owners and calls one replay frame API; it
does not implement replay business logic. Retained memory is measured and
bounded by purpose, and the replay allocation exception remains narrow.

## Phases

- [x] **R0 — Reconciled inventory.** Record every replay file, line count,
  owner/state category, public entry point, allocation owner, `Run::*` method,
  validation path, and deletion/merge candidate. Measure committed capacity,
  high-water use, and raw artifact size separately. Acceptance: inventory and
  source totals agree exactly.
  Evidence: `Agentic/Reports/replay_r0_inventory_20260710.md` reconciles all 28
  files / 23,814 lines, the 117-line outside probe-state adjunct, every Run
  replay business method, allocation owners/caps, configured capacity,
  observed high-water, raw artifact sizes, validation lanes, and R1/R2
  deletion candidates. The corrected v2 owner-action gate passed in 25.4s and
  the required tool-change fast gate passed in 19.4s.
- [x] **R1 — Delete and merge obsolete paths.** Remove duplicate presentation,
  legacy artifact, compatibility, and probe paths that no longer protect a
  supported format or workflow. Each retained legacy path needs an owner,
  supported input, and deletion condition. Acceptance: no behavior-free
  forwarding wrapper or unowned fallback remains.
  Evidence: `Agentic/Reports/replay_r1_cleanup_20260710.md` records deletion of
  the uncalled JSON exporter, two-file scrubber save forwarding bridge, two
  redundant CLI synonyms, deleted wire interpretation, and the untyped solver
  iteration callback. Binary v2 is the sole artifact format. The final fast,
  allocation, CPU, replay scrub, v2, interaction, and full gates passed.
- [x] **R2 — Move replay workspace behavior out of `Run`.** Move scrubber,
  cause-tree, velocity-edit, prediction-horizon, inspection-camera request,
  and overlay decisions behind replay-owned APIs. Input produces typed replay
  actions; replay returns commands and fixed-capacity draw records. Acceptance:
  `Run.h` exposes no `TickReplay*`, `RenderReplay*`, or replay camera-transition
  business method.
  Evidence: `Agentic/Reports/replay_r2_workspace_20260710.md` records the typed
  `TickWorkspace` boundary, replay-owned restore/reset/startup/probe transactions,
  fixed-capacity overlay production, zero replay business methods on `Run`, and
  zero Run pointer/backpointer/callback smuggling in replay owner headers. The
  CPU umbrella, allocation checker, replay scrub, v2 artifact, interaction,
  physics, DX12 renderer, and full gates passed from the final formatted source.
- [x] **R3 — Stabilise the retained-sample and memory model.** Name the durable
  recorded solver sample, presentation sample, prediction prefix, and artifact
  ownership rules. Right-size capacities from measured high-water evidence;
  preserve registered replay growth only where fixed preallocation is
  impractical. Acceptance: every replay growth owner has phase, hard cap,
  counter, and exhaustion behavior.
  Evidence: `Agentic/Reports/replay_r3_retained_memory_20260711.md` records the
  four durable data owners, sole presentation extension seam, three registered
  replay growth policies, evidence-backed 32/8/256 MiB caps, aggregate active
  byte enforcement, fixed-registry counters, diagnostics JSON, and explicit
  fatal-vs-cancel exhaustion rules. CPU, allocation, scrub/restore/prediction,
  v2, interaction, physics, perf, and full gates pass.
- [x] **R4 — Narrow live-owner access.** Replace replay traversal through
  `GameModelCollection` with body/collider/render/scene views and stable ids.
  Restore remains a command into physics/scene owners, not a write through a
  model compatibility surface. Acceptance: replay headers do not store a bare
  model index as persistent identity. Complete 2026-07-11: capture, workspace,
  picking, velocity edit, prediction, overlays, render overrides, and restore
  use explicit owners; retained rows are typed hints paired with `ReplayBodyId`;
  scene topology restore is coordinated by `SceneController`. CPU, scrub,
  interaction, physics, DX12, and full gates pass. Evidence:
  `Agentic/Reports/replay_r4_live_owner_identity_20260711.md`.
- [x] **R5 — Tests, size closure, and feature gate.** Extend scrub/restore,
  hash, branch, prediction, artifact, and allocation tests. Perform one final
  independent review. Acceptance: all replay tests run from the CPU umbrella or
  a named replay runtime gate; no replay source file exceeds 1,500 lines without
  an inventory justification based on cohesion rather than convenience.
  Complete 2026-07-11: stable-id restore rejects duplicate ids and ignores stale
  row hints; topology mutation preflights every row owner; recoverable failures
  capture and hash-verify a live rollback sample. The Debug v2 probe injects a
  post-mutation target-hash mismatch and proves fallback restoration. The
  reconciled inventory is 26 files / 24,904 lines; `ReplayRuntime.h` is 1,485
  lines and the five larger implementation files have explicit cohesion and
  split-expiry evidence. The first independent review found the production
  `ReplayLiveWorld` bag, partial-mutation hazards, and missing stale-hint proof;
  the required repeat review found duplicate-id and rollback-test gaps. All
  findings were fixed. Fast, CPU, scrub, v2, interaction, physics, perf, DX12,
  and full gates pass. Evidence:
  `Agentic/Reports/replay_r5_closure_20260711.md`.

## Dependencies And Sequencing

- Coordinate R2 with `runtime-shell-decomposition.md` extraction 4 and
  `runtime-ui-control-architecture-cleanup.md`.
- Coordinate R4 with `physics-authority-and-identity.md` D phases.
- `fracture-replay-feature.md` is blocked until R3 is complete and the retained
  presentation-sample extension contract is recorded.
- `validation-gate-integrity.md` V1/V2 should land before R5 closure.

## Validation

| Slice | Required gate |
|---|---|
| CPU data model / artifact logic | CPU-test umbrella |
| Scrub, restore, branch, prediction | `tools\validate_replay_scrub.bat` + focused interaction proof |
| Physics identity or restore | `tools\validate_physics.bat` + replay scrub |
| Rendered overlays | `tools\validate_dx12_renderer.bat` |
| Capacity/allocation changes | `tools\validate_perf.bat` |
| Cross-owner closure | `tools\validate_full.bat` after CPU umbrella integration |

## Definition Of Done

- Inventory measurements match current tracked source.
- Replay business methods have left `Run`.
- Stable replay identity and retained-sample ownership are documented and
  tested.
- Allocation exceptions are measured, capped, and replay-owned.
- Unsupported compatibility paths are deleted.
- Fracture replay can extend one documented presentation-sample seam rather
  than inventing a parallel replay system.
