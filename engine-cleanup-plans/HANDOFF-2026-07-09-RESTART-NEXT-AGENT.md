# Restart Handoff - Engine Cleanup Continuation

Date: 2026-07-09
Branch: `nightrunner-8th-july`
Latest pushed commit: `e699437f docs(cleanup): record owner cleanup decisions`
Worktree at handoff: clean and synced with `origin/nightrunner-8th-july`

## Status

The long-running goal was intentionally stopped by the owner so the machine can
restart and the next Codex session can begin with a fresh context. Do not
restart the cleanup campaign from scratch. Continue from the current branch,
current worktree, and committed plan ledgers.

The immediate restart point is:

- Read `AGENTS.md`, `README.md`, `Agentic/README.md`,
  `Agentic/SessionState.md`, and
  `Agentic/Skills/orchestrator/SKILL.md`.
- Read `engine-cleanup-plans/HANDOFF-2026-07-09-OWNER-DECISIONS.md`.
- Read `engine-cleanup-plans/HANDOFF-2026-07-09-PLAN04-CAPTURE-RESULT.md`.
- Resume Plan 04 one bounded slice at a time.

## Owner Decisions To Carry Forward

These decisions supersede any older "ask human" gates in the cleanup plans:

1. **Plan 11 / RenderGraph**
   Retire/delete the diagnostic RenderGraph path. Do not build a real
   render-graph compiler now. DX12 explicit hand-coded barriers are the honest
   architecture. Remove stale claims that RenderGraph owns, or will soon own,
   barrier derivation.

2. **Plan 03 / governance apparatus**
   Approved. Delete the regex boundary checker and frozen `MAX_*` ratchet
   apparatus, and update `AGENTS.md` accordingly when that plan step runs. Keep
   real product safety checks as simple pass/fail validation, especially
   DX12-only enforcement. Do not replace the old checker with vocabulary
   policing.

3. **Plan 13 / FAC-005 public physics API**
   Approved. Create and execute a dedicated public physics API boundary plan.
   Public physics API headers such as `PhysicsApi.h` and `PhysicsEngine.h`
   should expose no `GameModel`, no raw dense `modelIndex` authority, and no
   solver container types. Treat this as physics identity/authority cleanup and
   validate with `tools\validate_physics.bat` when implementation reaches the
   PR gate. This is now recorded as
   `engine-cleanup-plans/14-public-physics-api-boundary.md`.

4. **Plan 07 / allocation policy**
   Do not weaken allocation enforcement to only physics/render hot paths.
   Runtime allocation policy is global zero allocation by default. Runtime
   allocations are banned unless explicitly owner-approved and routed through
   the special allocator/approval path. Current approved runtime exception:
   replay only, with registered owner, phase/cap policy, counters, and
   diagnostics. Require owner approval before adding any new runtime allocation
   exception.

5. **Plan 04 / error handling**
   Continue current Plan 04 work. Convert Lane F throws to `SB_FATAL` where
   safe, then P to probe/report failures, then R to recoverable results.
   Allocator-hook fatal handling needs an allocator-safe strategy; do not
   blindly call `SB_FATAL` from allocation hooks if it can allocate or use unsafe
   engine logging. Math throw conversions may require coordinated test-contract
   changes.

## Completed Immediately Before Restart

- `a8e05d21 cleanup(04): return capture screenshot failures`
  - Converted Plan 04 Phase 3 screenshot capture/readback/file-output failures
    to recoverable `SbResult` paths.
  - `tools\validate_build.bat Profile` passed.
  - `tools\validate_full.bat` passed.
- `e699437f docs(cleanup): record owner cleanup decisions`
  - Recorded the owner decisions above in the plan ledgers.
  - Added Plan 14 for FAC-005 public physics API boundary work.
  - Added `HANDOFF-2026-07-09-OWNER-DECISIONS.md`.
  - Documentation-only slice; no repository validation required.
  - `git diff --check` passed.

## Next Work

Resume Plan 04 Phase 3 Lane R conversions. The best next bounded candidate from
the previous source handoff is the `Input.cpp` Win32 cursor/client-coordinate
cluster:

- `Input::GetMouseCoordinates` `GetCursorPos` failure
- `Input::GetClientMouseCoordinates` `ScreenToClient` failure
- `Input::SetMouseCoordinates` `SetCursorPos` failure
- `Input::CentreMouseCoordinates` `ClientToScreen` failure
- `Input::CentreMouseCoordinates` `SetCursorPos` failure

