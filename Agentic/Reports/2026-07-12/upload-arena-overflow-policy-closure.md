# Upload Arena Overflow Policy Closure

Date: 2026-07-12
Plan: `upload-arena-overflow-policy` — 4/4 complete
Branch: `nightrunner-12th-july`

## Outcome

The 32 MiB per-frame DX12 upload arena now attributes bytes to constants,
dynamic vertices, instance data, texture rows, and debug/prediction overlays.
The Memory tab, architecture log, automation report, and graphics-stress output
expose high-water, cold-flush, steady-drop, and lane-specific truncation data.

Replay ribbons have a fixed 27,000-segment / 162,000-vertex ceiling derived
from the measured 21,568-segment legitimate maximum plus 25% headroom. A
10-second 200-ragdoll proof reached exactly the ceiling, remained visible and
stable, recorded 2,636,807 cumulative lane-specific omissions, and used
16,946,496 peak overlay bytes with zero flushes or backend drops. Full workload
measurements are in `Agentic/Reports/2026-07-12/upload-arena-measurements.md`.

## Overflow Contract

`RuntimeAllocationScope` now publishes lifecycle phase even when optional
allocation counting is off. Render, Replay, Physics, and SteadyGameplay
reservations that do not fit return zero and increment per-category,
power-of-two-rate-limited diagnostics. Startup, SceneLoad, BackendInit, Capture,
Diagnostics, and Shutdown retain the cold submit/wait/reset retry.

The production resolver and CPU tests share one branch implementation. Size and
alignment arithmetic saturates rather than wrapping. Failed constant uploads
skip the draw, failed instance uploads clear prior frame addresses, and
transient geometry checks its reservation before changing shader/pipeline state.

## Independent Review

Two read-only rubber-duck passes were run. The first found the default-off phase
bug, quota undercounting, global log-rate suppression, arithmetic and integration
proof gaps, and missing measurement evidence. The implementation made phase
always-on, continued bounded quota traversal to account omissions, added
per-category counters, shared the resolver with tests, and produced the A1/A2
artifacts. The 2m24s final pass found no implementation or acceptance blocker.
Its only non-blocking residual is that the small-scene adaptive-stride diagnostic
may conservatively count a skipped sample later found missing or stationary.

| Review pass | Duration | Result |
|---|---:|---|
| `upload-arena-overflow-policy-duck-01` | ~8m | Six actionable implementation/evidence findings, all resolved |
| `upload-arena-overflow-policy-duck-02` | 2m24s | Clear; no blocking findings |

## Final Validation

- `tools\validate_all_cpu_tests.bat` — passed in 38.88s; all four CPU owners,
  including 174 doctest cases / 3,966 assertions and upload policy tests.
- `tools\validate_dx12_renderer.bat` three consecutive times — passed in
  98.90s total; zero InfoQueue errors and all committed screenshots matched.
  Summaries: `20260712T061855Z`, `20260712T061934Z`, `20260712T061957Z`.
- `tools\validate_perf.bat` — completed in 33.05s; zero gameplay allocations,
  absolute budgets passed, and no DX12 or physics regression.
- `tools\run_graphics_stress.bat 1` — passed in 60.92s; crash-free PID-scoped
  timeout, descriptor churn PASS, and upload flush/drop counters stayed 0/0.
- `tools\validate_full.bat` — passed in 88.84s; formatting and metadata clean,
  CPU lanes passed, zero-warning builds, DX12 screenshots/InfoQueue clean, and
  the 44,401-line physics baseline byte-exact.

## Comment Audit

Touched-file audit completed against
`Agentic/Skills/comment-style-audit/skill.md`: 21 source-bearing files checked,
0 deferred, 0 unchecked. Learning headers were present in every file; phase,
overflow, stale-address, attribution, quota, and automation-report invariants
were refreshed next to the affected code.
