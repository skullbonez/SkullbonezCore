---
name: parallel-orchestrator
description: Extend SkullbonezCore's repository orchestrator with a native multi-plan queue that actively runs as many dependency-ready plans, implementation lanes, tests, investigations, validations, and independent bug fixes as available agents can safely support, each on an isolated branch and worktree before main-orchestrator fan-in. Use when the user explicitly invokes the parallel orchestrator, asks to parallelize MASTER-PLAN or Agentic/Plans execution, requests multiple implementation agents or worktrees, asks to fix eligible master bug-report rows alongside planned work, or asks to reduce plan wall-clock time while preserving dependency, validation, review, commit, push, blocker, ledger, and handoff rules.
---

# Parallel Orchestrator

Run the normal repository orchestrator with a native multi-plan queue. Fill
available slots with dependency-ready, merge-safe plans or lanes on isolated
branches/worktrees, then fan them into the integration branch through the main
orchestrator. Parallelism changes scheduling, not the delivery contract.

## Load And Preserve The Base Contract

Before taking orchestration action, read
`Agentic/Skills/orchestrator/SKILL.md` completely. Follow its startup,
Night Runner branch resolution, persistent goal, MASTER-PLAN selection, live
ledger, blocker continuation, risk-proportional review and validation, plan
loop, commit/push, and final handoff procedures except for the native multi-plan
override below.

Also follow `AGENTS.md`, every selected plan, and applicable skills/references.
A specific restriction remains binding only when it names a direct dependency,
authority boundary, shared contract, or exclusive resource; generic one-plan
wording does not disable this multi-plan policy.

### Native Multi-Plan Override

Replace the base skill's plain one-plan queue and implementation-sub-agent
prohibition with this rule:

> Sub-agents may own separate dependency-ready plans or independent lanes within
> a plan. Every writer uses a fresh branch and isolated worktree. The coordinator
> actively maximizes safe occupancy, while retaining dependency analysis, file
> leases, integration, acceptance, validation selection, ledger truth, plan
> progress, commits, pushes, blocker records, and final reporting.

Invoking this skill explicitly supplies that sub-agent/worktree authorization.
It does not authorize any of the following:

- starting a plan or phase before its direct prerequisites or acceptance
  boundary permit it;
- changing a baseline, golden, rule, exception, dependency direction, or owner
  decision without the approval required by the repository;
- committing or pushing directly on `main` without explicit confirmation;
- merging a pull request, rebasing, force-pushing, or rewriting history; or
- weakening validation, review, comment, allocation, determinism, or handoff
  requirements.

MASTER-PLAN position is priority, not a dependency edge. Use it to allocate
scarce slots and order fan-in, but start a later-listed plan whenever no direct
dependency or credible merge conflict requires waiting. Never leave an agent
slot idle while dependency-ready, merge-safe work exists.

### Reversible Decisions During Fan-Out

Apply the base skill's Reversible Decision Autonomy before calling a contract
unresolved. The coordinator should freeze a reasonable provisional contract
and fan out instead of waiting for a small reversible choice. Give each worker
a decision envelope; within it, the worker may choose local constants or test
shapes, pin them with focused evidence, and report them for later revision.

For example, an unspecified motion threshold may provisionally use `0.1`
metres per Physics tick to promote and `0.075` metres to demote, independent of
collider thickness. Keep the wave running and notify the user afterward. Pause
only the affected lane for a true approval boundary; the dedicated bug lane and
other independent lanes continue.

### Out-Of-Plan Bug And Backlog Work

Invoking this skill does not make an unregistered bug ledger, audit, backlog, or
candidate list selectable MASTER-PLAN work. Before assigning such a row to a
parallel worker, classify it as:

- `owned by active task`: leave it with that task and do not duplicate it on
  another branch;
- `owned by another active plan`: leave it to that plan and do not duplicate it;
- `independent but unregistered`: require explicit user activation, a stable
  task/commit identity, acceptance criteria, validation, and a merge checkpoint;
  or
- `read-only investigation`: permit evidence gathering, but make no source edit
  or completion claim.

A separate branch or worktree isolates files; it does not create plan authority.
Explicit invocation of this skill authorizes the bounded master bug-report side
lane below. Any other out-of-plan campaign still requires separate user
activation. Refresh the active plan's inventories and baseline evidence after
an accepted side branch lands.

