# Night Runner Takeover Handoff

Date: 2026-07-28
Branch: `nightrunner-28th-JUL-26-takeover`
Base: main tip `81cc50bc`
Prior campaign PR: `#136`
Status: Ready for the next agent; one planned phase remains before owner review
of the protected warm-start working-tree experiment

## Why This Branch Exists

The completed principal-engineer campaign was accidentally developed under
the next day's branch name, `nightrunner-28th-JUL-26`, and merged through PR
`#136`. The takeover branch gives the actual 2026-07-28 run an unambiguous
branch without deleting a remote ref or rewriting history.

This branch was created directly from the merged main tip. No stash was used.
The three protected in-progress Physics files remained in the working tree
while the branch changed.

## Binding Execution Order

1. Continue `Agentic/Plans/TODO/dependency-proof-generation.md` at DP2 through
   the repository orchestrator. DP0 and DP1 are complete.
2. Commit and push accepted DP2 work with its required report, ledger,
   session-state, review, and mapped validation.
3. Only after DP2 is closed, evaluate the warm-start key-capacity experiment
   described below. It is intentionally the final actionable item.
4. Do not refresh a Physics, performance, Replay, DX12, golden, or other
   baseline without explicit owner approval.

The live portfolio remains 2/3 (67%). DP2 must prove planted generated-proof
drift, split project-ownership failure branches, end-to-end XML/path discovery,
bounded residual-parser behavior, final instruction/comment reconciliation,
independent review, and the mapped fast/full gates. The authoritative detail is
in `Agentic/Plans/TODO/dependency-proof-generation.md`.

## Protected Uncommitted Physics Work

These three source files are modified and intentionally unstaged:

- `SkullbonezSource/Physics/PersistentContactSolver.h`
- `SkullbonezSource/Physics/PersistentContactSolver.cpp`
- `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.cpp`

The experiment:

- defines one shared `PERSISTENT_CONTACT_BODY_MASK` with the existing
  `0x7fff` value;
- statically proves that every valid scene body index fits the persistent
  contact key's 15-bit body fields;
- replaces the two duplicated local mask definitions in solver packing and
  narrowphase prefix lookup with the shared constant.

The intended runtime behavior is neutral: the bit width, terrain-kind bit,
feature field, object-pair ordering, and packed key layout are unchanged.
This claim has not yet been validated. No build, test, determinism,
performance, contact-stability, or baseline command has been run for this
uncommitted diff.

Preserve these files while completing DP2. Before accepting the experiment,
inspect all three source files with the comment-style audit and run the
smallest Physics/performance evidence that answers the owner's acceptance
metric, followed by the mapped PR gate if the change will be committed.

## Owner Questions

The authoritative questions remain in `Agentic/Plans/MASTER-PLAN.md`:

1. For any future AoS proposal, does a meaningful regression mean more than
   2%, more than 5%, or only a statistically significant regression relative
   to SoA?
2. For this warm-start experiment, should acceptance use Physics-frame time,
   solver time, contact stability, or all three?
3. If measured output differs from a committed baseline, should the baseline
   be reset? Until the owner explicitly answers yes, the answer is no.

Neither unresolved question blocks DP2. The warm-start metric question blocks
the final experiment's acceptance decision, not read-only measurement.

## Completed Campaign Context

PR `#136` contains the completed principal-feedback response and its accepted
follow-up work: Physics hot-layout evidence with SoA retained, Replay restore
and signature governance, explicit Physics fixed-list ownership, compact
`SbResult`, explicit vector dot products, and isolated deterministic terrain
fixtures. The campaign's final-source gates passed without a baseline refresh.

The only live campaign plan is dependency-proof DP2. Its checkpoint evidence
is in:

- `Agentic/Reports/2026-07-28/dependency-proof-generation-dp0-comparison.md`
- `Agentic/Reports/2026-07-28/dependency-proof-generation-dp1-checkpoint.md`
- `Agentic/Reports/2026-07-28/principal-engineer-feedback-response.md`

## Validation For This Handoff

Documentation-only branch/handoff preparation required no repository
validation. `git status --short --branch` is the acceptance proof: the takeover
branch is checked out and only the three protected Physics files remain
unstaged after the handoff documentation is committed.

