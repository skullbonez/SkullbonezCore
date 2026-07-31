# Retirement Diagnostic Honesty Closure

Date: 2026-07-30
Plan: `retirement-diagnostic-honesty`
Status: COMPLETE - DH0-DH1, 2/2
Impact area: Rendering/DX12 retirement and uncertain-capture diagnostics, tests

## Outcome

`Dx12DeferredReleaseOwner` now reports real, owner-retained accounting at
exhaustion:

- fixed capacity, current count, and monotonic pending high-water;
- `phase=quarantine`;
- the most recent release input, released, and survivor counts;
- whether that release observed a ready frame fence; and
- the last completed fence value actually observed.

`Dx12RetirementDiagnosticState` owns those relationships. It derives
`released = input - survivors` atomically, terminates on impossible survivor
accounting, retains a completed fence only when the fence was observable, and
owns the exact fatal projection. It is a behaviorful invariant owner, not a
parameter bag or generic diagnostics framework.

`ReleaseCompleted` publishes an empty or compacted snapshot through that state.
`Dx12FrameOwner::ResetForDevice` and `ResetAfterShutdown` reset the diagnostic
epoch through explicit retirement-owner methods. Both retirement reset methods
Lane-F if pending rows remain, so a device boundary cannot erase accounting
while carrying resources into the next epoch.

## Plan Correction

The original DH1 wording required a monotonic high-water to differ from capacity
at exhaustion. That state is mathematically unreachable: the final accepted row
raises high-water to capacity before the next row is rejected. The plan was
amended to prove two honest facts separately:

- below saturation, the measured peak differs from capacity and resets at an
  empty device boundary;
- at exhaustion, the necessarily saturated high-water is truthful, while the
  release/fence snapshot supplies the distinguishing diagnostic information.

The correction narrows no acceptance intent and avoids manufacturing a
test-only owner state.

## DX12 Fatal Audit

The remaining `SB_FATAL` audit found one additional fabricated high-water in
`Dx12BackbufferCapture`. That two-row quarantine never releases before terminal
drain, so current count and high-water are structurally identical. The false
field was removed instead of replacing it with another tautological counter.
Its fatal now reports:

- `owner=Rendering/DX12/Capture`;
- `phase=uncertain_submission`;
- the failed operation;
- current count; and
- fixed capacity.

The development-UI descriptor and static-SRV exhaustion diagnostics already use
real retained high-water counters. No other fabricated-field pattern was found,
so no follow-up plan was required.

## Tests

Focused Profile coverage passes two cases and 44 assertions:

- monotonic below-capacity peak, release-count derivation, stalled-fence
  retention, and both device-boundary resets on an empty real owner;
- actual 512-row quarantine exhaustion, a nonzero release/fence fatal snapshot,
  and Lane-F rejection of both reset variants while one row remains live.

The full unit gate passes 471/471 cases and 2,423,979/2,423,979 assertions.

## Ownership And Comment Review

Final ownership evidence reports:

- authority-free aggregates: 1,181 candidates, 16 invariant-owning states,
  85/85 gated rows ruled, zero unruled;
- extraction scars: the one unrelated `WorkerPool` row remains ruled, zero
  touched findings;
- wide-signature and function-complexity self-tests and strict gates pass, with
  no touched trigger;
- build-configuration, dependency, project-filter, and refreshed compiled
  reachability gates pass with zero blocking diagnostics.

All five touched source-bearing files were inspected against
`Agentic/Reference/comment-style-guide.md`: 5 checked, zero deferred. The state
invariant, saturated-high-water hazard, release derivation, reset boundary, and
uncertain-capture diagnostic ownership are stated where they matter. This is a
touched-file audit, not a subsystem campaign, so no checklist plan was
required.

Independent Duck-01 review initially blocked closure on:

1. the impossible high-water-at-exhaustion assertion;
2. a still-tautological readback high-water;
3. reset coverage that carried pending rows across a device boundary;
4. default-zero-only fatal snapshot coverage;
5. the missing `phase` field; and
6. stale plan scope/non-goal wording.

Each finding was corrected and re-reviewed. The final verdict is no blocker:
the diagnostic state is a legitimate invariant owner; no capability slice,
extraction scar, rename evasion, or false comment remains. Native fence
branches are source-reviewed rather than unit-faked; the DX12 renderer and
graphics stress gates provide the appropriate integration proof.

## Final Validation

- `tools\validate_tests.bat`: PASS - 471 cases, 2,423,979 assertions.
- `tools\validate_dx12_renderer.bat`: PASS - 43 baked shader stages current,
  DX12 InfoQueue available with zero validation errors, expected captures
  present, and committed baselines clean.
- `tools\run_graphics_stress.bat 1`: PASS - exit 0 after 61.093 seconds; zero
  wrapper stderr bytes, zero app stderr bytes, and no fatal/error/validation
  text.
- `tools\validate_full.bat`: PASS - coverage, interaction policy, scene parser,
  Automation policy, DX12 renderer validation, full CPU matrix, and byte-exact
  Physics. `physics_regression_varied.csv` remains an exact 44,401-line match
  across two output runs and the committed baseline.

No baseline, golden, shader, configuration, schema, or committed runtime
artifact changed. A final `tools\validate_fast.bat` over this report, campaign
ledger, and TODO deletion is a commit precondition.
