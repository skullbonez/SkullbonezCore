# Code-Level Red Flags Remediation Closure

Date: 2026-07-18
Plan: `code-level-red-flags-remediation`, task C6
Branch: `nightrunner-17th-july`
Result: complete at 7/7; no baseline, golden, screenshot, or coverage-floor refresh

## Closure Verdict

The round-6 code-level findings are closed:

- renderer text-batch state is fixed-capacity and instance-owned;
- profiler and Debug lock validation no longer resolve singletons on their
  ratified hot paths, while `EngineLog` remains the documented cold/fatal
  process static;
- `PhysicsScene` exposes a narrow const read boundary and has no friend edge;
- fixed-step catch-up is capped at five ticks with tested dropped-time facts
  and cumulative diagnostics; and
- Release and `Profile-WPO` retain LTCG while the three contact/solver
  arithmetic translation units compile as native non-WPO objects.

The source-bearing tasks completed their mapped comment audits. The final C6
change is documentation and ledger cleanup only.

## Independent Review

The independent review found one blocking evidence gap: the original C5
performance and byte-exact gates exercised Debug or ordinary Profile rather
than the changed `Profile-WPO` item configuration. C5 was reopened.

The remediation ran a temporary all-WPO optimized control, restored the exact
three-TU boundary, completed two warning-free clean `Profile-WPO` rebuilds,
and compared two optimized replay-hash captures. Presentation and solver each
had 2,402 non-comment rows with zero differences; normalized SHA-256 values
were identical across runs. Raw hashes differed only in two comment headers
whose scene paths used `/` versus `\`. The optimized A/B measured -0.44%
whole-frame average and +0.0031 ms physics average, safely inside the absolute
budget. The repeat reviewer closed the P1 and reported no remaining blocker.
Full commands, timings, hashes, and artifacts are recorded in
`code-level-red-flags-lto-determinism.md`.

## Final Gates

| Command | Time | Result |
|---|---:|---|
| `tools\validate_full.bat` | 134.228s | Passed mandatory CPU umbrella, 284/284 doctests and 21,408/21,408 assertions, all standalone CPU lanes, Automation/replay smoke, DX12 with zero validation errors and accepted captures, and the 44,401-line physics oracle byte-exact. |
| `tools\run_graphics_stress.bat 1` | 60.826s | PID 12296 ran the bounded DX12 stress lane for one minute and exited successfully through the script's PID-scoped timeout. |

`validate_full` automatically ran `tools\validate_coverage.bat`. All ten
ratified subsystem floors passed: Maths 86.67%, core primitives 88.18%, physics
stores 70.95%, physics stages/solver 71.20%, replay codecs 75.79%, startup
91.82%, config/schema 94.77%, runtime input/interaction 74.56%, scene logic
97.22%, and replay value seams 82.23%.

Evidence logs:

- `TestOutput/validation/agent_logs/red_flags_c6_validate_full.log`
- `TestOutput/validation/agent_logs/red_flags_c6_graphics_stress.log`
- `TestOutput/validation/agent_logs/red_flags_c5_profile_wpo_oracle_rebuild_1.log`
- `TestOutput/validation/agent_logs/red_flags_c5_profile_wpo_oracle_rebuild_2.log`
- `TestOutput/validation/agent_logs/red_flags_c5_profile_wpo_oracle_1.csv`
- `TestOutput/validation/agent_logs/red_flags_c5_profile_wpo_oracle_2.csv`
- `TestOutput/validation/agent_logs/red_flags_c5_profile_wpo_oracle_1.solver.csv`
- `TestOutput/validation/agent_logs/red_flags_c5_profile_wpo_oracle_2.solver.csv`

The active/future implementation ledger is now empty. The separately recorded
GPU-hosted CI activation lane remains externally blocked and excluded from the
portfolio denominator; it is not unfinished local implementation work.
