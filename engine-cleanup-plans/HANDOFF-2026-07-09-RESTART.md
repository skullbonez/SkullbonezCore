# Engine Cleanup Restart Handoff - 2026-07-09

## Stop point

Paused by user request after finishing and committing the current Plan 05 slice.
Do not start the next cleanup step until the user restarts and resumes the goal.

Branch:

- `nightrunner-8th-july`

Latest implementation commit before this handoff:

- `6ab7eaef cleanup(05): test physics invariants`

Recent prior cleanup commits:

- `d080650e cleanup(05): test replay solver restore`
- `83b1084f cleanup(05): test runtime input bindings`
- `7da356ad docs(05): map behavioral coverage gaps`
- `b3737a92 docs: reconcile engine cleanup ledgers`

## What just landed

Plan 05 behavioral coverage now has:

- Phase 0 coverage map complete.
- Phase 1 input command-table tests complete.
- Phase 2 replay solver-sample restore determinism test complete.
- Phase 3 physics invariant/property tests complete.

The physics invariant slice changed
`SkullbonezTests/TestDeterminism.cpp` to add three property-style micro-world
tests:

- settled dynamic bodies stay within the configured terrain penetration
  tolerance, checked through body bounds and terrain manifold diagnostics;
- no-gravity fluid damping does not increase total kinetic energy;
- authored velocity wakes a seeded sleeping body and advances it on the next
  fixed step.

Plan ledgers updated:

- `engine-cleanup-plans/05-behavioral-test-coverage.md`
- `engine-cleanup-plans/00-EXECUTION-GUIDE.md`
- `engine-cleanup-plans/README.md`

Current test inventory recorded in Plan 05:

- 55 `TEST_CASE`s across 20 test files.
- 4 link-stub files still remain.

## Validation evidence

Physics invariant slice:

- `tools\validate_tests.bat`
- Log: `TestOutput\agent_logs\plan05_physics_invariants_validate_tests_20260709_085357.log`
- Result: project filters 0 errors, `SKULLBONEZ_TESTS` Profile 0 warnings/0
  errors, 55/55 doctest cases and 1504/1504 assertions passed.
- Runtime: 5.2 seconds.

Replay restore slice:

- `tools\validate_tests.bat`
- Log: `TestOutput\agent_logs\plan05_replay_restore_validate_tests_20260709_084459.log`
- Result: project filters 0 errors, `SKULLBONEZ_TESTS` Profile 0 warnings/0
  errors, 52/52 doctest cases and 1425/1425 assertions passed.

Input-binding slice:

- `tools\validate_tests.bat`
- Log: `TestOutput\agent_logs\plan05_input_bindings_validate_tests_20260709_083801.log`
- Result: project filters 0 errors, `SKULLBONEZ_TESTS` Profile 0 warnings/0
  errors, 51/51 doctest cases and 1304/1304 assertions passed.
- Extra broad gate because production runtime/project files changed:
  `tools\validate_full.bat`
- Log: `TestOutput\agent_logs\plan05_input_bindings_validate_full_20260709_083509.log`
- Result: Profile/Debug builds passed, DX12 validation errors 0, DX12
  screenshots matched baselines, physics CSV matched byte-exactly.

Comment audit:

- `SkullbonezTests/TestDeterminism.cpp`
- Prior Plan 05 touched files already audited:
  `SkullbonezSource/Runtime/InputController.Bindings.h`,
  `SkullbonezSource/Runtime/InputController.Bindings.cpp`,
  `SkullbonezSource/Runtime/RunInput.cpp`,
  `SkullbonezTests/TestRuntimeInputBindings.cpp`
- Deferred: 0.

## Current open work

Next non-human-gated Plan 05 step:

- `engine-cleanup-plans/05-behavioral-test-coverage.md` step 4.1:
  convert each `*LinkStubs.cpp` into a real test or delete it.
- Required gate: `tools\validate_tests.bat`.

The remaining link-stub files are:

- `SkullbonezTests/TestDiagnosticsLinkStubs.cpp`
- `SkullbonezTests/TestReplayRecorderLinkStubs.cpp`
- `SkullbonezTests/TestSceneParserLinkStubs.cpp`
- `SkullbonezTests/TestTerrainLinkStubs.cpp`

Plan 05 acceptance remains open until the link-stub count is 0.

Human-gated cleanup still open:

- Plan 11 RenderGraph decision remains human-gated.
- Plan 13 FAC-005 remains open on a public physics API planning decision.
- Plan 03 and Plan 07 remain sign-off/decision gated.

## Resume checklist

1. Re-run the repo startup contract from `AGENTS.md`.
2. Check `git status --short --branch`.
3. Confirm this branch is at or after `6ab7eaef`.
4. If resuming Plan 05, start at step 4.1 and keep the gate to
   `tools\validate_tests.bat`.
5. Do not continue Plan 11 until the human RenderGraph decision is made.
