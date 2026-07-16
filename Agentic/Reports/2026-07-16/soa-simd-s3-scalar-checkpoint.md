# SoA/SIMD S3 — Scalar Checkpoint

Date: 2026-07-16  
Parent tip measured: `4a96814fe`  
Reference machine: AMD Threadripper 3970X, 64 logical cores  
Owner: physics

## Outcome

The SoA-scalar checkpoint is accepted. The first S3 matrix exposed a real
regression: 1,000-body Physics averaged 1.2949 ms, 29.8% slower than S0's
0.9978 ms. Inspection found two scalar-only costs introduced by the S2 layout:

1. `PhysicsBodyHotFieldsConstView` and `PhysicsBodyHotFieldsView` each aggregate
   20 spans, but nested stage helpers passed the aggregate by value inside body
   and candidate-pair loops.
2. pending-impulse, pose-integration, and force kernels loaded and rewrote the
   complete 20-field row even though their mutations are limited to velocity or
   pose plus velocity.

The correction passes hot views by const reference and narrows the three store
kernels to the fields they read and write. Arithmetic order, iteration order,
and stored values are unchanged. `validate_physics` proves the correction
against all 44,401 baseline lines byte-for-byte.

## Final Matrix Against S0

All values are milliseconds. Percentages compare final S3 averages with the S0
scalar-AoS reference. The performance gate classifies marker movement with its
ramped noise threshold, `max(10%, 10 / sqrt(marker_ms))`; the small absolute
200-body and narrowphase changes are inside that envelope.

| Bodies | Frame | Physics | Physics delta | p50 | p99 | Apply | Broad | Grid | Terrain | Integrate | Narrow |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 200 | 0.4075 | 0.1009 | +11.4% | 0.0990 | 0.1802 | 0.0263 | 0.0216 | 0.0184 | 0.0173 | 0.0212 | 0.0006 |
| 520 | 1.2611 | 0.7174 | -5.3% | 0.7185 | 0.9442 | 0.1547 | 0.1302 | 0.1176 | 0.1820 | 0.1781 | 0.0038 |
| 1,000 | 1.7097 | 0.9795 | -1.8% | 0.9797 | 1.3411 | 0.1668 | 0.2667 | 0.2217 | 0.2099 | 0.1984 | 0.0087 |
| 2,000 | 9.1179 | 7.8972 | +3.7% | 9.9144 | 13.8476 | 0.1784 | 6.8572 | 6.6769 | 0.2680 | 0.2313 | 0.0305 |

The acceptance load is restored below S0: 1,000-body Physics is 0.9795 ms,
1.8% faster than the 0.9978 ms scalar-AoS reference. The 520-body row improves
5.3%. The 2,000-body stretch row moves 3.7%, below the gate's 10% floor, and
remains dominated by the previously recorded grid-build scaling cost.

## Exactness And Validation

- `tools\validate_build.bat Profile`: passed, zero warnings and zero errors,
  21.05 seconds after the correction.
- `tools\validate_physics.bat`: passed standalone/runtime-handle smoke and the
  44,401-line varied-scene CSV byte-exact comparison, 64.87 seconds.
- `tools\validate_perf.bat`: complete, all absolute performance budgets,
  allocation policy, gameplay allocation guard, selected-ball structural
  regression, DX12 lane, and scale matrix passed, 100.10 seconds.
- No physics baseline, replay golden, or performance baseline was refreshed.
- The original deterministic mismatch/oracle remains byte-exact; the change
  only removes redundant view copies and unrelated field traffic.

## Comment Audit Checklist

Scope was derived from `git diff --name-only` for source-bearing files. All 15
touched files were inspected against
`Agentic/Reference/comment-style-guide.md`: 15 checked, 0 deferred, 0 unchecked.
Every file retains File, Purpose, Summary, Glossary, Invariants, and Related
learning-header sections. The store header now explains the 20-span borrowed
view cost/lifetime, and the narrowed store writes state their velocity/pose
invariants locally. No wording requires owner clarification.

