---
name: parallel-orchestrator
description: Extend SkullbonezCore's repository orchestrator with a slot-saturating queue that runs at most one distinct plan per subsystem, supports multiple independent bugs across different unleased subsystems, and maps every active worker to an isolated branch and worktree before coordinator fan-in. Use when the user invokes the parallel orchestrator, asks to parallelize MASTER-PLAN or Agentic/Plans execution, requests multiple implementation agents or worktrees, asks to fix eligible master bug-report rows alongside planned work, or asks to reduce plan wall-clock time while preserving dependency, validation, review, commit, push, blocker, ledger, and handoff rules.
---

# Parallel Orchestrator

Run the normal repository orchestrator with a native parallel queue. Fill every
slot whose expected wall-clock saving exceeds its merge and validation overhead,
give every occupied slot one isolated worktree, and fan accepted commits into
the integration branch through the main orchestrator. Parallelism changes
scheduling, not the delivery contract.

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

> Sub-agents may own separate dependency-ready plans, independent lanes within
> one plan, or independent bug fixes. Every occupied agent slot owns exactly one
> isolated writable worktree. Distinct active plans never share a subsystem
> lease, and active bugs use different subsystems from every active plan and bug.
> The coordinator maximizes useful occupancy while retaining dependency analysis,
> file leases, integration, acceptance, validation selection, ledger truth, plan
> progress, commits, pushes, blocker records, and final reporting.

Invoking this skill explicitly supplies that sub-agent/worktree authorization.
It does not authorize any of the following:

- starting a plan or phase before its direct prerequisites or acceptance
  boundary permit it;
- changing a baseline, golden, rule, exception, dependency direction, or owner
  decision without the authorization required by the repository; active Physics
  plans already have standing authorization for governed goldens only through
  the exact-candidate, append-only retained-runtime lane;
- committing or pushing directly on `main` without explicit confirmation;
- merging a pull request, rebasing, force-pushing, or rewriting history; or
- weakening validation, review, comment, allocation, determinism, or handoff
  requirements.

MASTER-PLAN position is priority, not a dependency edge. Use it to allocate
scarce slots and order fan-in, but start a later-listed plan whenever no direct
dependency, subsystem lease, or credible merge conflict requires waiting. An
idle slot is correct only when every remaining lane is blocked, conflicts with
an active subsystem/resource lease, or is too small to repay fan-out/fan-in.

### Reasonable Slot Saturation And Subsystem Leases

Discover the live agent-slot capacity from the current runtime; never bake a
slot count into this skill. The coordinator occupies one slot and owns the
integration worktree. Map every other occupied slot one-to-one to one worker and
one writable worktree for the complete lifetime of that lane. Never let two
active agents share a worktree or one active agent write through two worktrees.

Before dispatch, derive the currently dispatched phase or lane's lease set from
its actual write scope, production owners, retained state, public contracts,
tests, project metadata, fixtures, baselines, and generated files. Recompute it
at every phase transition or scope expansion. Do not union future phases into a
current lease, and do not turn a wholly read-only coverage path into a production
owner lease. Use real ownership boundaries, not only the first directory in a
path. A phase or lane leases every owner it can mutate during that dispatch.

When the master bug report exists, parse the distinct non-empty `subsystem`
values from `Agentic/Bugs/master_bug_report.csv` at the start of every wave and
record that exact set in the wave manifest. Treat those strings as canonical
bug-subsystem aliases, not a closed-world package list. Map a plan or bug to
every matching alias. When a real affected owner has no current bug row, add a
normalized path-owner lease such as `path-owner:Runtime/UI` or
`path-owner:Runtime/Tools`; never coerce it into an unrelated CSV value. Add
Add the applicable canonical `resource:repo:`, `resource:path:`, or
`resource:facility:` lease for shared mutable build outputs, project manifests,
GPU execution, baselines, or other exclusive facilities.

Canonicalize every non-CSV lease before comparison:

