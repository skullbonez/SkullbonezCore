# Monolith Translation-Unit Right-Sizing — Finish What The Campaigns Declared Done

Date: 2026-07-17
Status: Active — 6/8 tasks
Branch: `nightrunner-17th-july` (owner-ratified at N0)
Impact area: `Runtime/Replay/ReplayPrediction.cpp`,
`Runtime/InteractionAutomationController.cpp`,
`Rendering/DX12/RenderBackendDX12.cpp/.h`, `Runtime/Replay/ReplayRecorder.cpp`,
`UI/UI.cpp`, `Physics/PhysicsApi.cpp`, project filters
Owner: cross-cutting (per-file owner named per task)

## Problem And Evidence (measured 2026-07-17 at the `main` tip, 0d77d51a4)

The 2026-07-17 hostile review found that monolith files survive despite the
decomposition campaigns being marked complete. Current `wc -l`:

| File | Lines |
|---|---:|
| `Runtime/Replay/ReplayPrediction.cpp` | 4,428 |
| `Runtime/InteractionAutomationController.cpp` | 4,131 |
| `Rendering/DX12/RenderBackendDX12.cpp` | 3,636 |
| `Runtime/Replay/ReplayRecorder.cpp` | 3,492 |
| `Rendering/DX12/RenderBackendDX12.h` | 1,431 (header) |
| `UI/UI.cpp` | 2,501 |
| `Physics/PhysicsApi.cpp` | 2,226 |

Prior rulings that bind this plan:

- Replay mass-reduction R6 (2026-07-16) ruled the oversized replay TUs
  cohesive because a moves-only split would need a new internal API or
  duplicate byte/order contracts. Re-litigation requires **new evidence**;
  this plan's replay tasks therefore target evidence-backed *internal
  responsibility* extraction behind the existing six owner boundaries, not a
  TU shuffle, and each replay task needs a fresh owner ruling.
- Round-3 explicitly excluded `RenderBackendDX12` re-partitioning beyond the
  bindless/frame-headroom task; the owner must lift that parking at N0 for
  the DX12 tasks to proceed.
- The `AGENTS.md` god-object closure rule: a mechanical TU split is not
  decomposition. Every task here must extract a named responsibility with its
  own state and boundary, or record a cohesion ruling and close honestly.

## Goal

Every product translation unit above ~2,000 lines either (a) loses a named,
concretely-owned responsibility to a new owner file with typed boundaries, or
(b) carries a current, dated, owner-signed cohesion ruling in this plan's
closure report. No file may satisfy the plan through a forwarding facade or
mechanical split.

## Non-Goals

- No replay ownership re-decomposition: the monolith campaign's six owner
  boundaries stand. Work happens *inside* those boundaries.
- No artifact-format, probe-schema, or byte/order contract changes.
- No behavioral change anywhere; zero baseline/golden/screenshot refresh.
- No line quotas: a 2,400-line cohesive owner with a recorded ruling is an
  acceptable outcome; a 900-line bag is not.

## Tasks

- [x] N0 — Census, rulings, and order. Regenerate the oversized-TU inventory
  (product configs only), map each file's internal responsibility clusters
  with line-range evidence, and obtain per-file owner rulings: split targets,
  cohesion claims to test, and the DX12 parking lift. Ratify the branch.
  Evidence: dated report under `Agentic/Reports/`.
- [x] N1 — `InteractionAutomationController.cpp` (4,131). This automation
  harness is one class covering CLI parsing, input injection, probe
  sequencing, and report emission. Extract at least the probe/report emission
  and the input-injection driver into owner-named automation TUs with typed
  inputs. It is automation-boundary code, so the link-level configuration
  exclusion must be preserved exactly. Gate: `validate_full` (automation
  boundary lane).
