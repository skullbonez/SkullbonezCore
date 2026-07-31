# Runtime Contract Hygiene

Date: 2026-07-31
Status: TODO — 0/3 phases complete
Impact area: Application exit reporting, Quaternion public contract, Physics fixed-list unwind path
Owner: Runtime
Priority: Medium

## Problem And Evidence

Three unrelated small defects share one shape: a contract that is stated but not
delivered, where nothing mechanical would notice.

**A silently successful failure exit.** `Run::Execute`
(`SkullbonezSource/Runtime/App/RunFrame.cpp:601`) checks the `SbResult` returned
by `PrepareRenderPhase`, `RenderOperatorUiPhase`, and `PresentFramePhase`, but
uses each only as a loop-exit signal and then returns
`m_applicationExit.Resolve( 0 )`. `ApplicationExitState::Resolve`
(`Runtime/App/ApplicationExitState.cpp:78`) returns `Success()` unless
`RequestOwnedFailure` was called separately. Today every phase does latch
internally (`RunFrame.cpp:306`, `:573`, `Runtime/UI/OperatorEditorFrameComposer.cpp:235`,
`:722`, `Runtime/App/InputFrameExecution.cpp:1068`), so the process reports
correctly. Nothing enforces the pairing. A future phase that returns a failure
and forgets to latch it makes the process exit `0` — a silent pass to every
automation harness and CI gate in the repository, which is the exact failure
mode the Lane R taxonomy exists to prevent.

**A public quaternion contract that is partly fiction.**
`SkullbonezSource/Maths/Quaternion.h:68` and `:79-81` document private methods
that no longer exist ("Rotate by angular-displacement components without Euler
decomposition", "Builds an X-axis delta rotation in radians", and two siblings).
Separately, `RotateAboutAxis` requires a normalized axis — it builds
`Quaternion( axis * sinf( half ), cosf( half ) )` at
`Maths/Quaternion.cpp:87` and a non-unit axis yields the wrong rotation angle
that the following `Normalise()` cannot recover. Every current caller satisfies
the precondition, including the careful cross-product normalization at
`Runtime/Editor/EditorTerrainOrientation.cpp:67`. The header states it nowhere.

**The last `throw` in engine source.** `Physics/PhysicsFixedList.h:581` rethrows
inside `RelocateInto`'s `_CPPUNWIND`-guarded block. `AGENTS.md` records the
strict source throw inventory as zero as of 2026-07-10 and states that any new
`throw` is a review failure. For trivially copyable `T` — which is nearly every
instantiation — the branch is compiled out entirely, so this is cosmetic in
effect and a false claim in fact.

## Goal

Make the exit contract structurally impossible to break, make the quaternion
header describe the code that exists, and make the zero-throw claim true.

## Non-Goals

- No change to Lane R semantics, `SbResult` layout, or the diagnostic store.
- No physics behavior change. CH2 must leave physics byte-exact.
- No new frame phase, owner, or context type.

## Phases

- [ ] **CH0 — Make a silently successful failure exit unrepresentable.**
  Choose and implement one enforcement: either `Resolve` becomes lane-F fatal
  when exit was requested by a failing phase and no owned failure was latched,
  or the phase signature stops returning a status the caller can drop, so
  latching is the only way to signal failure. Prefer whichever removes the
  pairing rather than documenting it. Audit every current `Execute` phase to
  confirm none regresses. Add focused coverage in
  `SkullbonezTests/TestApplicationExitState.cpp` proving a failing phase cannot
  produce process exit `0`. Evidence:
  `Agentic/Reports/2026-07-31/runtime-contract-hygiene-ch0-exit-contract.md`.

- [ ] **CH1 — Correct the Quaternion public contract.** Delete the orphan
  comments at `Quaternion.h:68` and `:79-81`. State the normalized-axis
  precondition of `RotateAboutAxis` in the header and add the Debug assert that
  makes a violation visible, matching the existing `Vector3::Normalise`
  precedent of a Debug tripwire with Release IEEE propagation. Confirm every
  caller found in the tree satisfies the precondition and record the list. This
  is comment-and-assert work; keep the diff free of arithmetic changes so it
  stays documentation-only for validation purposes. Evidence:
  `Agentic/Reports/2026-07-31/runtime-contract-hygiene-ch1-quaternion.md`.

- [ ] **CH2 — Make the zero-throw inventory true.** Remove the rethrow at
  `PhysicsFixedList.h:581`. Preferred repair is to require nothrow-move-
  constructible `T` for relocation with a `static_assert` and delete the
  `_CPPUNWIND` block, since the list already deletes copy and move and only
  relocates during phase-gated `Reserve`. If any live instantiation cannot
  satisfy that, state which and use a `noexcept` relocation path with an
  explicit lane-F failure instead. Confirm the repository-wide `throw` count is
  zero afterwards. Physics must stay byte-exact. Evidence:
  `Agentic/Reports/2026-07-31/runtime-contract-hygiene-closure.md`.

## Acceptance

A frame phase cannot report failure and exit `0`. `Quaternion.h` documents only
methods that exist and states the precondition its implementation relies on. The
engine source contains zero `throw` expressions and the claim in `AGENTS.md` is
true again. Physics baselines are unchanged.

## Validation

CH1 is documentation-only; prove the diff is comments and one Debug assert.
CH0 touches `Runtime/*` and requires `tools\validate_full.bat`. CH2 touches
physics storage and requires `tools\validate_tests.bat`,
`tools\validate_physics.bat`, and the allocation-policy self-test and repository
scan. Final gate for the plan: `tools\validate_full.bat`.