- For a source owner, start from its repository-relative owner root below
  `SkullbonezSource/`, strip that prefix, use `/`, trim trailing separators, and
  record `path-owner:<root>` (for example `path-owner:Runtime/UI`). On Windows,
  compare case-insensitively. Equivalent roots and ancestor/descendant roots
  collide, so `path-owner:Runtime` conflicts with `path-owner:Runtime/UI`.
- For a shared tracked file, strip the worktree prefix and record its canonical
  repository-relative path as `resource:repo:<path>`. Worktree location cannot
  hide a merge collision.
- For a mutable external output, record its resolved physical path as
  `resource:path:<absolute-path>` using platform path-comparison semantics.
  Different worktree-local outputs therefore remain distinct.
- For a non-path facility, use one lower-kebab identity such as
  `resource:facility:gpu-dx12` or `resource:facility:physics-baseline-writer`.

Record the normalized lease set in the wave manifest and worker prompt. Reject
an unnormalized, equivalent, or overlapping spelling before dispatch.

Acquire a `resource:*` lease only for the exact edit or command window that can
mutate it, and release it immediately after the output is stable. A plan does
not retain GPU, baseline, performance, build-output, project-manifest, or
terminal-validation resources merely because a future phase will use them.

- Run at most one distinct active phase/lane whose production lease set contains
  the same current CSV alias or a colliding canonical path-owner lease. Do not invent broad
  aggregate aliases. When ownership is ambiguous, conservatively lease every
  plausible exact alias and path owner. Concurrent commands or edit windows
  sharing a `resource:*` lease remain serial even when their surrounding phases
  continue. Git text-merge predictions never override either collision.
- Permit multiple lanes inside the same active plan only after its shared
  contract is frozen, write/test scopes are disjoint, and saved execution time
  is likely to exceed integration and cumulative-validation cost.
- After assigning dependency-ready plans from unleased subsystems, fill useful
  remaining slots with eligible bugs from different unleased subsystems. Run at
  most one active bug per subsystem.
- Use a read-only investigation or evidence lane only when it advances a real
  acceptance decision. Never manufacture work merely to display full occupancy.

Treat `RAGDOLL_PHYSICS` and `GAME_UI_COMPONENTS` as parallel-compatible plan
families whenever their phase-local direct prerequisites are satisfied. Physics
normally leases `Physics`, adding `path-owner:Runtime/Tools` only while changing
its instrumentation. UI normally leases `UI Library` plus the exact Runtime
product packages established by UI0. Displaying a detached Physics value,
linking both libraries into the application, or running a cross-subsystem gate
does not by itself create a production lease. If either lane edits their shared
UI/diagnostics contract, project/test manifests, or the other lane's package,
expand the lease before editing. Serialize shared baseline, GPU, performance,
and terminal validation after fan-in; do not serialize the whole plans for those
command-level resources.

Recompute dependencies, subsystem leases, machine-resource pressure, and
expected merge overhead whenever a lane finishes or exposes new scope. Refill a
freed slot promptly when a worthwhile safe lane exists.

### Scheduling Regression Matrix

Whenever this scheduling policy changes, review one frozen wave against these
fixtures before dispatch:

- accept one `RAGDOLL_PHYSICS` lane leasing `Physics` beside one UI1/UI2 lane
  leasing `UI Library`, with separate agents, worktrees, and mutable outputs;
- reject a second distinct Physics plan or second distinct UI plan in that wave;
- reject a Replay bug beside UI3 when both lease `Runtime Replay`;
- reject any otherwise-disjoint pair whose path-owner or mutable resource lease
  intersects; and
- reject aliasing spellings such as `Runtime/UI`,
  `SkullbonezSource\Runtime\UI`, or case variants until they normalize to one
  canonical lease;
- accept disjoint builds only after resolving their intermediate, binary, PDB,
  cache, report, and artifact paths inside separate worktrees.

