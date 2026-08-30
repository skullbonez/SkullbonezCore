# SDT4 Closure Evidence

Date: 2026-08-30
Branch: `codex/replay-capture-bugfixes`
Final measured implementation: `4894b9f0ad873e1e5247be0786be5aebb9af2c06`

## Selection And Rule Parity

The Git-derived pull request 162 reference uses base
`7376c69ae6aa32c570dc1ee95fa96e958ba1de42` and head
`26baa61e8b92173db46200c9bd306a344b31364f`. Both the serial and automatic
paths selected 79 files and 623 distinct compile contexts. The immutable
work-item identity SHA-256 is
`d9e4b117d81e3ae8ca5f29376335f7c219bdc47cd94c4438d4cb4a7a0da5db77`.

Every context completed exactly one Tidy analysis and one batched Query
analysis. The stable stdout, after excluding the timing summary, remained
byte-identical at SHA-256
`d45bac50d80adc91c99722cc51a91470720fb865d84fe45afbc73e8071605e4e`.
There were no findings or infrastructure errors.

| Reference path | Tidy | Query | Wall time | CPU time | Average active cores | Peak LLVM children | Peak committed bytes | Peak working set bytes |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Pre-batching serial | 623 | 2,492 | 3,274.505 s | not captured | not captured | 1 | 669,978,624 | not captured |
| Batched serial (`--jobs 1`) | 623 | 623 | 1,488.086 s | 1,134.766 s | 0.762 | 1 | 678,367,232 | 665,890,816 |
| Repaired automatic (`--jobs auto`) | 623 | 623 | 440.016 s | 1,140.719 s | 2.592 | 4 | 1,975,234,560 | 1,989,603,328 |

The deterministic-prefix repair leaves the one-worker clean path unchanged.
The repaired automatic path is 3.38 times faster than the batched serial path.
No stderr, paging symptom, child termination, or runner instability appeared.

## Final Local Checks

- Self-test with `--jobs 1`: 8 sources, 8 contexts, 4 Tidy processes, 7 Query
  processes, zero findings/errors, 4.560 seconds.
- Self-test with automatic jobs: the same selection, processes, and result,
  zero findings/errors, 4.538 seconds.
- Final automatic repository scan: 21 files, 154 contexts, 154 Tidy and 154
  Query processes, four observed workers, zero findings/errors, 128.044
  seconds.
- `tools\validate_fast.bat --preflight-only` passed all nine stages. The exact
  terminal invocation repeated this result at 128.096 seconds for source
  design and built Profile with zero warnings/errors.
- `tools\validate_all_cpu_tests.bat` passed all six lanes. The exact terminal
  invocation refreshed the unit inventory to 892 passed plus 1 skipped, or
  893 discovered cases, with 2,692,862 passing assertions.
- Coverage passed at 96.05% Maths, 88.84% Core primitives, 84.75% Physics
  stores, 90.43% Physics stages/solver, 84.19% Replay codecs, 89.73% startup,
  94.66% configuration/schema, 89.17% Runtime input/interaction, 95.74% scene
  logic, and 89.75% Replay value seams. Whole-product output was 73.29% and was
  not used as a gate.
- Runtime interaction Debug/Release, scene parser, UI boundary, and DX12
  architecture suites passed. No test, coverage floor, Physics evidence, or
  golden file changed.

## Hosted Measurements

Both clean `windows-2022` runs used exact SHA
`4894b9f0ad873e1e5247be0786be5aebb9af2c06` and retained one live
source-design invocation in the mandatory CPU lane.

| Run | Mandatory job | Source design | Preflight | CPU umbrella | Complete job | Result |
|---|---:|---:|---:|---:|---:|---|
| [33274650785](https://github.com/skullbonez/SkullbonezCore/actions/runs/33274650785) | `99159142074` | 177.089 s | 10m46.496s | 6m07.210s | 18m15s | success |
| [33274651985](https://github.com/skullbonez/SkullbonezCore/actions/runs/33274651985) | `99159145727` | 218.936 s | 12m11.588s | 6m56.046s | 20m14s | success |

Each source summary reported 21 files, 154 contexts, 154 Tidy processes, 154
Query processes, four configured and observed workers, and zero findings or
infrastructure errors. Both source phases are below 15 minutes and both complete
jobs are below 30 minutes.

## Independent Review

The first independent pass found that rolling replenishment could admit a
different tail before an already-active infrastructure failure became visible.
Commit `4894b9f0` changed admission to fixed deterministic prefixes and added a
fast-failure/slow-failure control with more contexts than workers. The control
compares rendered errors, exit class, admitted count, started identities,
not-admitted identities, and process counts.

The same reviewer independently reversed simultaneous failures with five
items and two workers. Both orders admitted only items 0 and 1, chose item 0 as
the canonical first failure, marked items 2 through 4 not admitted, produced
byte-identical errors, and returned infrastructure exit class 2. The follow-up
verdict was blocker-free with no remaining concurrency or diagnostic finding.

## Exactly-Once Terminal Result

`tools\agent_validate.bat --plan-completion` ran exactly once at the measured
implementation and returned exit code 1. It passed Debug, Physics/Profile, and
Automation builds with zero warnings/errors; mandatory preflight; all six CPU
lanes; Automation replay/prediction smoke; shader freshness; and DX12 InfoQueue
with zero validation errors. It stopped at the DX12 screenshot comparison:

- `water_ball_test`: average difference 4.9171, maximum 128, 471,156 pixels
  over 10;
- `solver_smoke`: average difference 4.0153, maximum 90, 397,318 pixels over
  10; and
- `space_three_body`: exact pass with zero differing pixels.

The terminal summary is
`TestOutput/validation/dx12_renderer/20260829T212240Z/summary.json`. The exact
pre-plan commit `456db76e7` reproduced the same inherited stop when its
changed-file base was pinned to itself: water metrics were identical, solver
metrics were 4.0154/90/397,324, and the three-body scene was again exact. Its
summary is
`TestOutput/validation/dx12_renderer/20260829T212423Z/summary.json` in the
isolated pre-plan worktree. No screenshot or other baseline was refreshed, and
the terminal command was not rerun.

The strict two-generation allocation probe also remains inherited: exact
pre-plan commit `456db76e7` and the final implementation both report
`gameplay_violations=280` and `policy_violations=0`. No allocation baseline or
reserve policy changed.
