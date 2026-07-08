# Engine Cleanup Restart Handoff - 2026-07-09

## Stop point

Paused by user request after finishing and committing the current Plan 05 slice.
Do not start the next cleanup step until the user restarts and resumes the goal.

Branch:

- `nightrunner-8th-july`

Latest local commits:

- `d080650e cleanup(05): test replay solver restore`
- `83b1084f cleanup(05): test runtime input bindings`
- `7da356ad docs(05): map behavioral coverage gaps`
- `b3737a92 docs: reconcile engine cleanup ledgers`

## What just landed

Plan 05 behavioral coverage now has:

- Phase 0 coverage map complete.
- Phase 1 input command-table tests complete.
- Phase 2 replay solver-sample restore determinism test complete.

The replay slice changed `SkullbonezTests/TestDeterminism.cpp` to capture a
micro-world into a `ReplaySolverFrameSample`, restore from that sample, advance
the same fixed-step window, recapture the future sample, and compare replay body
fields plus selected solver-world vectors byte-for-byte.

Plan ledgers updated:

- `engine-cleanup-plans/05-behavioral-test-coverage.md`
- `engine-cleanup-plans/00-EXECUTION-GUIDE.md`
- `engine-cleanup-plans/README.md`

Current test inventory recorded in Plan 05:

- 52 `TEST_CASE`s across 20 test files.
- 4 link-stub files still remain.

## Validation evidence

Input-binding slice:

- `tools\validate_tests.bat`
- Log: `TestOutput\agent_logs\plan05_input_bindings_validate_tests_20260709_083801.log`
- Result: project filters 0 errors, `SKULLBONEZ_TESTS` Profile 0 warnings/0 errors, 51/51 doctest cases and 1304/1304 assertions passed.
- Extra broad gate because production runtime/project files changed:
  `tools\validate_full.bat`
- Log: `TestOutput\agent_logs\plan05_input_bindings_validate_full_20260709_083509.log`
- Result: Profile/Debug builds passed, DX12 validation errors 0, DX12 screenshots matched baselines, physics CSV matched byte-exactly.

Replay restore slice:

- `tools\validate_tests.bat`
- Log: `TestOutput\agent_logs\plan05_replay_restore_validate_tests_20260709_084459.log`
- Result: project filters 0 errors, `SKULLBONEZ_TESTS` Profile 0 warnings/0 errors, 52/52 doctest cases and 1425/1425 assertions passed.
- Post-validation source edits were comments only.

Comment audit:

- `SkullbonezSource/Runtime/InputController.Bindings.h`
- `SkullbonezSource/Runtime/InputController.Bindings.cpp`
- `SkullbonezSource/Runtime/RunInput.cpp`
- `SkullbonezTests/TestRuntimeInputBindings.cpp`
- `SkullbonezTests/TestDeterminism.cpp`
- Deferred: 0.

## Current open work

Next non-human-gated Plan 05 step:

- `engine-cleanup-plans/05-behavioral-test-coverage.md` step 3.1:
  add physics invariant/property tests for penetration tolerance, bounded
  damping energy, and sleep/wake transitions.
- Required gate: `tools\validate_tests.bat`.

Remaining Plan 05 work after that:

- Step 4.1: convert or delete each `*LinkStubs.cpp`.
- Acceptance remains open until input/replay/scene logic coverage, physics
  invariants, zero link stubs, and top-risk coverage movement are all satisfied.

Human-gated cleanup still open:

- Plan 11 RenderGraph decision remains human-gated.
- Plan 13 FAC-005 remains open on a public physics API planning decision.
- Plan 03 and Plan 07 remain sign-off/decision gated.

## Resume checklist

1. Re-run the repo startup contract from `AGENTS.md`.
2. Check `git status --short --branch`.
3. Confirm this branch is at or after `d080650e`.
4. If resuming Plan 05, start at step 3.1 and keep the gate to
   `tools\validate_tests.bat`.
5. Do not continue Plan 11 until the human RenderGraph decision is made.
