# Replay Architecture And Right-Sizing

Date: 2026-07-10
Status: In progress - 1/6 phases complete
Impact area: replay runtime, prediction, scrub/restore, replay UI, memory policy,
Run decomposition
Owner: replay subsystem

## Problem

Current tracked measurements are 28 replay source files / 23,814 lines, with
`RunReplayTools.cpp` at 4,778 lines. Replay is larger than the DX12 backend and
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
- [ ] **R1 — Delete and merge obsolete paths.** Remove duplicate presentation,
  legacy artifact, compatibility, and probe paths that no longer protect a
  supported format or workflow. Each retained legacy path needs an owner,
  supported input, and deletion condition. Acceptance: no behavior-free
  forwarding wrapper or unowned fallback remains.
- [ ] **R2 — Move replay workspace behavior out of `Run`.** Move scrubber,
  cause-tree, velocity-edit, prediction-horizon, inspection-camera request,
  and overlay decisions behind replay-owned APIs. Input produces typed replay
  actions; replay returns commands and fixed-capacity draw records. Acceptance:
  `Run.h` exposes no `TickReplay*`, `RenderReplay*`, or replay camera-transition
  business method.
- [ ] **R3 — Stabilise the retained-sample and memory model.** Name the durable
  recorded solver sample, presentation sample, prediction prefix, and artifact
  ownership rules. Right-size capacities from measured high-water evidence;
  preserve registered replay growth only where fixed preallocation is
  impractical. Acceptance: every replay growth owner has phase, hard cap,
  counter, and exhaustion behavior.
- [ ] **R4 — Narrow live-owner access.** Replace replay traversal through
  `GameModelCollection` with body/collider/render/scene views and stable ids.
  Restore remains a command into physics/scene owners, not a write through a
  model compatibility surface. Acceptance: replay headers do not store a bare
  model index as persistent identity.
- [ ] **R5 — Tests, size closure, and feature gate.** Extend scrub/restore,
  hash, branch, prediction, artifact, and allocation tests. Perform one final
  independent review. Acceptance: all replay tests run from the CPU umbrella or
  a named replay runtime gate; no replay source file exceeds 1,500 lines without
  an inventory justification based on cohesion rather than convenience.

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
