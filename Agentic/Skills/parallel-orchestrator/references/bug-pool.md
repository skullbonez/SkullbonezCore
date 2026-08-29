# Master Bug-Report Pool

Read this reference before assigning any row from
`Agentic/Bugs/master_bug_report.csv`. The parallel orchestrator remains the
authority for subsystem leases, worktrees, dispatch, fan-in, validation, and
ledger state.

## Triage And Atomic Assignment

The coordinator, never a worker, performs global triage and atomically reserves
an ordered cohesive set of finding IDs from one subsystem plus its complete
lease set. Re-read the CSV without trusting its `fixed` column alone. Treat the
exact `subsystem` field as the mandatory initial lease, then expand it before
assignment from `locations`, CodeGraph/source ownership, tests, fixtures,
project metadata, and the required fixes. Add `path-owner:<package>` for every
affected owner absent from the CSV and `resource:<name>` for shared mutable
facilities. Never narrow the CSV lease from file-level evidence or coerce an
unmatched package into another alias.

Default to every eligible open finding in that subsystem in one batch. Keep the
group together when ownership, write scope, acceptance witnesses, validation,
and integration checkpoint remain understandable as one reviewable change.
Split it into the fewest coherent batches only when the full group crosses
independent owners, mixes incompatible high-risk contracts or baseline
decisions, requires unrelated exclusive facilities, becomes too large for one
reviewer to assess reliably, or otherwise makes a single pass unsafe. Record
the reason and membership of every split; finding count alone is not a reason.

Cross-check active plans, active and pending bug leases, SessionState, source,
tests, relevant history, current/upcoming file leases, contracts, resources,
and the intended integration checkpoint. Assign the highest-ranked eligible
subsystem batch whose complete lease is free.

The worker revalidates every assigned row and implements the whole batch before
review. If new evidence reveals another subsystem, it stops before editing that
owner and requests lease expansion. Grant expansion only when every added exact
subsystem value is unleased; otherwise preserve the evidence and defer the
lane. Workers never self-select from a shared pool.

## Conflict Forecast

Forecast conflict from:

- every active `Agentic/Plans/TODO/` plan's impact area, worklists, reference
  sites, intended moves, project changes, tests, and closure gates;
- dirty and untracked coordinator files;
- current CodeGraph impact/caller evidence, confirmed against source;
- project, generated-proof, baseline, fixture, and rule files affected by both
  planned work and the bug; and
- the exact commit checkpoint where the bug would merge.

Treat direct file overlap, shared public-contract edits, planned file moves,
common retained-state ownership, shared behavior-sensitive tests, and likely
semantic conflict as conflicts even when Git could merge the text. Defer an
uncertain lane; the pool exists for useful work that should fan in mechanically,
not speculative work that must later be ported by hand.

Choose a row only when all of these are true:

- no active or future registered plan phase already owns the finding;
- its source, tests, fixtures, baselines, project metadata, and generated files
  do not overlap an active plan task or another subsystem batch branch;
- the fix needs no unresolved owner, schema, dependency, baseline, or golden
  decision;
- a bounded regression can mechanically express the expected behavior, or the
  worker records why that is impractical;
- focused validation is isolated from exclusive GPU, performance, baseline,
  intermediate, binary, cache, report, and artifact resources; and
- the branch can wait for its checkpoint without uncommitted work from another
  lane.

Require mechanical fan-in: a normal merge must retain the implementation and
regression without choosing between competing contracts. Manual semantic
transplant, plan redesign, baseline reinterpretation, or an owner decision
makes the row ineligible for the pool.

After hard filtering, rank by severity descending, predicted clash risk, then
stable ID. Independence outranks severity: prefer a safer lower-severity row
when the higher row has materially greater integration risk, and record why.
Return the shortlist and deferrals for audit.

Do not select:

- a Physics finding already assigned to an active FP phase;
- a Runtime, App, operator-UI, or project-topology row owned by an active
  boundary-separation plan;
- UI foundation/product work owned by an active UI separation plan;
- a row whose fix changes an owner-controlled baseline or golden;
- a tooling row that changes a gate while an active plan relies on that gate;
  or
- a broad cluster merely because its rows share a subsystem label.

Record every seriously considered deferral with its conflicting owner, paths or
symbols, and safe reconsideration checkpoint. Revisit only after that owner
closes or the source scope materially changes. If no row is eligible, leave the
slot idle or assign different useful work; never manufacture a bug or substitute
a cosmetic audit.

## Execute One Subsystem Batch Per Branch

Use one agent, branch, isolated worktree, and subsystem lease per active
subsystem batch. Run no more than one active batch in a subsystem. Prefer:

```text
codex/bugs-<subsystem-lower>-<short-slug>
```

The worker follows repository startup, diagnoses every assigned finding before
editing, adds or updates the owning subsystem regressions, applies comment/code
standards, and completes the whole bounded implementation batch before review.
Use one independent read-only rubber-duck pass for the integrated batch. The
review prompt lists every finding id, acceptance criterion, changed path, and
focused witness and requires a per-finding verdict plus cross-bug interaction
findings. Fix blocking findings as one batch and repeat only when the fixes
materially change the reviewed risk or the reviewer explicitly requires it.
Then run cumulative focused validation, commit locally, and return the main
skill's complete dispatch handoff.

Preserve separate commits for unrelated findings in the batch. Each uses
exactly `BUG <FINDING_ID> — <ACTION SUMMARY>`, followed by the base
orchestrator's required `Why:`, `Ownership:`, `What:`, `Validation:`,
`Baselines/Artifacts:`, and `Review:` body sections. Combine findings in one
commit only when they share one inseparable root cause and acceptance witness;
name every covered id in the body while retaining one primary id in the subject.
The worker sends every full message for coordinator approval, runs the
deterministic message gate, and commits from those same files. Bug commits do
not claim MASTER progress. A worker does not edit MASTER, SessionState, the
active plan, the live ledger, or the bug CSV unless that file's documented
owner explicitly assigns the mutation.

## Hold And Integrate At A Safe Checkpoint

Do not fan a bug batch into the middle of a plan phase that is establishing
baseline, determinism, performance, graph, or validation evidence. Preserve the
commits and focused evidence. Default fan-in to the first checkpoint after every
overlapping active plan has published coherent evidence and before a dependent
plan captures its baseline. A more frequent checkpoint is allowed when it
cannot invalidate accepted evidence.

When a primary plan is blocked, its pushed documentation-only blocker record is
the bug fan-in checkpoint. Integrate safe independent subsystem batches one at
a time; do not hold them until the blocked plan completes.

At fan-in:

1. Require a clean integration worktree at the pushed coordinator commit; never
   rebase worker history.
2. Merge eligible branches one at a time in dependency order.
3. Inspect and validate each integrated bug independently and preserve separate
   commits for unrelated fixes.
4. Run cumulative cross-subsystem gates on the combined tree.
5. Push normally, record each full commit/result, and remove a worker worktree
   only after its evidence and integration are secure.
6. Re-read MASTER and refresh newly ready inventories from the combined commit.

If a branch conflicts with accepted plan ownership or behavior, do not guess.
Leave it unmerged, record the blocker and deletion/rework condition, and
continue with eligible branches.
