# Source Modernization MZ4 Performance Blocker

Date: 2026-08-03
Plan: `Agentic/Plans/TODO/source-modernization-sweep.md`
Status: BLOCKED — MZ4 remains unchecked
Owner needed: performance-baseline owner

## Outcome

The naming-and-idiom implementation is functionally clean, but the mandatory
committed performance comparison is red. This plan has no baseline-refresh or
threshold-change authority, so MZ4 cannot close and the plan remains 4/5.

## Passing Evidence

- `tools\validate_physics.bat` passes byte-exactly with no baseline movement.
- `tools\validate_physics_deep.bat` passes with no baseline movement.
- Debug and Profile x64 builds pass with zero warnings or errors.
- Formatting and `git diff --check` pass.
- The final touched-source comment audit is 41/41 with zero deferred files.
- The strict function-complexity inventory passes after refreshing the exact
  current-body digests for the two source bodies changed by cast spelling.
- The strict compiled-symbol reachability inventory passes after refreshing
  the exact current signature for `Terrain::TryCreatePhysicsFromHeightMap`.
- Every performance run passes its absolute budgets and improves retained
  process memory by approximately 5 MB. The structural selected-ball path and
  allocation guard also pass.
- Explicit preprocessing proves the restored intrinsics predicate in all four
  supported cases: default Profile enabled, default Debug disabled, forced-on
  Debug enabled, and forced-off Profile disabled.

## Repeated Performance Failure

The final run began on an idle host sampled at 6.3%, 11.1%, and 6.3% total CPU,
with no Python, engine, compiler, linker, or MSBuild process left running. Its
fresh artifacts still fail the committed comparisons:

| Artifact | Blocking comparison |
|---|---|
| DX12 | `Frame.avg` +12.7% (11% threshold) |
| DX12 | `Frame.p50` +13.4% (12% threshold) |
| DX12 | `Frame/Physics/Broadphase.avg` +43.6% (displayed 44% threshold; the comparator's unrounded threshold rejects it) |
| Physics bench | `Frame.avg` +19.9% (15% threshold) |

Earlier clean reruns varied in the number and identity of failed timing rows,
including whole-frame, input, VSync wait, physics, and broadphase values. Ten
stale diagnostic Python processes from completed Dense Pile investigations were
identified by exact command line and stopped before the final quiet-host run;
the gate still failed. The byte-exact physics and deep-physics results, broad
timing movement, and syntax/name-only source diff do not justify attributing the
measurements to a functional source change, but that inference does not waive a
mandatory red gate.

On this PowerShell host the unmodified comparator also encounters a CP-1252
`UnicodeEncodeError` while printing its emoji legend. Setting `PYTHONUTF8=1`
allows the exact committed comparator to finish and yields the failures above;
the encoding defect is therefore not the reason the timing verdict is red.

## Required Owner Decision

Do not move a baseline or threshold under this plan. To unblock MZ4, the
performance-baseline owner must choose one of these paths:

1. provide a controlled measurement environment in which the unchanged
   `tools\validate_perf.bat` comparison passes; or
2. explicitly authorize a separately reviewed performance-baseline transition
   after determining that current accepted engine performance, rather than this
   syntax/name-only diff, owns the drift; or
3. activate a separate performance investigation plan that identifies and
   repairs a source-owned regression before MZ4 is rerun.

After that decision, rerun `tools\validate_perf.bat`, then the complete final
`tools\validate_full.bat` gate, and obtain the final independent ACCEPT before
checking MZ4 and deleting the completed plan.