Use CodeGraph first to inspect `Input.cpp`, `Input.h`, and callers. Convert the
cluster only if the result API can be kept bounded and the call sites can report
the recoverable failure cleanly. For Runtime/Input source changes, the expected
PR gate is `tools\validate_full.bat`; a focused `tools\validate_build.bat
Profile` before the full gate is acceptable.

## Guardrails For Next Agent

- Run `git status --short --branch` before editing and before committing.
- Treat any dirty files as user-owned/current-runner-owned work.
- Use the orchestrator skill for plan implementation.
- Update checkboxes only after the named validation passes.
- Commit and push per completed validated step on this feature branch.
- Do not run broad validation unless the current step requires it.
- Documentation-only changes need no repository validation.
- Do not mark Plan 04 or the overall engine cleanup complete until all relevant
  plan checkboxes and acceptance boxes are genuinely closed.

## Prompt For Next Agent

```text
You are continuing engine cleanup work in C:\SkullbonezCore on branch
`nightrunner-8th-july`. Do not restart the campaign from scratch. Continue from
the current branch and protect any dirty files as user-owned/current-runner-owned
work.

Follow AGENTS.md exactly. First read:
- AGENTS.md
- README.md
- Agentic/README.md
- Agentic/SessionState.md
- Agentic/Skills/orchestrator/SKILL.md
- engine-cleanup-plans/HANDOFF-2026-07-09-OWNER-DECISIONS.md
- engine-cleanup-plans/HANDOFF-2026-07-09-RESTART-NEXT-AGENT.md
- engine-cleanup-plans/HANDOFF-2026-07-09-PLAN04-CAPTURE-RESULT.md

Run `git status --short --branch` before editing. If `.codegraph/` exists, use
CodeGraph first for code investigation.

Owner decisions to carry forward:
1. Plan 11 / RenderGraph: retire/delete the diagnostic RenderGraph path; do not
   build a real compiler now. DX12 explicit hand-coded barriers are the honest
   architecture. Remove stale claims that RenderGraph owns or will soon own
   barrier derivation.
2. Plan 03 / governance apparatus: approved to delete the regex boundary checker
   and frozen `MAX_*` ratchet apparatus, and update AGENTS.md accordingly. Keep
   real product safety checks as simple pass/fail validation, especially
   DX12-only enforcement. Do not replace it with vocabulary policing.
3. Plan 13 / FAC-005: approved to execute dedicated public physics API boundary
   Plan 14. `PhysicsApi.h` / `PhysicsEngine.h` must expose no `GameModel`, no raw
   dense `modelIndex` authority, and no solver container types. Validate source
   implementation with `tools\validate_physics.bat`.
4. Plan 07 / allocation policy: do not weaken enforcement to physics/render hot
   paths. Runtime allocation policy is global zero allocation by default.
   Runtime allocations need owner approval and the special allocator/approval
   path. Current approved runtime exception is replay only, with registered
   owner, phase/cap policy, counters, and diagnostics.
5. Plan 04 / error handling: continue Lane F to `SB_FATAL` where safe, then P to
   probe/report failures, then R to recoverable results. Allocation hooks need an
   allocator-safe fatal strategy before conversion; math conversions may require
   coordinated test-contract changes.

Latest pushed commit is `e699437f docs(cleanup): record owner cleanup decisions`.
The previous source slice was `a8e05d21 cleanup(04): return capture screenshot
failures`; it passed focused Profile build and `tools\validate_full.bat`.

Resume Plan 04 one bounded slice at a time. Good next candidate:
`Input.cpp` Lane R Win32 cursor/client-coordinate failures:
`GetCursorPos`, `ScreenToClient`, `SetCursorPos`, `ClientToScreen`, and
`SetCursorPos` in the centering path. Use CodeGraph to inspect `Input.cpp`,
`Input.h`, and callers before editing. Keep the API/result change bounded. For
Runtime/Input source changes, run the required gate before ticking checklist
items: expected gate is `tools\validate_full.bat`; a focused
`tools\validate_build.bat Profile` first is acceptable.

Commit and push each completed validated step. Documentation-only changes need
no repository validation. Do not run broad validation unless the current step
requires it. Do not update plan checkboxes until the named validation passes.
```