Record the pass/reject result in the wave manifest or handoff. These fixtures
prove scheduler behavior; they do not waive the live plan/lease derivation.

### Reversible Decisions During Fan-Out

Apply the base skill's Reversible Decision Autonomy before calling a contract
unresolved. The coordinator should freeze a reasonable provisional contract
and fan out instead of waiting for a small reversible choice. Give each worker
a decision envelope; within it, the worker may choose local constants or test
shapes, pin them with focused evidence, and report them for later revision.

For example, an unspecified motion threshold may provisionally use `0.1`
metres per Physics tick to promote and `0.075` metres to demote, independent of
collider thickness. Keep the wave running and notify the user afterward. Pause
only the affected lane for a true approval boundary; eligible bug-pool workers
and other independent lanes continue.

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

## Concurrent Master Bug-Report Pool

When `Agentic/Bugs/master_bug_report.csv` exists, use as many remaining worker
slots for independent bug fixes as the subsystem leases and fan-in cost justify.
Multiple bugs may run concurrently, but each must lease a different subsystem
that is not leased by an active plan or another bug. A primary-plan blocker does
not suspend otherwise eligible bugs in other subsystems. The pool is a side
lane, not a second source of plan priority, and it has no fixed reserved size.

Run every bug through its own agent, fresh branch, and isolated worktree. Never
execute a bug in the coordinator worktree or attach it to a plan worker's branch.
When a bug finishes, preserve or integrate it at its declared checkpoint, then
retriage the pool and refill any worthwhile safe slots.

Before using this pool, read `references/bug-pool.md` completely. It owns the
eligibility forecast, ranking, coordinator-atomic assignment, lease-expansion
stop, one-bug-per-branch execution contract, and safe checkpoint fan-in.

The coordinator assigns one exact finding and complete lease to each worker;
workers never self-select from a shared pool. If the worker discovers another
subsystem, it stops before editing that owner and requests expansion. Never fan
in a bug while an overlapping plan is establishing behavior-sensitive evidence.

## Bootstrap Before Fan-Out

Complete the base orchestrator bootstrap first:

1. Read the startup files and inspect the dirty tree.
2. Resolve and verify the Night Runner integration branch.
3. Create or reuse the persistent orchestration goal.
4. Start the base work ledger.
5. Inventory the current exact subsystem vocabulary and read candidate plans
   deeply enough to derive their complete lease sets.
6. Build a dependency, subsystem-lease, resource, and merge-overhead graph for
   every unfinished live plan.
7. Select the largest worthwhile set whose dependency edges are satisfied,
   subsystem lease sets are pairwise disjoint, resources are isolatable, and
   expected savings exceed fan-out/fan-in cost.
8. Start and account for every selected task before implementation dispatch.

Do not create worker branches from uncommitted coordinator work. First preserve
pre-existing dirty files as user-owned, then establish the exact clean commit
that every lane will use as its base.

## Build A Parallel Wave

Treat the unfinished MASTER queue and each selected plan as one dependency
graph. Fill worthwhile slots with the highest-priority ready plans from distinct
subsystems, then safe lanes and bugs under the lease rules above. Do not
serialize plans merely because one appears later in MASTER, but never run two
distinct plans from the same subsystem concurrently. Give each dispatched
worker lane its own branch and writable worktree. A plan with multiple permitted
internal lanes therefore uses one worktree per occupied worker slot; lanes never
share a plan-level worktree. Merge completed branches only through the main
orchestrator. Before dispatch, answer:

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
comment standard once while implementing its touched files. The coordinator
does not reopen the integrated tree for a separate comment audit. The base
orchestrator's independent rubber-duck reviews implementation and may report a
materially false ownership, sequencing, lifetime, units, or hazard claim, but
does not perform cosmetic comment review.

Record the wave in coordinator commentary before dispatch. Include the frozen
base commit, dependency edges, lane owners, write scopes, prohibited shared
files, required evidence, and fan-in order.

