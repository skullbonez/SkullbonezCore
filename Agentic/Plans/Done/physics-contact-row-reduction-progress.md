# Physics Contact Row Reduction Progress

Purpose: track implementation progress for the six contact-row reduction steps in `Agentic/Plans/IN PROGRESS/physics-contact-row-reduction-plan.md`.

Parent plan: `Agentic/Plans/IN PROGRESS/physics-contact-row-reduction-plan.md`

## Current Status

- Status: First source slice implemented, measured, and validated.
- Branch at implementation: `nightrunner-30th-june`.
- Created: 2026-06-30.
- Impact area: physics solver performance and determinism.
- Validation for the source slice: `tools\validate_physics.bat` and `tools\validate_perf.bat` passed on 2026-06-30.

## Hard Constraints

- [x] Preserve deterministic physics behavior unless an intentional baseline update is explicitly approved.
- [x] Take before/after samples before claiming any performance result.
- [x] Keep high-impact and newly colliding pairs conservative until measured.
- [x] Do not reduce terrain contacts in the first implementation slice.
- [x] Keep row selection deterministic with stable tie-breaks.
- [x] Run the comment-style audit for touched source-bearing files during implementation.
- [x] Run `tools\validate_physics.bat` and `tools\validate_perf.bat` before committing source changes.

## Step 0 - Preflight And Baseline Evidence

- [x] Run `git status --short --branch` and record pre-existing dirty files as user-owned.
- [x] Read `AGENTS.md`, `README.md`, `Agentic/README.md`, and `Agentic/SessionState.md`.
- [x] Read the parent plan.
- [x] Inspect `ObjectContactManifold` box/box, box/hull, and hull/hull face generation and feature IDs.
- [x] Inspect `PersistentContactSolver` row append, warm-start, friction, and cache-store paths.
- [x] Identify the exact wall benchmark scene and launch command to reuse.
- [x] Capture three before samples for row counts and solver timings.
- [x] Decide the smallest first implementation slice.

## Step 1 - Cached Two-Point Cap For Resting Box/Box Face Manifolds

- [x] Add a narrow same-shape box/box and hull/hull face-manifold reduction gate.
- [x] Gate reduction to low-impact, resting-policy candidates only.
- [x] Prefer cached feature IDs when selecting rows.
- [x] Preserve full rows when cache evidence is insufficient; no reduction is applied until at least two points are warm-cache hits.
- [x] Preserve full four-point manifolds for new impacts, high relative speed, high angular speed, mixed hull/box contacts, sphere contacts, terrain contacts, and ambiguous support.
- [x] Record before/after row counts and `SolveRows` timing.
- [x] Validate with `tools\validate_physics.bat` and `tools\validate_perf.bat`.

## Step 2 - Cache-Prioritized Manifold Reduction

- [x] Move or keep reduction at the point where `m_persistentContactCache` is available.
- [ ] Score candidates by cache hit, cached normal impulse, penetration, support orientation, and feature ID.
- [x] Preserve deterministic selected-row ordering.
- [x] Add bounded diagnostics through the `ContactRowReduction` profiler scope and SkullScope solver-row summaries.
- [x] Confirm cache and warm-start rates remain acceptable after the first few frames.
- [x] Validate with physics and perf gates if source changes are included.

## Step 3 - Impulse-Based Row Retirement

- [ ] Track rows that repeatedly solve to near-zero normal and tangent impulse.
- [ ] Retire only stable resting-policy rows, never fresh impact rows.
- [ ] Prefer top rows per pair by solved normal impulse.
- [ ] Keep deterministic feature-ID tie-breaks.
- [ ] Verify fixed-contact release still observes meaningful impulses.
- [ ] Compare settled-scene row counts before/after.

## Step 4 - Per-Body Resting Row Budget

- [ ] Add or prototype a config-backed `max_resting_rows_per_body` style budget.
- [ ] Apply the budget only after non-resting/impact rows are protected.
- [ ] Rank candidate resting rows by cache hit, normal impulse, upward support, penetration, and feature ID.
- [ ] Recompute or update `persistentContactCounts` and `persistentRestingContactCounts` from the final row set.
- [ ] Verify sleep support edges still produce credible islands.
- [ ] Measure per-body row distribution in the wall benchmark.

## Step 5 - Friction Row Reduction

- [ ] Prototype solving tangent friction on fewer representative rows for stable box/box face contacts.
- [ ] Keep normal support rows independent from friction-row count.
- [ ] Restore full friction for high tangential speed, impacts, and unstable cache evidence.
- [ ] Compare sliding/stack stability against the full-friction baseline.
- [ ] Measure whether `SolveRows` improves beyond normal row reduction alone.

## Step 6 - State Ladder For Contact Detail

