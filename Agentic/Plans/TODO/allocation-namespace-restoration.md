# Allocation Namespace Restoration

Status: Registered — 0/1 task (A0)
Owner: repository owner; filed 2026-07-20 from the independent dependency-direction closure review
Evidence: `dependency-direction-restoration` L5 surviving-exception table
Ledger: A0
Depends on: `dependency-direction-restoration` closure

## Objective

Make the Core allocation policy's semantic namespace match its physical owner.
Rename `SkullbonezCore::Runtime::Allocation` to
`SkullbonezCore::Core::Allocation` across the allocator implementation and all
consumers, with no compatibility alias, forwarding namespace, or behavior
change.

## Scope And Decisions

- This is a mechanical namespace migration only. Type names, owner strings,
  capacities, phases, replay privileges, allocation/free pairing, and policy
  behavior remain byte-for-byte equivalent.
- The `FATAL[Runtime/Allocation]` diagnostic label describes the runtime
  allocation-policy lane and may remain; it is not a namespace or dependency
  edge.
- Every source, test, project-facing architecture assertion, comment, and
  documentation reference to the old C++ namespace updates in the same commit.
- No alias or bridge may preserve the old namespace. Exact-token proof is zero
  at closure.

## Task

- [ ] A0 — Rename the namespace in `Core/Allocation/*` and every consumer;
  update focused tests and comment references; prove
  `rg -n 'SkullbonezCore::Runtime::Allocation|Runtime::Allocation'
  SkullbonezSource SkullbonezTests Agentic/Tests`
  returns zero live C++ namespace rows (the fatal diagnostic string is checked
  separately, and documentation that names the deleted token is excluded).
  Inspect every touched source-bearing file with the comment audit.
  Validation: `tools\validate_fast.bat`, allocation checker self-test and repo
  scan, `tools\validate_physics.bat`,
  `tools\validate_replay_visual_fidelity.bat` exactly once,
  `tools\validate_perf.bat`, `tools\validate_dx12_renderer.bat`,
  `tools\run_graphics_stress.bat 1`, and `tools\validate_full.bat`.

## Acceptance

- Core allocation source and all consumers use
  `SkullbonezCore::Core::Allocation`; the old namespace has zero live rows and
  no compatibility spelling.
- Allocation owner/phase/cap semantics, replay growth policy, and heavy-Tracy
  allocation/free connection pairing remain covered and unchanged.
- No behavioral artifact, baseline, golden, scene, shader, or screenshot is
  refreshed.

## Validation Summary

A0 runs the cumulative mapped gates listed above. The replay command is one
invocation, one engine process, and one prediction generation with zero golden
refresh.
