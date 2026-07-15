# Runtime Signature Decomposition — Kill The 9-To-13 Argument Calls

Date: 2026-07-15
Status: Active — 0/5 tasks complete
Impact area: `InputRouter`, `Run` frame composition, runtime frame views,
physics stage contexts (survey only), call-site readability across `Runtime/`
Owner: runtime shell

## Problem And Evidence

The god-object cleanup pushed authority out of `Run`, but the cost surfaced as
parameter-list explosion — a god object smeared across the stack:

1. `InputRouter::SetWorldInteractionOwner` is called with **13 arguments**
   (`SkullbonezSource/Runtime/RunFrame.cpp:636-651`).
2. `InputRouter::ApplyCameraMode` takes **9 arguments**
   (`RunFrame.cpp:621-630`).
3. Frame-view bundles already exist (`RuntimeFrameHostView`,
   `RuntimeFrameInteractionView`, `RuntimeFrameSceneView`,
   `RuntimeFramePresentationView` — `RunFrame.cpp:584-604`,
   `SkullbonezSource/Runtime/RuntimeFrameViews.h`) but the widest calls
   bypass them and re-thread the same owners positionally.
4. Physics stage contexts reach 19 fields
   (`ObjectNarrowphasePairStageContext`,
   `SkullbonezSource/Physics/PhysicsWorld.cpp:3636`); these are value-context
   structs (acceptable hot-path shape) but belong in the survey so the
   pattern's boundary is recorded rather than guessed.

Adversarial review 2026-07-15, finding #1. Owner decision: attack signatures
first; TU splits of `PhysicsWorld.cpp`/`Init.cpp` and `Run` member-list
shrinking are separate future campaigns, deliberately not started here.

## Goal

No runtime function is called with more than six positional arguments where
an existing frame view (or a small named value struct) already carries the
same owners. The widest offenders (`SetWorldInteractionOwner`,
`ApplyCameraMode`) take a view/context plus at most a couple of
operation-specific values. Call sites read as intent, not plumbing.

## Non-Goals

- No ownership moves: this is signature shape only; every borrowed owner keeps
  its current owner and lifetime. This plan must not create a new broad
  mutable bag — views remain per-frame, stack-only borrow maps per the
  existing `Lifetime:` comments, which keeps it on the right side of the
  god-object closure rule.
- No `PhysicsWorld.cpp`/`Init.cpp` file splits, no `Run` member migration
  (future campaigns).
- No behavior change of any kind; physics and DX12 baselines pass unchanged.

## Tasks

- [ ] T1 — Offender inventory: `rg`-based survey of `Runtime/` (and physics
      boundary calls into it) listing every function invoked with ≥7
      arguments — file, function, arg count, which existing view already
      carries which of its parameters. Table recorded in this plan; the
      remaining tasks work strictly from it.
- [ ] T2 — Migrate `InputRouter::SetWorldInteractionOwner` and
      `InputRouter::ApplyCameraMode` to take the appropriate frame view(s)
      plus a small operation value struct (e.g. owner/reason/restore-mode).
      All call sites updated; no view stored beyond the call.
- [ ] T3 — Migrate the remaining T1 offenders in `Runtime/` above the
      threshold, or record a per-row reason to leave one as-is (e.g. a
      hot-path value context that is already a named struct).
- [ ] T4 — Boundary review: confirm no new type violates the closure rules —
      views are read/borrow maps, not authority bags; no callback packs, no
      stored host pointers, no reach-back. One independent rubber-duck review
      per the orchestrator skill at plan end (single review for the whole
      plan, per the migration-cleanup review rule).
- [ ] T5 — Final gate: `Run*`/`Runtime/*` changes map to
      `tools\validate_full.bat`; byte-exact physics and matching DX12
      baselines, no refresh.

## Dependencies And Decisions

- Owner decision 2026-07-15: "signatures first" selected; PhysicsWorld split,
  Init split, and Run member-list shrink were offered and deferred.
- Runs last among the 2026-07-15 remediation plans: it touches the same
  `RunFrame.cpp` region as `win32-message-pump-drain.md` and should rebase on
  top of it rather than race it.

## Acceptance

- T1 table shows zero remaining ≥7-argument runtime calls without a recorded
  keep-reason.
- Independent review finds no new authority bag, forwarding relay, or
  reach-back.
- `validate_full` passes with no baseline changes.

## Validation

- `tools\validate_full.bat`, output pasted at closure.
