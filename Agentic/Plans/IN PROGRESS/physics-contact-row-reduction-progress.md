# Physics Contact Row Reduction Progress

Purpose: track implementation progress for the six contact-row reduction steps in `Agentic/Plans/IN PROGRESS/physics-contact-row-reduction-plan.md`.

Parent plan: `Agentic/Plans/IN PROGRESS/physics-contact-row-reduction-plan.md`

## Current Status

- Status: Not started.
- Branch at plan creation: `codex/physics-wall-cpu-profile-200-bodies`.
- Created: 2026-06-30.
- Impact area: physics solver performance and determinism.
- Validation for this document-only progress file: no repository validation required.

## Hard Constraints

- [ ] Preserve deterministic physics behavior unless an intentional baseline update is explicitly approved.
- [ ] Take before/after samples before claiming any performance result.
- [ ] Keep high-impact and newly colliding pairs conservative until measured.
- [ ] Do not reduce terrain contacts in the first implementation slice.
- [ ] Keep row selection deterministic with stable tie-breaks.
- [ ] Run the comment-style audit for touched source-bearing files during implementation.
- [ ] Run `tools\validate_physics.bat` and `tools\validate_perf.bat` before committing source changes.

## Step 0 - Preflight And Baseline Evidence

- [ ] Run `git status --short --branch` and record pre-existing dirty files as user-owned.
- [ ] Read `AGENTS.md`, `README.md`, `Agentic/README.md`, and `Agentic/SessionState.md`.
- [ ] Read the parent plan.
- [ ] Inspect `ObjectContactManifold` box/box face generation and feature IDs.
- [ ] Inspect `PersistentContactSolver` row append, warm-start, friction, and cache-store paths.
- [ ] Identify the exact wall benchmark scene and launch command to reuse.
- [ ] Capture three before samples for row counts and solver timings.
- [ ] Decide the smallest first implementation slice.

## Step 1 - Cached Two-Point Cap For Resting Box/Box Face Manifolds

- [ ] Add a narrow box/box face-manifold reduction gate.
- [ ] Gate reduction to low-impact, resting-policy candidates only.
- [ ] Prefer cached feature IDs when selecting rows.
- [ ] Fall back to deepest point plus farthest-spread point when cache evidence is insufficient.
- [ ] Preserve full four-point manifolds for new impacts, high relative speed, high angular speed, and ambiguous support.
- [ ] Record before/after row counts and `SolveRows` timing.
- [ ] Validate with `tools\validate_physics.bat` and `tools\validate_perf.bat`.

## Step 2 - Cache-Prioritized Manifold Reduction

- [ ] Move or keep reduction at the point where `m_persistentContactCache` is available.
- [ ] Score candidates by cache hit, cached normal impulse, penetration, support orientation, and feature ID.
- [ ] Preserve deterministic selected-row ordering.
- [ ] Add bounded diagnostics for original count, reduced count, selected features, and reason.
- [ ] Confirm cache hits/warm-start rows do not regress after the first few frames.
- [ ] Validate with physics and perf gates if source changes are included.

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

## Open Questions

- What exact relative-speed and angular-speed thresholds should define "resting enough" for row reduction?
- Should row reduction live in `PersistentContactSolver` only, or should `ObjectContactManifold` expose helper metadata for face-vs-edge contact type?
- Do we need per-pair contact age in the persistent cache, or can cache-hit/impulse evidence carry the first implementation?
- Should reduced row selection be config-disabled by default for the first measured branch?

## Handoff Requirements

- Record every implementation commit that closes a checkbox.
- Record exact before/after sample commands and summary numbers.
- Record validation commands, results, and log paths.
- If SkullScope is used, include trace command, query commands, artifact sizes, per-query output sizes, and total GPT-read size in the final handoff.
