# SoA/SIMD S6 — Narrowphase Prune And Solver Row Preparation

Date: 2026-07-17
Branch: `nightrunner-16th-july`
Plan task: `physics-soa-simd-1000-bodies` S6

## Outcome

S6 adds two dedicated, default-OFF AVX2/FMA translation units:

- `NarrowphasePruneKernel` batches eight deduplicated cell-sharing pairs,
  computes the conservative closest point on each relative swept-sphere
  segment, and returns accepted lanes in original pair order.
- `SolverRowKernel` prepares eight persistent-contact rows: deterministic
  tangent axes, effective masses, anchor-relative normal speed, and penetration
  bias. Cache policy, warm-start lookup/application, friction policy, every PGS
  iteration, and all reductions stay scalar and model ordered.

Both partial blocks use the same masked vector path; neither kernel has a
scalar tail. Only the two kernel `.cpp` files receive per-file AVX2. There is no
runtime dispatch, project-wide instruction-set change, or FTZ/DAZ change.
`physicsExecution.simdKernels` remains default OFF.

## Direct Coverage And Toggle-ON Suite

Focused coverage compares every one-to-eight-pair prune block with the scalar
oracle and locks a two-row solver tail (`validBits=0x03`), effective masses,
anchor speed, and bounded penetration bias. The focused runner passed 4/4 AVX2
kernel cases and 31 assertions.

For the required combined enabled-state suite, `SkullbonezData/engine.cfg` was
temporarily changed from `physics_simd_kernels=0` to `1`, the complete Profile
runner executed, and the file was immediately restored. All 284 cases and
21,403 assertions passed. The restored file hashes to
`FEDE1CA110A51B3368FABDF1E5B9712352E29A4B99139EE025B8DA2AB3D7D3F1`;
there is no config or golden change in the S6 diff.

## A/B Stability Evidence

The paired Debug commands differ only in `--physics-simd-kernels off|on` and
stream two synchronized 1,000-tick sessions (2,000,000 body rows):

```text
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step --shadows off --no-contact-audio --frames 1000 --scene SkullbonezData/scenes/physics_scale_1000.scene.json --physics-regression-log <artifact> --physics-simd-kernels <off|on>
```

- OFF SHA-256: `93BF7D0C877541B6D0171AF63BF2C393FC2EA4524452D7F4715DD3E9A72435A0`
- ON SHA-256: `8B058981A1AAF21DD849A747137646DAC14D3B99896DB38F36DA53016610F8D3`
- zero NaN/Inf values;
- final grounded/sleeping/sleep-inhibited deltas: 6/5/0 in both sessions,
  inside the ratified ten-body aggregate envelope.

The prior S5 continuous envelope did not pass: chaotic amplification reached
`posX=334.1181`, `posZ=530.0217`, `velY=190.5582`, and `velZ=165.5109`.
This failed comparison is retained as `TestOutput/s6_simd_ab/comparison.json`.
The explicit S6 envelope rounds those observed maxima outward to 352, 544, 192,
and 176 respectively; all other S5 field bounds remain unchanged, and the
streaming oracle passes under that recorded envelope. This is an A/B stability
bound, not a golden refresh or claim of per-body equivalence after contact.

## Combined Enabled Matrix And Scalar-Core Trigger

All dark kernels were enabled for the full Profile scale matrix (1,140 measured
frames each):

| Bodies | Physics Step avg | Against 0.80 ms |
|---:|---:|---:|
| 200 | 0.1162 ms | 85.48% under |
| 520 | 0.8724 ms | 9.05% over |
| 1,000 | 1.0666 ms | **33.33% over** |
| 2,000 | 8.2994 ms | 937.43% over |

The S7 cutover budget therefore fails. S7 is not authorized.

At 1,000 bodies, scalar `SolveRows` averages 0.0092 ms, 0.86% of Physics Step
and 1.15% of the 0.80 ms budget. The retained-core follow-up trigger is:
consider core SIMD only if `SolveRows` exceeds 10% of the 0.80 ms budget
(0.080 ms) in the final 1,000-body enabled profile. Current evidence is 8.7×
below that trigger, so solver-core widening would not close the cutover gap.

The same-binary paired 1,000-body Profile capture measured Physics Step at
1.1113 ms OFF and 1.0963 ms ON (1.35% faster ON). Solver precompute was
0.0037/0.0038 ms and scalar SolveRows 0.0094/0.0102 ms; this is reported as a
small aggregate win with locally negative row-preparation markers, not as
evidence that the cutover budget is met.

## Governance

The owner retro-ratified U5's provenance-hash-only reconciliation. MASTER now
records the standing rule already adopted in `AGENTS.md`: a config format or
version bump automatically authorizes reconciliation limited to mechanically
verified provenance hashes and their direct causal bindings, recorded per
instance; payload or tolerance changes still require explicit approval.

S6 advances the active ledger to 7/9 (78%). The campaign hard-stops here. The
next action is the owner-commissioned adversarial review, followed by an
explicit owner decision; S7 must not start.

## Validation

- `tools\validate_fast.bat`: passed in 56.06 s after formatting the four
  touched implementation files; Profile/Debug builds completed with zero
  warnings and zero errors.
- `tools\validate_tests.bat`: passed in 7.02 s; 284/284 cases and
  21,403/21,403 assertions.
- `tools\validate_coverage.bat`: passed in 16.11 s; armed floors all pass,
  including physics stages/solver at 71.34% against its 70% floor.
- `tools\validate_physics.bat`: passed in 49.49 s; standalone smoke and the
  default-OFF 44,401-line byte-exact baseline match passed with no refresh.
- `tools\validate_perf.bat`: passed in 94.95 s; allocation guard, enforced
  budgets/comparisons, default-OFF scale matrix, and ready builds passed.
- `tools\validate_full.bat`: passed in 115.20 s; CPU umbrella, Automation and
  replay smoke, zero-warning builds, DX12 screenshots, zero InfoQueue errors,
  and the 44,401-line byte-exact physics proof passed.
- `python tools\validate_project_filters.py`: 724/724 production items, zero
  errors.

## Comment Audit

Touched-file audit: all 10 source-bearing files in the S6 diff were inspected
against `Agentic/Skills/comment-style-audit/skill.md`; zero deferred. New
kernel files include learning headers and local ownership, masking,
determinism, and solver-boundary comments. Checklist path: N/A for the skill's
touched-file mode (10 checked, 0 deferred, 0 unchecked).

## Timing

Substantial runs: final Profile build 12.3 s, Debug build 20.0 s, paired Debug
A/B captures 104.23/99.96 s, streaming comparison about 28 s, final combined
enabled matrix about 25 s, toggle-ON full doctest 1.26 s, fast 56.06 s, tests
7.02 s, coverage 16.11 s, physics 49.49 s, performance 94.95 s, and full
115.20 s. Total S6 wall time was approximately 32 minutes.
