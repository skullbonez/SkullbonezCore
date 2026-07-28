# Physics Body Hot Layout Closure

Date: 2026-07-28
Branch: `nightrunner-28th-JUL-26`
Plan: `Agentic/Plans/DONE/physics-body-hot-layout-evidence.md`
Status: Complete — 4/4 phases

## Outcome

The Physics body hot store remains a structure of arrays (SoA), as directed by
the owner. The plan corrected the former eight-body-load claim, measured the
real scalar consumers and five-scene performance, and removed only the 20 inert
`alignas(32)` annotations on list control blocks plus their C4324 warning
suppression.

`PhysicsFixedList` continues to own the useful guarantee: every independently
allocated payload starts at a boundary of at least 32 bytes. The 18 `float`
streams, two flag streams, separate scene-load allocations, field order,
live-prefix contract, views, and scalar consumers are unchanged.

## Evidence Chain

- BL0:
  `Agentic/Reports/2026-07-28/physics-body-hot-layout-bl0.md`
  inventories 20 payloads, 74 live bytes per body, scalar consumers, and the
  clean five-scene baseline witness.
- BL1:
  `Agentic/Reports/2026-07-28/physics-body-hot-layout-bl1-ruling.md`
  selects only inert control-block alignment removal and rejects unmeasured
  contiguous-backing, vectorization, and AoS work.
- BL2:
  `Agentic/Reports/2026-07-28/physics-body-hot-layout-bl2-validation.md`
  records implementation, byte-exact Physics, and the first post-change
  performance run.

The 20-stream payload-alignment doctest remains in
`SkullbonezTest/TestPhysicsHandles.cpp`; it checks each `HotFields()` data
pointer rather than the containing store's member addresses.

## Final Performance Witness

The committed-source BL3 run at `11ff7774` passed
`tools\validate_perf.bat` in 91.5 seconds. Every scene captured 1,140 frames.

| Scene | BL0 P50 | BL3 P50 | Delta | BL0 P99 | BL3 P99 | Delta |
|---|---:|---:|---:|---:|---:|---:|
| 200 | 0.1187 ms | 0.1197 ms | +0.8% | 0.2020 ms | 0.2222 ms | +10.0% |
| 520 | 0.8179 ms | 0.8593 ms | +5.1% | 1.0453 ms | 1.0854 ms | +3.8% |
| 1,000 | 1.1580 ms | 1.2148 ms | +4.9% | 1.5884 ms | 1.5912 ms | +0.2% |
| 2,000 | 2.0764 ms | 2.0248 ms | -2.5% | 3.1065 ms | 3.1201 ms | +0.4% |
| sleepy-5,000 | 1.3354 ms | 1.3164 ms | -1.4% | 26.9538 ms | 27.1128 ms | +0.6% |

The same-source BL2-to-BL3 P50 movement is
`+0.4%, +1.7%, +4.2%, -3.8%, -3.8%`. Movement changes direction across repeat
runs and has no body-count-correlated pattern. The evidence therefore shows no
repeatable, layout-attributable performance regression; it does not claim
timings are identical. Low-count P99 remains noisy and is the residual risk.
No performance baseline or accepted artifact was refreshed.

## Ownership Inventories

All self-tests pass. Final scans report:

- wide signatures: 407 rows, maximum arity 12, 33 exact-12 rows, zero over 12;
- aggregate ownership: 1,172 discovered, 86 review candidates, 86 ruled, zero
  unruled, zero signalled; and
- extraction scars: one retained `WorkerPool` alias, one ruled, zero unruled.

The exact-12 rows remain visible for the next campaign plan, which replaces the
hard ceiling with the owner's qualitative review rule.

## Final Validation

All final gates ran in an isolated worktree whose executable source excluded the
uncommitted warm-start experiment:

| Gate | Result |
|---|---|
| `tools\validate_physics.bat` | PASS in 109.1 s; Profile/Debug warning-clean and committed Physics output byte-exact |
| `tools\validate_perf.bat` | PASS in 91.5 s; five Physics captures and allocation/performance policy checks |
| `tools\validate_full.bat` | PASS in 431.1 s after the candidate header received its required mechanical formatting |

The first full-gate attempt stopped at formatting preflight. The formatter was
run only in the isolated worktree, produced exactly the intended
`PhysicsBodyStore.h` declaration-layout diff, and the corrected rerun passed.
No baseline was refreshed.

## Comment And Independent Review

Touched-source audit:

- `SkullbonezSource/Physics/PhysicsBodyStore.h`: checked
- Checked: 1/1
- Deferred: 0
- Unchecked: none

The learning header now defines SoA, names the independently allocated payload
alignment owner, and accurately describes scalar production consumers.

The independent read-only BL3 review found zero blockers and no missing
evidence. It confirmed:

- no aggregate, capability slice, extraction scar, rename evasion, or new
  owner surface;
- unchanged payload allocation/alignment and reserve ownership;
- intact 20-stream payload-alignment coverage; and
- no repeatable, layout-attributable performance degradation.

The warm-start experiment remains outside this plan and intentionally unstaged
for owner evaluation.