## Concurrent Master Bug-Report Lane

When `Agentic/Bugs/master_bug_report.csv` exists, reserve one live sub-agent slot
for an independent bug-fix lane. Keep that lane moving both while the
coordinator advances the selected MASTER-PLAN task and while the selected task,
phase, or plan is blocked. A primary-plan blocker never suspends an otherwise
eligible bug. This is a side lane, not a second source of plan priority.

Run the bug lane through a separate agent on its own fresh branch and isolated
worktree. Never execute it in the coordinator agent or worktree, attach it to a
plan worker's branch, or consume its reserved slot with plan review or
validation work. When one bug finishes, have the dedicated bug agent rerun its
triage and begin the next eligible bug without waiting for the primary-plan
blocker to clear.

### Bug Agent Self-Triage

The dedicated bug agent owns each selection. Re-read the CSV without trusting
its `fixed` column alone. Cross-check active plans, SessionState, source, tests,
relevant history, and coordinator-provided current/upcoming worker file leases,
contracts, resources, and checkpoint. Start the highest-ranked eligible row
without waiting for coordinator nomination.

Before ranking bugs, build a conservative conflict forecast from:

- every active `Agentic/Plans/TODO/` plan's impact area, phase worklists,
  reference sites, intended source moves, project changes, tests, and closure
  gates;
- current dirty and untracked files in the coordinator tree;
- CodeGraph impact/caller results for the reported symbols when CodeGraph is
  current, confirmed against source;
- project, generated-proof, baseline, fixture, and rule files affected by both
  the bug and planned work; and
- the intended checkpoint commit where the bug would merge.

Treat direct file overlap, shared public-contract edits, planned file moves,
common retained-state ownership, shared behavior-sensitive tests, and likely
semantic conflict as conflicts even when Git could merge the text. If the
forecast is uncertain, defer the bug. The purpose of the side lane is useful
work that is reasonably expected to merge, not speculative work that must be
ported by hand later.

Choose a row only when all of these are true:

- no active or future registered plan phase already owns the finding;
- its source, tests, fixtures, baselines, project metadata, and generated files
  do not overlap any active plan task or another pending bug branch;
- the fix needs no unresolved owner, schema, dependency, baseline, or golden
  decision;
- one bounded regression test can mechanically express the expected behavior,
  or the worker records why that is impractical;
- its focused validation can run in the worker worktree without contending for
  the coordinator's exclusive GPU, performance, or baseline resources; and
- the branch can wait until the recorded integration checkpoint without
  depending on uncommitted work from another lane.

Require the expected fan-in to be mechanical: a normal merge should retain the
bug's complete implementation and regression witness without choosing between
two competing contracts. A bug that would require manual semantic transplant,
plan redesign, baseline reinterpretation, or a conflict-resolution owner
decision is ineligible.

After hard eligibility filtering, rank by reported severity descending, then
predicted clash risk across files, contracts, tests, metadata, fixtures, and
validation resources, then stable id. Independence outranks severity: when a
higher-severity row carries materially more integration risk, choose the safer
row and record why. Return the ranked shortlist and deferrals for audit.

Do not select:

- a Physics finding already assigned to an FP phase;
- Runtime package, App, operator-UI, or project-topology work owned by an active
  boundary-separation plan;
- UI foundation/product work owned by an active UI separation plan;
- a row whose fix changes an owner-controlled baseline or golden;
- a tooling row whose repair would change the active plan's required gate while
  that plan is using the old gate as evidence; or
- a broad cluster merely because its rows share a subsystem label.

Record every skipped row that was seriously considered with the conflicting
plan/phase, overlapping files or symbols, and safe reconsideration checkpoint.
Do not repeatedly re-investigate the same ineligible row during one run unless
the owning plan closes or its source scope materially changes.

If no row is eligible, record the eligibility screen and why each seriously
considered row was deferred. Leave the dedicated bug lane idle until an eligible
row appears; do not repurpose its agent as a comment-audit worker. Never
manufacture bug work to satisfy a concurrency target.

### Execute One Bug Per Branch

Use one separate agent, branch, and isolated worktree per active bug. Combine two
rows only when they have one inseparable root cause and one acceptance witness.
Prefer:

```text
codex/bug-<finding-id-lower>-<short-slug>
```

