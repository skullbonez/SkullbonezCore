# Fable 01 E2 ReplayRecorder Tests

Date: 2026-07-07
Branch: `nightrunner-7th-july`

## Result

E2 is complete. `SKULLBONEZ_TESTS` now covers the ReplayRecorder presentation
ring-buffer contracts.

## Changes

- Added `SkullbonezTests/TestReplayRecorder.cpp`.
- Added `SkullbonezTests/TestReplayRecorderLinkStubs.cpp`.
- Added `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp` to
  `SKULLBONEZ_TESTS.vcxproj`.
- Added matching Tests and Runtime/Replay filters to
  `SKULLBONEZ_TESTS.vcxproj.filters`.

## Evidence

- CodeGraph and focused source reads mapped `ReplayRecorder::Configure`,
  `ResetTimeline`, `CaptureFrameFromSolverSample`, `CopySamplesChronological`,
  `SampleAtNormalized`, `LatestSample`, and `GetStats` as the clean standalone
  ring-buffer surface.
- Discovery command `rg -n "Cfg\(|Gfx\(|::Instance"
  SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp
  SkullbonezSource/Runtime/Replay/ReplayRecorder.h` returned no hits.
- Tests use synthetic solver samples through `CaptureFrameFromSolverSample()`
  so they exercise the real presentation ring while avoiding the full live
  model/world capture path.
- Coverage locks retention capacity, oldest-frame eviction after wrap,
  chronological copy order, event cursor retention, body payload copy, latest
  sample, normalized scrub lookup, stats, and `ResetTimeline()` clearing
  samples/cursors while preserving capacity.
- `ReplayRecorder.cpp` also contains uncalled full-capture methods that
  reference camera, world, model, and physics owners. `TestReplayRecorderLinkStubs.cpp`
  supplies loud test-only stubs for those hooks and throws if a focused ring
  test crosses that boundary.

## Validation

- First `tools\validate_tests.bat` attempt passed project filters but failed
  link on uncalled full-capture owner hooks from `ReplayRecorder.cpp`.
- Final `tools\validate_tests.bat`: exit 0 in 4.335s, 38 doctest cases and
  397 assertions passed with 0 warnings/errors.

## Comment Audit

Touched source-bearing test files inspected against the comment-style guide:
`SkullbonezTests/TestReplayRecorder.cpp` and
`SkullbonezTests/TestReplayRecorderLinkStubs.cpp` both have learning headers
with glossary, invariants, and related links. The test has a local `Why:`
comment for solver-sample mirroring, and the link stub has a local `Hazard:`
comment for accidental full-capture boundary crossings.
