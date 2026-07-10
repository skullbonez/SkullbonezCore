# Owner Decisions Handoff - Engine Cleanup Plans

Date: 2026-07-09
Branch: `nightrunner-8th-july`
Slice: documentation-only steering ledger update

## Status

The owner resolved the pending "ask human" gates for Plan 03, Plan 07, Plan 11,
and FAC-005. Continue from the current branch and worktree; do not restart the
campaign from scratch. These decisions are recorded in the plan ledgers before
any further gated implementation.

## Decisions Recorded

- Plan 03 / governance apparatus: owner approved deleting the regex boundary
  checker and frozen `MAX_*` ratchet apparatus. Keep real product safety checks
  as simple pass/fail validation, especially DX12-only enforcement. Do not
  replace the old checker with vocabulary policing.
- Plan 07 / allocation policy: global runtime zero allocation remains the
  policy by default. Do not weaken enforcement to physics/render hot paths.
  Replay is the only approved runtime allocation exception, and it must use the
  special allocator path with owner, phase/cap policy, counters, and diagnostics.
- Plan 11 / RenderGraph: retire/delete the diagnostic RenderGraph path. Do not
  build a real render-graph compiler now. DX12 explicit hand-coded barriers are
  the honest architecture.
- Plan 13 / FAC-005: create and execute a dedicated public physics API boundary
  plan. Public physics API headers must expose no `GameModel`, no raw dense
  `modelIndex` authority, and no solver container types.
- Plan 04 / error handling: continue Lane F to `SB_FATAL` where safe, then Lane
  P to probe/report failures, then Lane R to recoverable results. Allocation
  hooks need an allocator-safe fatal strategy before conversion; math conversions
  may require coordinated test-contract changes.

## Files Updated

- `engine-cleanup-plans/03-governance-apparatus-reduction.md`
- `engine-cleanup-plans/04-error-handling-policy-reconciliation.md`
- Engine cleanup plan 07 allocation-gate right-sizing (completed and deleted
  per MASTER convention on 2026-07-10)
- Engine cleanup Plan 11 render-abstraction leaks (completed and deleted per
  MASTER convention on 2026-07-10)
- `engine-cleanup-plans/13-facade-retirement.md`
- `engine-cleanup-plans/14-public-physics-api-boundary.md`
- `engine-cleanup-plans/00-EXECUTION-GUIDE.md`
- `engine-cleanup-plans/README.md`
- `Agentic/SessionState.md`

## Validation

Documentation-only slice. No repository validation required.

## Next Work

Return to Plan 04 one bounded slice at a time. The next small candidate from
the prior source handoff is the `Input.cpp` Lane R Win32 cursor/client-coordinate
cluster. Use CodeGraph first, then update checkboxes only after the named gate
passes and commit the completed step.