The bug worker follows the repository startup contract, diagnoses before
editing, adds or updates the owning subsystem regression test, applies the
comment and code standards, runs the cumulative focused validation mapped by
`AGENTS.md`, commits locally, and returns the exact handoff required under
Dispatch Contract.

Bug commits use normal commit-message rules with the finding id; they do not
claim MASTER-PLAN progress. The worker must not edit MASTER-PLAN, SessionState,
the active plan, the live ledger, or the bug CSV unless that CSV's documented
owner explicitly requires status mutation.

### Hold And Integrate At A Safe Checkpoint

Do not merge a completed bug branch into the middle of a plan phase whose
baseline, determinism, performance, graph, or validation evidence is still
being established. Preserve its commit and focused evidence, then continue the
main lane.

Default fan-in to the first checkpoint after every overlapping active plan has
published coherent evidence and before a dependent plan captures its baseline.
This keeps evidence coherent without waiting for unrelated plans. A more
frequent checkpoint is allowed when it cannot invalidate accepted evidence.

When the primary plan is blocked, treat its pushed documentation-only blocker
record as the bug fan-in checkpoint. Integrate completed independent bug branches
one at a time after focused review and validation, push the accepted commits,
then dispatch the next eligible bug from a fresh branch and worktree. Do not
hold safe bug fixes until the blocked plan completes.

At the checkpoint:

1. Recreate or refresh a clean integration worktree at the pushed coordinator
   commit without rebasing worker history.
2. Merge eligible bug branches one at a time in dependency order.
3. Inspect and validate each integrated bug independently; retain separate
   commits rather than collapsing unrelated fixes.
4. Run the cumulative cross-subsystem gates for the combined tree.
5. Push normally, record each full commit and result in the live ledger/handoff,
   and delete a worker worktree only after its evidence and integration are
   secure.
6. Re-read MASTER-PLAN and refresh every newly ready plan's inventories from
   the final combined commit.

If a bug branch conflicts with accepted plan ownership or behavior at fan-in,
do not guess through the conflict. Leave it unmerged, record the exact blocker
and deletion/rework condition, and continue with other eligible branches.

## Bootstrap Before Fan-Out

Complete the base orchestrator bootstrap first:

1. Read the startup files and inspect the dirty tree.
2. Resolve and verify the Night Runner integration branch.
3. Create or reuse the persistent orchestration goal.
4. Start the base work ledger.
5. Build the direct dependency/conflict graph for every unfinished live plan.
6. Select as many dependency-ready, merge-safe plans as available slots permit.
7. Start and account for every dispatched plan task, then read each plan deeply
   enough to resolve acceptance, counts, risks, tests, baselines, and stops.

Do not create worker branches from uncommitted coordinator work. First preserve
pre-existing dirty files as user-owned, then establish the exact clean commit
that every lane will use as its base.

## Build A Parallel Wave

Treat the unfinished MASTER queue and each selected plan as one dependency
graph. Fill every available slot with the highest-priority ready plan or lane;
do not serialize independent plans merely because one appears later in MASTER.
Give each plan its own branch and worktree, and merge completed branches only
through the main orchestrator. Before dispatch, answer:

1. What contract or design decision must be frozen before implementation
   divides?
2. Which lane produces that contract, and which lanes consume it?
3. What exact files or directories may each lane write?
4. Which files and resources require a single writer?
5. What result or commit must exist before each lane may start?
6. Which focused witness proves each lane independently?
7. What integrated validation proves the combined result?

Classify each proposed lane as one of:

- `investigation`: read-only mapping, reproduction, measurements, or source
  analysis;
- `implementation`: a cohesive production change with an exclusive write set;
- `tests`: focused behavioral tests or false-pass controls in owner-specific
  test files;
- `validation`: a read-only gate against one frozen integrated commit;
- `evidence`: bounded artifact or result collection that does not decide
  acceptance; or
- `review`: the base orchestrator's independent read-only rubber-duck lane.

Never create a dedicated comment-audit lane or dispatch a sub-agent whose job is
only to audit comments. Every source-writing worker applies the repository
comment standard to its own touched files, and the coordinator reconciles the
complete integrated touched-file set before closure. This does not remove the
base orchestrator's independent rubber-duck review requirement.

Record the wave in coordinator commentary before dispatch. Include the frozen
base commit, dependency edges, lane owners, write scopes, prohibited shared
files, required evidence, and fan-in order.