- [ ] Define deterministic contact states: full detail, reduced detail, minimal/sleep-backed detail.
- [ ] Add stable-age thresholds and promotion rules.
- [ ] Immediately promote to full detail on velocity spikes, angular motion, normal change, penetration growth, or cache churn.
- [ ] Keep thresholds config-backed if they affect runtime behavior.
- [ ] Verify first-impact behavior stays full-detail.
- [ ] Verify waking a settled pile restores enough detail before visible divergence.

## Evidence Log

- 2026-06-30: Plan/progress files created from the contact-row reduction discussion. No code changes yet.
- 2026-06-30: Created branch `nightrunner-30th-june` from clean `main`.
- 2026-06-30: Baseline scene for requested test is `SkullbonezData/scenes/aaa_ragdoll_sunset_showcase.scene.json`; generated fixed-step measurement copies under `TestOutput/validation/contact_row_reduction/` with screenshot capture removed and 1800 frames.
- 2026-06-30: Before Profile baseline, three runs of `Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step --hide-overlay --scene TestOutput\validation\contact_row_reduction\before_<n>.scene.json --frames 1800`. Settled window frames 1500-1799 averaged `Frame/Physics` 3.077006 ms, `PersistentContacts` 2.563539 ms, `SolveRows` 0.735922 ms.
- 2026-06-30: Implemented same-shape warmed contact row reduction in `SkullbonezSource/Physics/PersistentContactSolver.cpp`. The reducer runs after object manifolds are built and before persistent rows are appended, requires quiet point-relative velocity, low angular speed, same-shape box/box or hull/hull contact, resting footprint support, and at least two cached feature IDs.
- 2026-06-30: Rejected an earlier mixed hull/box trial because SkullScope found a late settled `penetration_growing` event on a ragdoll-limb/brick contact. Compact evidence retained in `profile_before_after_mixed_shape_trial.json` and `skullscope_quality_mixed_shape_trial.json`.
- 2026-06-30: Final after Profile runs, three runs of `Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step --hide-overlay --scene TestOutput\validation\contact_row_reduction\after_<n>.scene.json --frames 1800`. Summary artifacts: `after_profile_summary.json` and `profile_before_after_comparison.json`.
- 2026-06-30: Final Profile deltas: full `Frame/Physics` 3.877063 -> 3.248653 ms (-16.208%), full `PersistentContacts` 3.299388 -> 2.721774 ms (-17.507%), full `SolveRows` 0.933457 -> 0.696086 ms (-25.429%). Settled `Frame/Physics` 3.077006 -> 2.596596 ms (-15.613%), settled `PersistentContacts` 2.563539 -> 2.129873 ms (-16.917%), settled `SolveRows` 0.735922 -> 0.573445 ms (-22.078%).
- 2026-06-30: Final SkullScope quality comparison from `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step --hide-overlay --scene TestOutput\validation\contact_row_reduction\after_trace.scene.json --frames 1800 --physics-diag TestOutput\validation\contact_row_reduction\after.physicsdiag.ndjson`. Summary artifact: `skullscope_quality_comparison.json`.
- 2026-06-30: Final SkullScope deltas: average contact rows 584.718889 -> 447.628889 (-23.445%), settled contact rows 481.346667 -> 384.890000 (-20.039%), final max penetration 0.035400 -> 0.010572, final total energy 30.545744 -> 23.208514, high `penetration_growing` events 9 -> 6, and settled-window events 0 -> 0.
- 2026-06-30: Validation passed. `tools\validate_physics.bat` completed in 18.57 s and reported `VALIDATE_PHYSICS: ALL PASSED`; core physics regression CSV matched the committed baseline byte-exactly. `tools\validate_perf.bat` completed in 21.23 s and reported `VALIDATE_PERF: COMPLETE`; DX12 absolute performance budgets passed and both Profile/Debug binaries were left ready. Logs: `TestOutput\validation\contact_row_reduction\validate_physics_cached_gate.log`, `TestOutput\validation\contact_row_reduction\validate_perf_cached_gate.log`.

## Open Questions

- What exact relative-speed and angular-speed thresholds should define "resting enough" for future wider row reduction?
- Should `ObjectContactManifold` expose helper metadata for face-vs-edge contact type before Step 3 or Step 4?
- Do we need explicit per-pair contact age in the persistent cache, or can cache-hit/impulse evidence carry the next implementation?
- Should reduced row selection be config-disabled by default for the first measured branch?

## Handoff Requirements

- Record every implementation commit that closes a checkbox.
- Record exact before/after sample commands and summary numbers.
- Record validation commands, results, and log paths.
- If SkullScope is used, include trace command, query commands, artifact sizes, per-query output sizes, and total GPT-read size in the final handoff.