### Eligibility Gate

A lane may run concurrently only when:

- every dependency that defines its inputs is already complete;
- no distinct active plan or bug already leases any subsystem the lane needs;
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
- baseline transitions, golden files, phase artifact manifests, and final
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

Before concurrent build or test commands, resolve every mutable intermediate,
binary, PDB, cache, report, and artifact output path. Each must reside inside
its worker worktree or be explicitly unique to that lane. If two lanes resolve
any shared mutable path, lease that resource exclusively and serialize them.
Never infer build isolation from Git worktree isolation alone.

## Ledger-Compatible Concurrency

Preserve the base work ledger as the sole orchestration ledger. Never invent
token counts, costs, worker identities, or overlapping elapsed time.

Do not treat the ledger's attached-worker arity as a worktree limit. Worktree
capacity and live-agent concurrency are separate concerns:

- map each occupied agent slot to exactly one writable worktree, including the
  coordinator slot's integration worktree;
- retain a completed evidence worktree only until its evidence is preserved and
  its branch is fanned in or explicitly deferred; and
- bound simultaneous execution by available agent slots, machine resources,
  dependency safety, and the ledger's ability to account for every live
  session, not by a fixed worktree count in this skill.

Every occupied slot has exactly one writable worktree and every writable
worktree has at most one active agent. Do not precreate an unbounded waiting
pool. A completed evidence tree may remain temporarily during fan-in, but it is
not an active slot. Open distinct task/step accounting for every live worker. A
single-active-task or single-worker ledger seam is a tooling defect to repair,
not permission to serialize merge-safe work. Never invent usage or reuse one
writable tree across agents.

For every live worker, transition to an accountable step before dispatch,
attach its real thread/session id using the supported ledger mechanism, and
fan in only after that worker finishes. Tool availability is never permission
to omit accounting.

At a run handoff, close an unfinished investigation or worker lane with the
base ledger's `stop-task` action and an exact preserved-state outcome. Never
bind an unrelated integration commit to a lane that produced no accepted
commit. The next run opens a new task id for its new worker session.

## Create Worker Worktrees

Create a worker worktree when its lane is ready to occupy a slot. Keep one
integration worktree for the coordinator and exactly one writable worktree per
active worker slot. A completed evidence tree may remain temporarily until
fan-in, but do not precreate an unbounded waiting pool. Do not serialize safe
work merely to conserve a fixed worktree count. Create each worker from the
exact frozen integration commit on a fresh local branch.
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
branch. It does not push, edit progress ledgers, close tasks, write goldens, or
integrate another lane unless the coordinator explicitly assigns that exact
action and the base orchestrator permits it. A Physics plan's standing authority
removes the owner-prompt blocker; it does not remove the coordinator's
single-writer lease over the golden and immutable artifact bundle.

## Dispatch Contract

Every implementation worker prompt must include:

```text
Use the repository startup contract and the applicable plan.
Base commit: <full hash>
Integration branch: <branch>
Worker branch/worktree: <branch and absolute path>
Authority: <independent plan, plan lane, or master bug-report side lane>
Tracking identity: <stable plan task id/title or bug finding id/title>
Subsystem lease: <complete exclusive subsystem set for this lane>
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
7. The coordinator reviews ownership evidence and relies on each source writer's
   one-pass comment work. Do not add a separate comment-audit or wording-fix
   phase; the independent rubber-duck checks implementation-level truth.
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
advancing only dependency-safe work. Stop conflicting plan lanes, but keep every
eligible bug in an unleased subsystem working from its separate branch and
worktree.

Stop only the affected fan-out and return those plans to serial integration
when the following applies; keep every unrelated ready plan running:

- the contract is still changing;
- write scopes overlap or an undeclared shared file appears;
- a worker needs a new owner decision, a non-Physics baseline approval, or scope
  expansion; a governed Physics golden transition is not a pause condition when
  its exact candidate and complete append-only bundle are ready;
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