- [x] N2 — `PhysicsApi.cpp` (2,226). Partition by API domain (body/collider
  lifecycle, queries, diagnostics access) into owner-named physics TUs only
  where the census shows separable state; otherwise record the cohesion
  ruling. Gate: `validate_physics` byte-exact.
- [x] N3 — `UI.cpp` (2,501). Extract per-tab or per-widget-family
  responsibility clusters the census identifies (layout constants must stay
  with their hit-test consumers per the UI invariant). Gate: `validate_fast`
  plus one interactive smoke run.
- [x] N4 — `RenderBackendDX12.cpp` (3,636) + header (1,431). Requires the N0
  parking lift. Extract the census-selected cluster (candidates: swapchain /
  present path, readback/screenshot, render-graph transient materialization)
  into concrete Dx12 owner files consistent with the existing
  `Dx12FrameOwner`/`Dx12PipelineOwner` pattern. Shrink the header by moving
  implementation-only types out of the public surface. Gates:
  `validate_dx12_renderer`, then `tools\run_graphics_stress.bat 1` with
  recorded command, runtime, and exit evidence; `dx12_validation.txt` = 0.
- [x] N5 — `ReplayRecorder.cpp` (3,492). New-evidence pass inside the
  recorder owner: if the census shows separable responsibilities (ring
  bookkeeping vs. encode staging vs. eviction policy) extract them behind the
  existing owner; otherwise record a fresh dated cohesion ruling superseding
  nothing. One mega-gate invocation per MASTER rule 11. Gate:
  `tools\validate_replay_visual_fidelity.bat` plus `validate_tests`.
- [ ] N6 — `ReplayPrediction.cpp` (4,428) — the engine's largest product TU.
  Same new-evidence protocol as N5; the prediction single-writer /
  published-prefix protocol is load-bearing and must move intact or not at
  all. One mega-gate invocation. Gate:
  `tools\validate_replay_visual_fidelity.bat` plus `validate_tests`.
- [ ] N7 — Independent review and closure. Rerun the oversized-TU census
  from final source; every file above ~2,000 lines has either a completed
  extraction task or a dated owner cohesion ruling in the closure report.
  Independent review confirms no extraction produced a facade, bag, or
  reach-back. Final gates: `validate_full`; DX12-touching outcome reruns the
  stress proof. Update MASTER-PLAN and delete this plan on closure.

## Dependencies And Decisions

- N0 owner rulings are mandatory before any splitting task; N4 additionally
  requires the explicit round-3 parking lift.
- Execution order: N1 → N2 → N3 (isolated, cheap gates first), then N4
  (renderer), then N5 → N6 (replay, most gate-expensive), then N7.
- Every replay task consumes exactly one mega-gate invocation, one engine
  process, one prediction generation; a diff is a revert, never fix-forward.
- Allocation-allowlist rows move with their code in the same commit.

Owner-ratified N0 decisions (2026-07-17): use branch
`nightrunner-17th-july`; use the complete 16-TU product inventory and current
line-range map in
`Agentic/Reports/2026-07-17/monolith-tu-right-sizing-census.md`; execute the N1
automation input-driver/report-writer split and N3 window-interaction owner;
record conditional cohesion decisions at N2/N5/N6 only after their fresh
dependency passes; retain the dated cohesion rulings for the other ten TUs;
and lift the Round-3 DX12 parking only for moving the existing frame/deferred-
release owners and implementation-only declarations in N4. No baseline,
golden, screenshot, artifact-format, frame-buffering, or behavior change is
authorized.

## Acceptance

- Final census shows every >2,000-line product TU resolved per the Goal.
- Zero new types named around migration mechanics; every new file is an
  owner-named domain TU.
- All mapped gates pass from final source with unchanged baselines, goldens,
  and `dx12_validation.txt` = 0.

## Validation

Per-task gates as listed. The plan touches multiple areas, so final closure
runs `validate_full`; DX12 tasks additionally carry the mandatory bounded
graphics-stress proof, and replay tasks the one-invocation mega gate.