### Eligibility Gate

A lane may run concurrently only when:

- every dependency that defines its inputs is already complete;
- its production write set is disjoint from every other active lane, or it is
  wholly read-only;
- shared headers or value contracts it consumes are frozen at the base commit;
- its test ownership is separate from other active test lanes;
- its output can be reviewed and integrated without guessing intent;
- its validation does not contend for an exclusive runtime resource; and
- the live ledger can account truthfully for every concurrent agent involved.

Do not parallelize when workers would edit the same function, retained owner,
state transaction, generated proof, plan progress row, project item list,
baseline, or behavior-sensitive fixture. Split discovery from implementation,
land the shared contract first, or keep the task serial.

## Single-Writer Locks

The coordinator owns these by default unless an active plan gives one entire
lane exclusive ownership:

- `Agentic/Plans/MASTER-PLAN.md`, `Agentic/SessionState.md`, and every active
  plan's progress or closure evidence;
- `Agentic/Plans/WORK_LEDGER.csv` and goal/task lifecycle commands;
- baseline approvals, golden files, phase artifact manifests, and final
  acceptance evidence;
- shared solution/project membership and generated dependency proof;
- common public contracts consumed by multiple active lanes;
- conflict resolution and the integrated branch; and
- final validation, commit, push, and task/goal closure.

Treat the GPU, performance-measurement environment, baseline generator, and any
fixed output path as exclusive resources. CPU-only read-only gates may run in
parallel worktrees against the same frozen commit when their outputs are
isolated. Run GPU launches, graphics stress, performance A/B measurements,
baseline generation, and terminal plan-completion validation serially unless
the repository provides an explicitly isolated facility.

## Ledger-Compatible Concurrency

Preserve the base work ledger as the sole orchestration ledger. Never invent
token counts, costs, worker identities, or overlapping elapsed time.

Do not treat the ledger's attached-worker arity as a worktree limit. Worktree
capacity and live-agent concurrency are separate concerns:

- create as many isolated worktrees as the dependency graph needs;
- give each dispatched agent/skill lane at most one writable worktree;
- keep additional prepared, waiting, completed, or integration worktrees even
  when no agent is currently active in them; and
- bound simultaneous execution by available agent slots, machine resources,
  dependency safety, and the ledger's ability to account for every live
  session, not by a fixed worktree count in this skill.

The maximum is one writable worktree per dispatched agent or lane, plus the
coordinator and integration worktrees; there is no fixed plan or worktree
ceiling. Open distinct task/step accounting for every live plan worker. A
single-active-task or single-worker ledger seam is a tooling defect to repair,
not permission to serialize merge-safe plans. Never invent usage or reuse one
writable tree across agents.

For every live worker, transition to an accountable step before dispatch,
attach its real thread/session id using the supported ledger mechanism, and
fan in only after that worker finishes. Tool availability is never permission
to omit accounting.

## Create Worker Worktrees

Create one isolated worktree for every dispatched lane, and create as many lane
worktrees as the plan, bug side lane, review wave, and integration sequence
need. Do not serialize work merely to conserve a fixed worktree count. Create
each worker from the exact frozen integration commit on a fresh local branch.
Use the repository's `codex/` branch prefix for worker branches unless the user
specifies another name. Prefer a stable shape such as:

```text
codex/<plan-token-lower>-<task-lower>-<lane>
```

Use a new worktree path for the lane. Do not reuse an old detached worktree or a
worktree with unrelated changes. Before dispatch, verify:

- the integration and worker commits match the recorded base;
- the worker tree is clean;
- no other worktree owns the worker branch;
- the lane's write and prohibited paths are explicit; and
- enough disk and build capacity remain for the planned validation.

Never rebase a worker. A worker may make bounded local commits on its feature
branch. It does not push, edit progress ledgers, close tasks, approve baselines,
or integrate another lane unless the coordinator explicitly assigns that exact
action and the base orchestrator permits it.

## Dispatch Contract

Every implementation worker prompt must include:

