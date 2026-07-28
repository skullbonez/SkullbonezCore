# Determinism Terrain Fixture Isolation Closure

Date: 2026-07-28
Plan: `Agentic/Plans/DONE/determinism-terrain-fixture-isolation.md`
Phases: TF0-TF2 complete
Impact area: Physics tests, startup probe terrain lifetime, test order independence

## Outcome

The terrain-bearing determinism tests no longer share mutable terrain owners.
Each engine now borrows from an explicit per-test config/terrain lifetime, and
the startup lifecycle probe destroys its retained-view engine before the
terrain and config that back the view.

TF2 adds an in-process reconstruction witness in
`SkullbonezTests/TestDeterminism.cpp`. Each invocation constructs one cold
fixture, seeds three in-bounds bodies, runs 120 fixed ticks, captures the exact
solver hash, and destroys the engine/terrain/config before returning. The
sequence is:

```text
flat -> deep -> flat -> deep -> deep -> flat -> flat
```

That sequence covers all four predecessor transitions: flat-to-flat,
flat-to-deep, deep-to-flat, and deep-to-deep. Every flat hash is byte-identical
to every other flat hash, every deep hash is byte-identical to every other deep
hash, and the flat/deep hashes differ. The final focused case passes 92/92
assertions.

## Deletion And Isolation Proof

Both final-source searches returned no rows:

```powershell
rg -n "FlatTestTerrain|DeepSpaceTestTerrain|PredictionSeedTestTerrain|FlatCoverageTerrain|Terrain\s*\*\s*terrain\s*=|ClearTerrainView\(\)" SkullbonezTests/TestDeterminism.cpp SkullbonezTests/TestPhysicsHandles.cpp SkullbonezTests/TestTerrain.cpp
rg -n "static\s+.*Terrain|static\s+Terrain" SkullbonezTests/TestDeterminism.cpp SkullbonezTests/TestPhysicsHandles.cpp SkullbonezTests/TestTerrain.cpp
```

The isolated validation worktree's tracked diff contained only
`SkullbonezTests/TestDeterminism.cpp`. No baseline, golden, config, schema,
performance artifact, body layout, or SoA storage changed.

## Randomized Order Proof

The embedded doctest runner supports randomized ordering. Both required
final-source determinism-suite runs passed:

| Command | Result |
|---|---|
| `Profile\SKULLBONEZ_TESTS.exe --source-file="*TestDeterminism.cpp" --order-by=rand --rand-seed=28072026 --no-breaks=true --duration=true` | PASS in 1.11 s; 24/24 cases, 2,385,028 assertions |
| `Profile\SKULLBONEZ_TESTS.exe --source-file="*TestDeterminism.cpp" --order-by=rand --rand-seed=731942 --no-breaks=true --duration=true` | PASS in 1.13 s; 24/24 cases, 2,385,028 assertions |

An extra whole-suite randomized run was not an acceptance gate. Seed 28072026
passed all 438 cases; seed 731942 exposed 11 fingerprint failures in
`TestUIDrawValues.cpp`. The independent reviewer ruled that out of TF2 because
both required randomized determinism runs and every normal broad gate pass, and
TF2 changes only `TestDeterminism.cpp`. Its pre-TF2 provenance was not
established in this plan, so the observation is retained as separate
order-dependency debt rather than labelled a pre-existing defect or used to
refresh UI fingerprints.

## Validation

Validation ran in detached worktree
`C:\SkullbonezCore-terrain-tf2-validation`, with the owner's three warm-start
files excluded:

| Proof | Result |
|---|---|
| Focused reconstruction witness | PASS; 1/1 case, 92/92 assertions |
| `tools\validate_tests.bat` | PASS in 16.18 s; 438/438 cases, 2,419,221 assertions |
| `tools\validate_format.bat` | PASS in 44.34 s; 571 implementations and 317 headers clean |
| `tools\validate_project_filters.bat` | PASS in 2.84 s; 787/787 production rows |
| `tools\validate_dependency_graph.bat` | PASS in 3.55 s; zero findings |
| Strict authority-free aggregate inventory | PASS in 23.74 s; every bounded row ruled |
| Extraction-scar inventory | PASS in 28.04 s; 1/1 current finding ruled |
| Strict wide-signature inventory | PASS in 28.03 s; every 12-or-more trigger row ruled |
| `tools\validate_physics.bat` (final source) | PASS in 24.13 s; 44,401-line CSV byte-exact, zero warnings/errors |
| `tools\validate_physics_deep.bat` | PASS in 105.55 s; core, bullet, shooting, chaos, known-signature, and query artifacts exact |
| `tools\validate_full.bat` (final source) | PASS in 284.23 s; 438 tests, CPU/coverage and runtime lanes, accepted DX12 checks, byte-exact Physics |

