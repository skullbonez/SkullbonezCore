# Allocation Namespace Restoration Closure

Date: 2026-07-20
Branch: `nightrunner-20th-july`
Plan: `allocation-namespace-restoration` (A0, 1/1)

## Outcome

The plan is complete. The allocation policy physically owned by
`SkullbonezSource/Core/Allocation/` now declares
`SkullbonezCore::Core::Allocation`, and every source, test, architecture
assertion, and allocation-policy allowlist consumer uses that owner-true
namespace.

No compatibility namespace, alias, forwarding shim, type rename, capacity
change, phase change, replay privilege change, allocation/free pairing change,
or policy behavior change was introduced. The existing
`FATAL[Runtime/Allocation]` diagnostic owner label remains intentionally: it
names the runtime enforcement lane, not a C++ namespace or dependency edge.
No baseline, golden, scene, shader, screenshot, or authored-data artifact was
refreshed.

## Exact Proofs

Every final-source proof returned zero rows:

| Proof | Rows |
|---|---:|
| `SkullbonezCore::Runtime::Allocation` or relative `Runtime::Allocation` in source/tests | 0 |
| unqualified bare `Allocation::` in source/tests | 0 |
| old local `RuntimeAllocation` aliases or uses | 0 |
| compatibility namespace or forwarding shim | 0 |

The four allocator declaration files were inspected directly and declare the
`SkullbonezCore::Core::Allocation` nesting. The retained fatal diagnostic label
was checked separately and has one expected row.

## Policy Allowlist Correction

The first repository allocation-policy scan found seven issues because three
approved reserve-growth patterns still named the deleted namespace: four live
findings were no longer recognized and three old patterns were stale. Only
those exact strings in `tools/allocation_policy_allowlist.json` were updated;
owner, phase, reason, cap, and removal-plan metadata stayed unchanged.

The corrected self-test passed in 0.15s. The corrected repository scan passed
in 9.11s with `scanned=404`, `direct_heap_findings=39`,
`dynamic_stl_member_findings=147`, `stl_growth_findings=647`, and
`allowlist_errors=0`.

## Comment Quality

All 47 touched source-bearing files were inspected against the repository
comment-style guide. Every file retained its required learning header, the
namespace-only body edits exposed no new ownership, lifetime, invariant, or
hazard requiring a local teaching comment, and zero files were deferred.

## Validation

Targeted Profile, Debug, and Automation builds passed in 51.81s with zero
warnings/errors after correcting omitted bare references and two obsolete DX12
namespace aliases.

The final mapped gates all passed:

- `tools\validate_fast.bat` — 58.23s final log span; 48 staged candidates,
  zero size violations, zero warnings/errors.
- `python tools\check_allocation_policy.py --self-test` — 0.15s, pass.
- `python tools\check_allocation_policy.py --repo .` — 9.11s, pass with zero
  allowlist errors.
- `tools\validate_physics.bat` — 53.81s; deterministic baseline pass and zero
  build warnings/errors.
- `tools\validate_replay_visual_fidelity.bat` — exactly one invocation in
  442.73s; one engine process, one prediction generation, 2,401 ticks, all
  positive and false-pass controls green.
- `tools\validate_perf.bat` — 102.46s log span; complete with zero build
  warnings/errors.
- `tools\validate_dx12_renderer.bat` — 54.18s; zero DX12 InfoQueue errors and
  all three committed image comparisons accepted.
- `tools\run_graphics_stress.bat 1` — 61.51s; exact PID 47208 completed the
  bounded run and was closed by the PID-scoped timeout without a crash.
- `tools\validate_full.bat` — 148.04s log span; mandatory CPU/coverage and all
  five engine-process lanes passed, DX12 validation remained at zero, and the
  44,401-line physics baseline matched byte-for-byte.

The desktop tool surface could not expose a separate visible console, so gate
output was mirrored under `TestOutput/logs/a0_*.log`.

## Handoff

The completed one-task plan leaves the active/future ledger under inventory
rule 4, reducing the denominator from 48 to 47. Start
`physics-facade-unification` F0 next. The only remaining semantic exception
from dependency-direction closure is the `Core/Profiler.h` Rendering/Text seam,
whose deletion condition remains bound to Render HAL M0/M5.
