# Handoff - Plan 04 RenderGraph Fatal Invariants

Date: 2026-07-09
Branch: `nightrunner-8th-july`
Worktree: `C:\SkullbonezCore`
Status: Pause after this handoff so the user can restart the computer.

## Completed Slice

Plan: `engine-cleanup-plans/04-error-handling-policy-reconciliation.md`
Step: 1.1, Lane F fatal-invariant conversion, RenderGraph sub-slice.

Commit:

- `69ac610a cleanup(04): fatalize render graph invariants`

Converted Plan 04 inventory rows 22 through 39 and 46 through 48 from
`throw std::runtime_error` to `SB_FATAL("RenderGraph", ...)`.

Touched source:

- `SkullbonezSource/Rendering/RenderGraph.cpp`
- `SkullbonezSource/Rendering/RenderGraph.h`

What changed:

- RenderGraph fixed-list reserve/resize/push capacity failures now use
  `SB_FATAL`.
- Resource capacity, transient descriptor/dimension contract, pass capacity,
  callback contract, transition capacity, mixed subresource state, subresource
  capacity, transient lifetime, transient pool/allocation capacity, resource
  handle, pass index, and concrete access guards now use `SB_FATAL`.
- `RenderGraph.h` includes `../Core/FatalError.h` and no longer includes
  `<stdexcept>`.
- `RenderGraph.cpp` no longer includes `<stdexcept>`.
- Success-path behavior is intended to be unchanged.

Evidence:

- Strict source throw statement count:
  `rg -n "^\s*throw\b" SkullbonezSource` -> `212`.
- Fatal macro invocation count:
  `rg -n "SB_FATAL\s*\(" SkullbonezSource` -> `72`.
- `rg -n "^\s*throw\b|std::runtime_error|stdexcept" SkullbonezSource\Rendering\RenderGraph.cpp SkullbonezSource\Rendering\RenderGraph.h`
  has no matches.
- Comment-style audit scope:
  `SkullbonezSource/Rendering/RenderGraph.cpp` and
  `SkullbonezSource/Rendering/RenderGraph.h`; checked 2, deferred 0.

## Validation

Required gate:

```bat
tools\validate_full.bat
```

Passed run:

- Exit code: 0
- Elapsed: `00:01:15.7980914`
- Log: `Agentic\Reports\validate_full_plan04_rendergraph_fatals_20260709.log`
- Key lines:
  - `VALIDATE_PROJECT_FILTERS: ALL PASSED`
  - `VALIDATE_RUNTIME_BOUNDARIES: ALL PASSED`
  - Profile and Debug builds: `0 Warning(s)`, `0 Error(s)`
  - `PASS: All source files correctly formatted.`
  - `DX12 validation errors: 0`
  - `PASS: DX12 screenshots match committed baselines.`
  - `VALIDATE_PHYSICS: ALL PASSED`
  - `VALIDATE_FULL: DEFAULT GATE PASSED`

One earlier gate attempt failed at the formatting check after builds passed,
because `RenderGraph.cpp` needed formatting. I formatted only
`RenderGraph.cpp` and `RenderGraph.h` with the repo clang-format executable,
then reran the full gate successfully.

## Plan/Session Updates

Updated:

- `engine-cleanup-plans/04-error-handling-policy-reconciliation.md`
- `engine-cleanup-plans/04-throw-site-lane-inventory.md`
- `Agentic/SessionState.md`

The Plan 04 inventory table remains the Step 0.1 baseline snapshot; progress is
recorded in the phase notes.

## Remaining Work

Overall goal remains active and incomplete.

Resume Plan 04 Step 1.1 with remaining Lane F groups. Likely next slices:

- DX12 capacity/lifetime fatal-invariant rows.
- Remaining runtime/scene/collection fatal-invariant rows.
- After Lane F is exhausted, proceed to Phase 2 probe assertions and Phase 3
  recoverable result boundaries per the plan.

Do not continue automatically after this handoff; the user requested a pause for
computer restart.

## Rubber-Duck Accounting

No rubber-duck review was run for this small incremental slice. The orchestrator
skill reserves rubber-duck review for major completed plans/checkpoints,
explicit user requests, or repeated failure loops.