```text
Use the repository startup contract and the applicable plan.
Base commit: <full hash>
Integration branch: <branch>
Worker branch/worktree: <branch and absolute path>
Authority: <independent plan, plan lane, or master bug-report side lane>
Tracking identity: <stable plan task id/title or bug finding id/title>
Commit convention: <resolved current DONE/TOTAL header or normal bug subject>
Lane kind and objective: <bounded outcome>
Dependencies already satisfied: <commits/evidence>
Reversible decision envelope: <choices the worker may make without waiting>
Allowed write scope: <exact files/directories>
Prohibited shared scope: <exact files/directories>
Required focused evidence: <commands/assertions/artifacts>
Do not edit plan progress, SessionState, WORK_LEDGER, baselines, or other lanes.
Commit locally when the lane is complete and return the full hash, changed-file
list, validation output, residual risks, and actual thread/session id.
```

Require every worker to inspect its own `git status` before editing and before
committing. It must treat unexpected dirty files as user-owned and stop that
lane rather than overwriting them.

Worker commits that do not close the selected task retain the current completed
count in the required plan header. Only the coordinator's accepted closure
commit advances the plan count. Master bug-report side-lane commits use the
normal bug convention defined above and never carry a plan progress header.

## Coordinator Work During The Worker Lane

While the attached worker runs, the coordinator should perform a genuinely
disjoint useful lane, such as:

- implementing the integration-owned half of a frozen contract;
- adding tests in a different owner-specific file;
- preparing read-only inventories or targeted evidence;
- examining call paths and future merge impact; or
- running an isolated CPU check that does not consume worker outputs.

Do not race the worker on the same design decision or prepare speculative code
for a later plan phase. Continue sending concise progress updates at the normal
cadence.

## Fan-In And Integration

When a worker completes:

1. Verify its reported commit, status, diff, file scope, and validation output.
2. Reject scope leakage, unrelated cleanup, ledger edits, baseline changes, or
   work based on the wrong commit.
3. Inspect behavior-sensitive code and tests directly; a worker summary is not
   source review.
4. Integrate with a normal merge or another non-history-rewriting Git operation
   permitted by the base orchestrator.
5. Resolve conflicts only in the coordinator worktree. A conflict in a declared
   exclusive write scope is evidence that the wave definition was wrong; record
   and correct the scheduling rule before another wave.
6. Run the cumulative focused evidence required for the integrated behavior.
7. The coordinator applies the touched-source comment audit and ownership
   reviews required by `AGENTS.md`; never delegate this step to a standalone
   comment-audit worker.
8. Use a separate read-only rubber-duck reviewer only at the cadence allowed by
   the base orchestrator.
9. Run terminal plan-completion validation once for each plan when that plan is
   actually closing on the integrated tree.
10. Update all affected plan progress and SessionState, then commit, push, and
    finish each ledger task exactly as the base orchestrator requires.

Remove a worker worktree only after its commit is integrated, the branch state
is verified, and no evidence exists solely inside that worktree. Never use a
destructive cleanup command against an unresolved or computed broad path.

## Failure And Blocker Handling

A failed worker lane does not automatically block its owning plan. The
coordinator should inspect its evidence, retry with a narrower lane when safe,
or complete the work locally. When the underlying plan task is genuinely
blocked, follow the base orchestrator's Blocker Continuation procedure and keep
advancing only dependency-safe work. Stop conflicting plan lanes, but keep the
dedicated bug agent working from its separate branch and worktree whenever an
eligible independent row exists.

Stop only the affected fan-out and return those plans to serial integration
when the following applies; keep every unrelated ready plan running:

- the contract is still changing;
- write scopes overlap or an undeclared shared file appears;
- a worker needs a new owner decision, baseline approval, or scope expansion;
- a lane exposes a dependency on a later phase;
- validation artifacts or runtime resources cannot be isolated;
- ledger accounting cannot remain exact; or
- integration/rework consumes the expected concurrency benefit.

## Final Handoff Additions

Provide every handoff item required by the base orchestrator. Add:

- each parallel wave's base commit, dependency graph, lane objective, worker
  thread/session id, worktree, branch, and commit;
- declared versus actual changed-file scopes;
- fan-out, worker, coordinator, fan-in, conflict-resolution, and validation
  timings without summing overlapping wall time;
- exact main and worker usage/cost rows from the live ledger;
- conflicts, rejected lane output, retries, or serial fallbacks; and
- the concrete wall-clock evidence for whether parallel execution helped.

Do not claim a speedup from agent count. Compare elapsed execution and rework
against a meaningful serial phase or the coordinator's measured critical path.