Generated logs remained under
`TestOutput/validation/terrain_tf2` in the disposable validation worktree. The
following sizes and SHA-256 digests preserve the evidence after worktree
removal:

| Log | Bytes | SHA-256 |
|---|---:|---|
| `post_review_focused.log` | 383,676 | `e640bbbca5cabbf09e771862b62b8c1cf75aa56cbb64eec9076ad2471a381851` |
| `validate_tests_final.log` | 3,694,946 | `32a75cfa978f91d6bb85ea7eba71a130f42b120bd87b5b330f00bb8edcc7193e` |
| `determinism_random_seed_28072026_final.log` | 2,134,334 | `ce5f7e3a10297b67193b90cf908138f6fcc8241d4ecf06f41befe1423c2253fa` |
| `determinism_random_seed_731942_final.log` | 2,134,334 | `9addc27673fa6afd75ba8419bd6e77b43bb3cb0bc4bae95f33d0342346ea96e3` |
| `format.log` | 1,232 | `33651d3c3c8ffced6ac4021ad2a2866586f5b47264ea71b805f708ef312e355c` |
| `filters.log` | 874 | `2cf3b01048c4e52e357fed4d312223c33ae06b77c8dd3408e5842a9804286fa1` |
| `dependency.log` | 596 | `1b32f113689849e64a4b735e2fb6b47b953af7df8955b94035917df45e208f2d` |
| `aggregate_strict.log` | 6,554 | `3c4126b779ef7455a5f5859cad66266e19d997f21f3738e8dd25ce8f0a87d16a` |
| `scar.log` | 400 | `10f0487e4b0dea219793d98b52e5bd01fcc2af58b2dcc3f5413fad2e9cb53d46` |
| `wide.log` | 414,998 | `35536b8d0fadfb9c5bdaa5f31520f9bc4406f5ab3c9f635fb6464c3d14b50d1e` |
| `validate_physics_final.log` | 577,050 | `526d31bb15bce50f8ce04221af53a723128d2416a31ad34bf70c466c69ccfe40` |
| `validate_physics_deep.log` | 1,168,342 | `1f54d5fc3fa7b7791b1119eba77fc8e20a3bf5433278f16ae48a0e86dea60e3b` |
| `validate_full_final.log` | 4,321,822 | `0ed48c42a020ef416c83bee1c2a35dedc93019e4067616367b6b61390729ac56` |
| `random_seed_28072026.log` | 3,489,654 | `b2758d4c3bb7495c9aa9e89e639f3b8ade00604cbced6d58fc03a69251486668` |
| `random_seed_731942.log` | 3,495,138 | `9700483f8de3f0084672ee1ee65fff2d509f27cb4c8a31f89f2615e3a90ab347` |

## Comment Audit

Plan-wide checklist/evidence path: this closure report.

- Checked: 4/4 source-bearing files touched across TF1-TF2.
- Deferred: 0.
- Unchecked: none.
- `TestDeterminism.cpp` was re-inspected after TF2. Its learning header names
  the reconstruction invariant and the witness explains destruction and all
  predecessor transitions next to the exact comparisons.
- `TestPhysicsHandles.cpp`, `TestTerrain.cpp`, and
  `Runtime/Startup/StartupProbeHarnesses.cpp` are unchanged since their clear
  4/4 TF1 audit; their terrain/config/engine lifetime claims still match source.
- Every repository-relative `Related:` entry resolves. No wording requires
  human approval.

## Independent Review

The first TF2 review found one blocking false-pass: flat/deep/flat/deep covered
only cross-kind predecessors. The reviewer required all four predecessor
transitions before the order-independence claim could stand. The seven-run
sequence closed that blocker.

The follow-up review returned no blockers and no non-blocking TF2 findings. It
explicitly passed aggregate ownership, capability slices, extraction scars,
rename evasion, false-claim review, and wide-signature disposition. Residual
risk is limited to a theoretical 64-bit hash collision and analytic-terrain
coverage rather than a cached heightfield; both were accepted for this scoped
exact-hash witness.

## Closure

All three phases are complete. No owner question remains for this plan. The
Principal Engineer Feedback Campaign removes this completed plan from the live
ledger under rule 4, leaving dependency-proof-generation at 0/3 (0%).
