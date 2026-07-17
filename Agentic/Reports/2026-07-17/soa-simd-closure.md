# Physics SoA/SIMD Campaign Closure

Date: 2026-07-17

Branch: `nightrunner-16th-july`

Plan: `physics-soa-simd-1000-bodies`, S0-S8 complete

## Final verdict

The campaign improved the production codebase overall, but not because SIMD
met its premise. The durable result is a fixed-capacity SoA scalar body store,
direct hot-field consumers, removal of accidental span copies, a complete
bounded SpatialGrid, substantially stronger behavioral coverage, and a much
smaller production surface. The measured-neutral SIMD experiment is gone in
full; there is no dark path, toggle, CLI/config setting, per-file AVX build
island, A/B tool, or dedicated SIMD test left to maintain.

S7's source commit removed 3,193 lines while adding 699 lines including its
201-line evidence report. Ten kernel files and one 309-line A/B script were
deleted. `SpatialGrid.cpp` shrank by 498 net lines and `SpatialGrid.h` by 111
net lines while retaining correct admission through 8,192 unique cells and an
explicit fatal at 8,193. The simplified grid has one storage and lookup path,
not the prior primary/secondary routing.

This is a code-quality win: less conditional behavior, less configuration
surface, fewer project-specific compiler settings, fewer stale diagnostics,
more direct ownership, a tested capacity failure, deterministic migration of
the obsolete config key, and coverage floors enforced in both local and hosted
mandatory CPU validation. Independent review ended with no findings after six
concrete issues were fixed.

## What remains from each phase

| Phase | Durable outcome |
|---|---|
| S0 | Fixed-seed 200/520/1,000/2,000-body performance fixtures and the honest 0.9978 ms AoS reference. |
| S1 | Twenty aligned, fixed-capacity SoA hot-field arrays in `PhysicsBodyStore`. |
| S2 | Direct scalar hot-field consumers; the transitional record shim was deleted. |
| S3 | Accidental by-value span copies and unrelated full-row traffic removed; isolated SoA result 0.9795 ms and byte-exact. |
| S4-S6 | Historical evidence that the deterministic AVX2/FMA kernels were roughly neutral and missed the 0.80 ms cutover target; no production code retained. |
| S7 | SIMD deleted, one complete 8,192-cell grid retained, attribution-only machinery removed, config v4 migration and provenance-only reconciliation completed, coverage made mandatory, all gates green. |
| S8 | Whole-campaign review reconciled, closure published, live plan deleted, branch synchronization confirmed. |

## Performance disposition

The arithmetic experiment did not earn production complexity. S6's same-tip
1,000-body A/B was 1.35% faster with SIMD enabled (1.0963 versus 1.1113 ms),
which is noise-sized relative to the failed 0.80 ms cutover budget. S3's layout
checkpoint was a real 1.8% improvement over S0 (0.9795 versus 0.9978 ms) after
the accidental span copies were removed.

The final scalar-only S7 gate is mixed relative to the previously retained
two-route grid:

| Bodies | Previous Step / Broadphase | Final Step / Broadphase | Disposition |
|---:|---:|---:|---|
| 1,000 | 1.0546 / 0.2741 ms | 1.0871 / 0.3005 ms | modest low-scale regression |
| 2,000 | 2.0517 / 0.8904 ms | 1.8670 / 0.7549 ms | high-load improvement |

All absolute performance budgets still pass. The branch does not claim the
old 0.80 ms target, nor that the final full product preserves S3's isolated
layout number. A future performance campaign should target measured broadphase
work with a fresh plan; this campaign preserves no speculative SIMD fallback.

## Correctness, migration, and provenance

- The 44,401-line physics regression remains byte-exact; no behavioral physics
  baseline was regenerated.
- Replay visual-fidelity passed all 16 packet controls and 72 assertions with
  one engine process and one prediction generation.
- The two replay manifest changes are provenance-only consequences of engine
  config v4. The config SHA is
  `f5fe4daa3a3667b168da9a5aad3dbfc846a5857378bb358b50ec8de40c09e3fe`;
  the causal binding is
  `77f2044158694097e55a92d348ad2529d982e8730878ea89c767786cf7fe56df`.
  This instance is recorded under the standing owner ruling.
- Native config writers and the migration tool both remove the obsolete v3
  SIMD key before stamping v4, while preserving unknown settings.
- SpatialGrid focused tests cover displaced keys, full capacity, reinsertion,
  copy/clear behavior, and the owner-attributed exhaustion fatal.

## Final validation and review

The S7 evidence report contains every command, duration, performance row,
SkullScope query, and artifact/query-size accounting:
`Agentic/Reports/2026-07-17/soa-simd-s7-scalar-cleanup.md`.

Final gates:

- migration, allocation policy, and query regression: passed;
- coverage: all ten ratified subsystem floors passed, whole product 68.99%;
- performance: all absolute/comparison/allocation guards passed;
- replay visual fidelity: passed with all false-pass controls effective;
- full gate: 282 tests and 21,389 assertions, zero-warning Profile/Debug
  builds, zero DX12 validation errors, matching captures, and byte-exact
  physics.

Touched-source comment audit closed at 30/30 checked, 0 deferred, 0 unchecked.
Checklist path is N/A in touched-file mode. Independent reviewer Aquinas
(`/root/u9_independent_review`) returned zero blocking and zero non-blocking
findings after the six review-discovered issues were corrected.

## Closure state

S0-S8 are complete. The live plan is deleted under MASTER inventory rule 4;
this report and the per-phase reports are the archive. There is no active or
future implementation plan in the authoritative portfolio ledger at this tip.
