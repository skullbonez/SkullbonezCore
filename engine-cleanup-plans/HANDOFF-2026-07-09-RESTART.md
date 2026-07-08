# Engine Cleanup Restart Handoff - 2026-07-09

## Stop Point

Paused by user request after finishing, validating, and committing the current
Plan 05 slice. Do not start the next cleanup step until the user restarts and
resumes the goal.

Branch:

- `nightrunner-8th-july`

Latest implementation commit before this handoff:

- `0200ec48 cleanup(05): behavioral coverage step 4 - kill link stubs`

Recent prior cleanup commits:

- `6ab7eaef cleanup(05): test physics invariants`
- `d080650e cleanup(05): test replay solver restore`
- `83b1084f cleanup(05): test runtime input bindings`
- `7da356ad docs(05): map behavioral coverage gaps`
- `b3737a92 docs: reconcile engine cleanup ledgers`

## What Just Landed

Plan 05 behavioral coverage is complete:

- Phase 0 coverage map complete.
- Phase 1 input command-table tests complete.
- Phase 2 replay solver-sample restore determinism test complete.
- Phase 3 physics invariant/property tests complete.
- Phase 4 link-stub removal complete.

The link-stub slice removed all four test-owned link shim translation units:

- `SkullbonezTests/TestDiagnosticsLinkStubs.cpp`
- `SkullbonezTests/TestReplayRecorderLinkStubs.cpp`
- `SkullbonezTests/TestSceneParserLinkStubs.cpp`
- `SkullbonezTests/TestTerrainLinkStubs.cpp`

Replacement coverage/source linkage:

- Added `SkullbonezTests/TestAssetSystem.cpp` for asset-library source lookup,
  logical id/name retention, path resolution, and built-in asset library
  registration.
- Added `SkullbonezTests/TestTerrain.cpp` for the real flat-slope terrain
  height, plane, fluid-height, and bounds contract.
- Added `SkullbonezTests/TestReplayRecorderFullCaptureBoundary.cpp` for the
  replay recorder solver-mirror path with explicit full-capture owner hook
  guards.
- Added `SkullbonezTests/TestRenderResourceDoubles.h` so terrain-dependent unit
  tests can construct production terrain without initializing DX12.
- Updated `SkullbonezTests/TestDeterminism.cpp` to use the shared render-resource
  double.
- Updated `SKULLBONEZ_TESTS.vcxproj` and `.filters` to remove link stubs and
  link real `AssetSystem.cpp`, `Terrain.cpp`, `FatalError.cpp`, and `Log.cpp`.

Plan ledgers updated:

- `engine-cleanup-plans/05-behavioral-test-coverage.md`
- `engine-cleanup-plans/00-EXECUTION-GUIDE.md`
- `engine-cleanup-plans/README.md`

Current test inventory:

- 59 `TEST_CASE`s across 19 `SkullbonezTests/*.cpp` files.
- 2,537 total lines across those test `.cpp` files.
- 0 `*LinkStubs.cpp` files.

## Validation Evidence

Link-stub removal slice:

- `tools\validate_tests.bat`
- Log: `TestOutput\agent_logs\plan05_link_stubs_validate_tests_attempt3_20260709_0906.log`
- Result: project filters 0 errors, `SKULLBONEZ_TESTS` Profile 0 warnings/0
  errors, 59/59 doctest cases and 1532/1532 assertions passed.
- Runtime: 5.09 seconds.

Earlier failed attempts were fixed before commit:

- Attempt 1 failed because `FLAT_SLOPE_EXTENT` was unqualified.
- Attempt 2 failed because `FLAT_SLOPE_EXTENT` was incorrectly namespace
  qualified.
- Final fix uses `Terrain::FLAT_SLOPE_EXTENT`.

Comment audit:

- Audited touched source-bearing files against
  `Agentic/Reference/comment-style-guide.md`.
- Files audited:
  - `SkullbonezTests/TestAssetSystem.cpp`
  - `SkullbonezTests/TestTerrain.cpp`
  - `SkullbonezTests/TestReplayRecorderFullCaptureBoundary.cpp`
  - `SkullbonezTests/TestRenderResourceDoubles.h`
  - `SkullbonezTests/TestDeterminism.cpp`
- Deferred count: 0.

Whitespace/status checks:

- `git diff --check` reported only expected CRLF normalization warnings for
  Visual Studio project files; no whitespace errors.

## Current Open Work

Plan 05 is complete and its campaign checklist row is checked.

Open cleanup items that remain in `engine-cleanup-plans/00-EXECUTION-GUIDE.md`:

- Plan 13 FAC-005 remains open on a human-owned public physics API planning
  decision.
- Plan 11 RenderGraph decision remains human-gated.
- Plan 02 rest remains open for solver decomposition work.
- Plan 04 remains open for error-handling reconciliation.
- Plan 03 remains sign-off gated.
- Plan 07 remains decision gated.

## Resume Checklist

1. Re-run the repo startup contract from `AGENTS.md`.
2. Check `git status --short --branch`.
3. Confirm the branch is at or after `0200ec48`.
4. Continue from `engine-cleanup-plans/00-EXECUTION-GUIDE.md`.
5. Do not continue Plan 11 until the human RenderGraph decision is made.
6. Do not continue Plan 13 FAC-005, Plan 03, or Plan 07 without the required
   human decision/sign-off.
