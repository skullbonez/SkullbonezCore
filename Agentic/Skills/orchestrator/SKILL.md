---
name: orchestrator
description: Run SkullbonezCore's persistent MASTER-PLAN implementation queue on a deterministic Night Runner branch, either serially in the main agent or as the integration owner for the parallel orchestrator. Use when the user asks for the orchestrator, night runner, nightrunner, overnight runner, queued plan runner, MASTER-PLAN runner, or asks Codex/Antigravity to complete one or more Agentic/Plans items, then continue through blockers, validate, commit, and push each accepted plan.
---

# Orchestrator

Coordinate the persistent SkullbonezCore MASTER-PLAN queue without the retired
repository-owned JSON/Python state machine. Resolve the Night Runner branch and
goal before edits, implement plans in the main agent unless the parallel skill
is active, continue past documented blockers, save independent `$rubber-duck`
critique for a major completed plan or whole-job checkpoint, run required final
gates, and commit/push one accepted slice or blocker record at a time.

## Inputs

Default to `Agentic/Plans/MASTER-PLAN.md` when the user invokes the orchestrator
without naming a plan. Treat `masterplan.md`, `master-plan.md`, and any casing
variants as aliases for that exact authoritative path.

Accept explicitly named plans in the user's requested order. Normalize other
bare names by adding `.md`, and resolve them under `Agentic/Plans/` unless the
user gives a path. If a plan cannot be found, search by stem with
`rg --files Agentic/Plans` and ask only if multiple plausible matches remain.

## Night Runner Bootstrap

Resolve and verify the branch before any implementation edit:

1. If the user explicitly gives a branch name, use that exact spelling.
2. Otherwise run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Agentic/Skills/orchestrator/scripts/resolve_nightrunner_branch.ps1 -Apply
```

3. Reuse the current branch when it is already a canonical or legacy Night
   Runner branch. Recognition is case-insensitive and includes
   `nightrunner-*`, `night-runner-*`, and `*-night-runner`.
4. When the current branch is not a Night Runner branch, reuse today's branch
   if it exists locally or under `origin`; otherwise create it from the current
   tip. The canonical format is
   `nightrunner-<ordinal-day>-<MMM>-<YY>`, with an uppercase English month and
   local calendar date. For 2026-07-22 it is `nightrunner-22nd-JUL-26`.
5. Run `git branch --show-current` again and require it to equal the resolver's
   output before editing. Never improvise another date spelling or duplicate a
   Night Runner branch.

## Startup

Follow the SkullbonezCore startup contract before any edit:

1. Read `AGENTS.md`, `README.md`, `Agentic/README.md`, and
   `Agentic/SessionState.md`.
2. Run `git status --short --branch`.
3. Treat pre-existing dirty files as user-owned.
4. State the impact area, classify the work with the risk-proportional cadence
   below, and say which evidence is required now versus deferred to terminal
   closure.
5. Start a wall-clock timer and report elapsed time in the final handoff.

Do not force-push, rebase, rewrite history, merge PRs, or commit/push on
`main` without explicit confirmation.

## Persistent Goal And Queue

Treat an explicit orchestrator invocation as authorization to create a goal for
the orchestration run. After branch verification and before plan edits:

1. Inspect the current goal when goal tools are available.
2. Reuse an active goal that already covers completing
   `Agentic/Plans/MASTER-PLAN.md` on the selected Night Runner branch.
3. Otherwise create this objective:

```text
Complete Agentic/Plans/MASTER-PLAN.md on <branch> in direct dependency order; maximize worthwhile dependency-ready, subsystem-lease-disjoint concurrency when the parallel policy is active; validate, commit, and push accepted slices; record genuine blockers and continue without stopping.
```

4. If an unrelated unfinished goal prevents goal creation, record that tooling
   conflict as a blocker and continue the MASTER-PLAN queue. Create the intended
   goal as soon as the goal slot becomes available; do not abandon repository
   work because goal metadata is temporarily unavailable.
5. Keep the goal active while any actionable MASTER-PLAN work remains. One
   blocked item never blocks or completes the whole goal.

Use `Agentic/Plans/MASTER-PLAN.md` as the live dependency graph. Read its
Current Execution Priority, Portfolio Progress Ledger, plan-state tables, and
linked `TODO/` plans. Plain orchestration selects the next unfinished,
dependency-safe item in binding order. Explicit parallel-orchestrator
invocation activates its native queue policy: fill as many slots as are worth
the fan-out/fan-in cost, map every occupied agent slot to one isolated
worktree, and lease each canonical bug subsystem and unmatched path owner to at
most one distinct active write phase/lane. Recompute leases at every phase
boundary; future phases and read-only coverage do not hold production leases.
Acquire exclusive resource leases only around their exact edit/command windows.
Lanes whose current lease sets intersect remain serial even when their file
lists do not. Remaining slots may run eligible subsystem bug batches
concurrently only in different, unleased owners.
Binding order allocates scarce slots and orders fan-in; list position alone is
not a dependency. Ignore `Agentic/Plans/WNF/` unless the owner reactivates an
item, and re-read MASTER-PLAN after every pushed slice.

## Reversible Decision Autonomy

Do not turn an under-specified but reversible implementation choice into a
blocker. When no user or accepted contract fixes the answer, use best
engineering judgement, choose the smallest testable assumption that preserves
stated acceptance, and keep working. Goal and overnight runs optimize for
useful recoverable progress: a bounded wrong assumption can be revised in a
later commit, while idle time cannot be recovered.

- Proceed autonomously when the choice is local, reversible with a normal
  follow-up commit, has no destructive or external effect, and focused tests
  can expose a bad choice. A generic `owner decision` or `TBD` placeholder does
  not make a tuning constant blocking unless the user explicitly reserved it.
- Preserve optionality: isolate the choice behind a named constant or narrow
  policy seam, pin its current behavior in tests, and avoid spreading it through
  unrelated owners.
- Report it at the next progress update and in the final handoff as
  `Provisional decision:` with the chosen value, rationale, evidence, affected
  behavior, and exact revision seam. This is notice for later revision, not a
  synchronous request for permission.
- If evidence rejects the assumption, revise it autonomously. If the user later
  chooses differently, correct it in the next normal commit; never defend sunk
  work, rewrite history, or leave the run idle.

For example, if motion eligibility needs hysteretic travel thresholds but no
accepted contract supplies numbers, choose a simple thickness-independent
policy such as `0.1` metres per Physics tick to promote and `0.075` metres to
demote, assert both boundaries, and continue. Report those constants afterward
as provisional and easy to revise. Waiting overnight for that local numeric
choice is an orchestration failure.

Still stop for choices that require authority the run does not have: destructive
or externally visible actions, security or data-loss risk, irreversible
migrations, dependency/schema/rule changes that require owner approval,
non-Physics baseline or golden transitions whose governing rule requires owner
approval, or a user instruction that explicitly says to wait. Block only the
affected path and continue every independent safe item under Blocker
Continuation.

An active Physics plan's governed goldens are not an approval blocker. Apply the
standing archived automated lane from `AGENTS.md`: explain the behavior change,
bind the exact candidate SHA-256, preserve complete old/new launch payloads in a
new immutable transition bundle, rerun the mapped gate, and continue without an
interactive phrase or per-update pre-approval. Never use that authority to
refresh an unexplained failure.

## Live Work Ledger

Use the batch-owned live ledger for the complete orchestrator goal. It writes
`Agentic/Plans/WORK_LEDGER.csv` beside `MASTER-PLAN.md`; the path is ignored by
Git because the final post-push hash update cannot be part of the commit whose
hash it records. Never stage or hand-edit this runtime artifact.

Every batch call reads the exact cumulative token counter for
`CODEX_THREAD_ID` (or Antigravity conversation ID / transcript stream),
captures a local ISO-8601 timestamp with timezone, and atomically rewrites
valid CSV plus its final embedded recovery-state row. A failed call is an
orchestration blocker: do not replace exact telemetry with an estimate or an
in-memory row. The unfinished row identifies the live step, so the owner can
inspect the ledger at any time while work is running.

Run `work_ledger.bat show` before inspecting the CSV. `show` refreshes the open
row's current elapsed time, input/output/cached-input counters, API-cost
estimate, and master-plan progress without closing the step.

Start the ledger immediately after branch/goal bootstrap:

```bat
Agentic\Skills\orchestrator\scripts\work_ledger.bat start-goal -Goal "MASTER-PLAN"
```

Start every selected plan task before reading, editing, or investigating it.
The call opens both the task and its first step:

```bat
Agentic\Skills\orchestrator\scripts\work_ledger.bat start-task -Task "<PLAN>-T<n>" -Title "<task title>" -Step "implementation-01" -Kind implementation -Label "Implementation"
```

Use one `transition` call at each boundary. It closes the active step and opens
the next step with one shared parent-session timestamp/counter, so no parent
tokens or wall time are lost or double-counted:

```bat
Agentic\Skills\orchestrator\scripts\work_ledger.bat transition -Task "<PLAN>-T<n>" -Outcome "ready for review" -Step "rubber-duck-01" -Kind rubber-duck -Label "Rubber duck pass 1"
```

For a newly created rubber-duck agent/thread/subagent, transition before launching it,
then attach its returned thread/session/conversation id with a zero baseline. Zero is the
exact start of that new reviewer session, including its prompt and context:

```bat
Agentic\Skills\orchestrator\scripts\work_ledger.bat attach-worker -Task "<PLAN>-T<n>" -WorkerThreadId "<reviewer id>" -WorkerBaselineZero
```

Use the reviewer's actual `CODEX_THREAD_ID` or Antigravity `ConversationId`, not
an orchestration handle that cannot be resolved under `.codex\sessions` or
`.gemini\antigravity\brain\<id>\.system_generated\logs\transcript.jsonl`. If the launch
API returns only a handle, have the reviewer read and return `$env:CODEX_THREAD_ID` or
its assigned conversation ID; attaching it afterward with a zero baseline still accounts
for that complete new session.

When reusing an existing reviewer session, attach or open the step with
`-WorkerThreadId` and omit `-WorkerBaselineZero`; the helper snapshots its
current cumulative counter. Close every rubber-duck step with `-Findings <n>`.
Count every enumerated item under the review's Findings and Missing evidence
sections. Put the verdict in `-Outcome`. Use `finding-fix`, `rubber-duck`, and
`validation` kinds for repeats so the ledger derives duck-pass count, fix-cycle
count, total findings, reviewer input/output/cached-input counters and cost, and
cumulative validation duration.

At minimum, record implementation/investigation, every rubber-duck pass, every
finding-fix cycle, every validation actually run, and commit/push as separate
steps. When the cadence below makes review or validation inapplicable, record
that outcome without opening a fake work step. Use `other` for another material
phase. After any required validation, transition to `commit-push`; after the
commit has been pushed, close the task with its commit:

```bat
Agentic\Skills\orchestrator\scripts\work_ledger.bat finish-task -Task "<PLAN>-T<n>" -Outcome "pushed" -Commit HEAD
```

`finish-task` resolves the full commit hash and refuses to close until that
commit is an ancestor of the configured upstream. After the final task, close
the goal immediately before the final handoff:

```bat
Agentic\Skills\orchestrator\scripts\work_ledger.bat finish-goal -Outcome "complete"
```

When an investigation or interrupted lane ends without a repository commit,
close only that ledger task with an explicit handoff outcome; do not attach an
unrelated commit merely to satisfy accounting:

```bat
Agentic\Skills\orchestrator\scripts\work_ledger.bat stop-task -Task "<TASK>" -Outcome "<exact preserved state and continuation>"
```

`stop-task` closes usage and elapsed time without claiming plan progress. A
later worker starts a new task id from the preserved branch/worktree state.

The ledger groups step rows beneath each task and maintains task/run summaries
with elapsed time; explicit input, output, and cached-input counters; main and
reviewer splits; duck passes; fix cycles; findings; validation time; outcomes;
full commit hashes; portfolio and active-plan progress; and estimated API cost
in USD. Never report an unqualified `tokens` or `total tokens` column. Codex
input includes the cached-input subset, so calculate cost as uncached input
(`input - cached`) at the input rate, cached input at the cached rate, and
output at the output rate. Record the model, rates, Standard/Short pricing
basis, and official pricing URL in the CSV. Unknown models are blockers rather
than occasions to guess a rate. The embedded state survives context compaction
and process restart.

### Completion Ledger Artifacts

After `finish-goal` closes a completed orchestration run and before the final
handoff, use `Agentic/Skills/render-work-ledger/SKILL.md` with its standalone
HTML mode to render the final live ledger as both HTML and PNG. Store both
tracked artifacts in `Agentic/Ledgers/` with the current branch as their shared
filename stem. Make the stem lowercase and replace `/`, `\\`, spaces, and
unsupported filename characters with hyphens; for example, `codex/Replay Fix`
becomes `codex-replay-fix.html` and `codex-replay-fix.png`.

Refresh the live CSV with `work_ledger.bat show`, reconcile the renderer's JSON
summary against that CSV, render the HTML at a wide desktop viewport, and
visually inspect the PNG for clipping or unreadable labels. Stage both artifacts
and create and push one terminal ledger-artifact commit after the completed goal
commit; never stage the ignored `Agentic/Plans/WORK_LEDGER.csv`. Report clickable
paths to both artifacts in the final handoff. If the run stops without completing
the goal, preserve the live CSV state but do not publish completion artifacts.

## Blocker Continuation

Do not stop the orchestration run when one item is blocked. First exhaust safe,
in-scope attempts and alternatives. When a blocker is genuine:

1. Leave the owning task or phase incomplete.
2. Mark its MASTER-PLAN state `Blocked` and record the owner, cause, evidence,
   exact unblock condition, unchanged verified count, and affected dependents
   in the owning plan, MASTER-PLAN's existing state/next-action fields, and
   `Agentic/SessionState.md`. A blocker that is not in SessionState is invisible
   to the next agent, which reads it at startup and would pick the blocked item
   straight back up.
3. Never stage broken or unvalidated implementation. If the blocked attempt
   left unsafe partial changes, preserve bounded evidence, then remove only
   changes created by that attempt. Never alter pre-existing user-owned work.
4. Commit and push the documentation-only blocker record with the appropriate
   `MASTER-PLAN` progress subject. Do not count it as completed work or silently
   change the portfolio denominator.
5. Skip blocked dependents, select the next independent dependency-safe item,
   and continue immediately.

Finish only when every actionable item is complete or every remaining item is
explicitly blocked and no independent safe work remains. Mark the overall goal
complete only for true MASTER-PLAN completion. Mark it blocked only when goal
tool rules permit it and the entire remaining queue is at an impasse; otherwise
leave it active and report the blocker inventory.

## Sub-Agent Tools

In plain mode, use sub-agents only for independent `$rubber-duck` review at the
end of a major plan/checkpoint or whole job. Earlier review is allowed only when
the user explicitly asks for one, or when the same failure mode has repeated and
independent critique is the cheapest way to get unstuck. Do not run a review per
edit, checklist row, source file, commit, or small slice, and do not dispatch
plain-mode implementation, cleanup, validation, staging, committing, or pushing
to a sub-agent.

When the parallel orchestrator is active, its dispatch contract replaces that
plain-mode implementation prohibition. It may use sub-agents for plan lanes and
cohesive subsystem bug batches, with exactly one isolated writable worktree per
occupied agent slot, no two distinct active plans leasing the same subsystem,
and no bug batch sharing a subsystem with an active plan or batch. The main
orchestrator remains the sole integration owner.

Use hosted collaboration actions such as `spawn_agent`, `send_message`,
`followup_task`, `wait_agent`, and `list_agents` for managed sub-agents. Use
user-owned Codex thread tools only when the user explicitly requests a separate
thread. If a review tool creates a separate worktree, keep it read-only.

### Ownership Evidence For The End-Of-Plan Review

Before dispatching the end-of-plan `$rubber-duck` pass on a plan that changed
C++ source, run the two focused reports and include their output in the
review prompt:

```bash
python tools/check_build_config_consistency.py --repo .
python tools/check_source_design.py --repo .
```

`AGENTS.md` delegates its build-configuration, dead-code, struct,
capability-slice, local-refactor, wide-signature, and function-shape rules
to this review, so the reviewer
needs the evidence rather than an impression. A review that returns clean
without answering the ownership questions in
`Agentic/Skills/rubber-duck/SKILL.md`, including one-call-helper complexity
evasion, is incomplete: send it back rather than closing the plan on it.
`validate_fast` already fails on an unruled or stale row, so a green gate means
every row has a current ruling — not that every ruling is right, which is what
the review is for.

## Rubber-Duck Accounting

Default to zero rubber-duck steps while ordinary implementation is in progress.
Assign every pass a stable step id such as `rubber-duck-01`,
`rubber-duck-02`, and so on. Use the live-ledger transition and worker-session
attachment sequence above for every pass; do not maintain a second in-memory
review table. The worker-session delta is the exact reviewer usage, while the
same row separately retains main-agent orchestration usage, elapsed wall time,
finding count, and verdict. Keep this accounting separate from SkullScope
diagnostics accounting; it measures agent-session usage, not repository
artifacts or validation logs.

## Subsystem Bug Batches

When an activated bug ledger or bug-fix campaign contains multiple open bugs,
group eligible bugs by their canonical subsystem and treat each cohesive
subsystem group as the default implementation and review unit. Diagnose, fix,
and add focused evidence for the whole batch before opening its rubber-duck
step. Do not run one rubber-duck pass per bug merely because the ledger has one
row per finding.

Keep a subsystem together when its bugs have compatible ownership, bounded
combined scope, compatible acceptance evidence, and one understandable
validation strategy. Split it into the fewest coherent batches when the full
group would cross independent owners, mix incompatible high-risk contracts or
baseline decisions, require unrelated exclusive facilities, create an
unreviewably large change, or otherwise make one pass unsafe. Complexity is an
escape hatch, not a reason to return automatically to one bug per batch.

Use one independent read-only rubber-duck review after the complete subsystem
batch is implemented. Give the reviewer every finding id, acceptance criterion,
changed path, and focused witness; require a verdict for each bug plus any
cross-bug interaction. Fix the batch's blocking findings together. Repeat the
review only when those fixes materially change the reviewed risk area or the
reviewer explicitly requires a follow-up. Preserve one finding identity per
commit unless multiple bugs share one inseparable root cause and acceptance
witness.

## Risk-Proportional Review And Validation

Choose review and validation cadence from the meaning and risk of the change,
not from a ritual applied to every task. File count and LOC are useful context,
but neither is the decision by itself. Consider behavior changed, subsystem
criticality, reversibility, observability, ownership/concurrency impact, golden
sensitivity, and how much unvalidated work has accumulated.

Use these qualitative cadences:

1. **Documentation, comments, bookkeeping, or mechanical metadata only:** use
   targeted inspection and `git diff --check`; run zero rubber-duck passes and
   no repository validation unless a specific documentation checker owns the
   edited contract.
2. **Very small, low-risk implementation slice inside a plan:** skip rubber-duck
   review. Run only the narrow compile, test, launch, or inspection that answers
   a concrete uncertainty; when the change is obvious and already covered by a
   later plan gate, defer validation to the next meaningful integration
   checkpoint.
3. **Small cohesive plan:** normally accumulate implementation, then perform at
   most one review and one mapped validation pass at terminal closure. A small
   plan that is documentation-only or mechanically provable may require neither.
4. **Large or multi-owner plan:** validate at meaningful integration checkpoints
   and before commits that publish a substantial coherent slice. Use focused
   owner gates during the plan and one terminal full gate. Rubber-duck only a
   major completed checkpoint or the final integrated plan, not each task.
5. **High-risk behavioral work:** before every commit that changes core Physics,
   determinism, Replay serialization/restore, DX12 resource/fence lifetime,
   concurrency, allocation/growth privilege, durable scene state, or another
   golden-sensitive invariant, run the mapped focused gate that can detect that
   slice's failure. This requirement applies even when the textual diff is
   small. Reserve the full terminal suite and independent rubber-duck for the
   integrated end unless the user requests an earlier review or repeated
   failures make one cost-effective.

A commit boundary alone does not justify validation. A meaningful unvalidated
behavior boundary does. Conversely, do not defer a risky Physics or lifetime
change merely because it is one line. Before opening a validation or review
ledger step, state the concrete risk or question it will resolve. If no such
question exists, skip the step and record zero review passes or deferred
terminal evidence instead of manufacturing activity.

Do not rerun a gate when only documentation or plan bookkeeping changed after
that gate and its source inputs are unchanged. Do not repeat a clean rubber-duck
unless subsequent fixes materially changed the reviewed risk area or the
reviewer explicitly required a follow-up.

## Write Comments Once

Apply the repository comment standard while implementing each touched source
file. Do not open a later comment-audit step, reread every comment through a
separate skill, or spend a worker on wording review. The terminal rubber-duck
reviews the implementation as a whole and may block a materially false comment
about ownership, sequencing, lifetime, units, or a hazard; it does not run a
second style pass or request cosmetic rewrites. Mechanical Related-path and
glossary checks remain part of their existing validation gates and run once at
the mapped checkpoint.

The dedicated comment-audit skill remains available only when the user
explicitly requests a comment/subsystem audit. Ordinary implementation work
writes and verifies its comments once in the source-writing lane.

## Plan Loop

For each plain-mode plan, or each parallel-mode fan-in slice:

1. Select the task from MASTER-PLAN, resolve its stable task id/title, and call
   `work_ledger.bat start-task` before deeper plan reading, investigation, or
   implementation. The live file must show the task as in progress before work
   consumes time or tokens.
2. Read the plan enough to understand scope and required validation. Read the
   authoritative progress ledger in `Agentic/Plans/MASTER-PLAN.md`, resolve the
   owning plan's post-slice completed-task count and total, and include this
   fully resolved line in the active implementation prompt/task framing before
   edits. Both counts are plan-local; do not compute a cross-plan percentage.

```text
Required commit subject first line: <PLAN_NAME>, TASK <DONE>/<TASK_COUNT> — <ACTION SUMMARY>
```

   Recalculate it if scope or task completion changes before commit.
3. In plain mode, complete exactly that plan in the main agent and do not launch
   an implementation worker. In parallel mode, follow the parallel skill's
   multi-plan fan-out while this main orchestrator remains the integration owner.
4. Inspect the result with `git status --short` and targeted file reads or
   diffs.
5. For ordinary incremental slices, skip rubber-duck review and proceed to the
   risk-proportional validation decision in step 8. When review is required,
   transition to `rubber-duck-<nn>` before creating or messaging the reviewer.
   Launch a separate read-only rubber-duck review sub-agent only when the slice
   completes a major plan/checkpoint or whole job, when the user explicitly
   asks for review, or when repeated failures show that independent critique is
   needed:

```text
Use $rubber-duck to review the completed work for <plan-path> on branch <branch>.
Stay read-only. Prioritize bugs, behavior regressions, missing tests, validation gaps,
baseline mistakes, determinism risks, DX12 validation risks, and hot-path allocation concerns.
Return findings with file/line references and a clear verdict.
```

6. Attach the new reviewer thread id to the active ledger step immediately
   after creation. When its response arrives, count its findings and transition
   to `finding-fix-<nn>` before addressing blocking findings in the main agent.
   If there are no fixes, proceed directly to the step 8 validation decision.
7. Repeat the rubber-duck pass only if the fix changed meaningful behavior in
   the reviewed risk area or the reviewer requested a follow-up. Transition at
   both sides of every repeat so each pass and fix cycle is a separate live
   ledger row.
8. Apply the risk-proportional cadence above. Open a `validation` ledger step
   only when this slice needs evidence now; otherwise transition directly to
   commit/push and record that validation is deferred or not applicable. Use
   focused owner gates for intermediate high-risk or substantial integration
   commits. Do not run heavy multi-minute suites, graphics stress, or deep
   regression gates for ordinary low-risk increments. Concentrate those at the
   end of a small plan or in the terminal closure pass of a large plan, except
   where `AGENTS.md` maps a high-risk changed behavior to a pre-commit gate.
   Documentation-only changes require no validation. Run
   `tools\agent_validate.bat --plan-completion` once, after any appropriate
   independent review and immediately before the terminal commit that closes
   the entire implementation plan.
9. Update `Agentic/SessionState.md` whenever a task or phase completes, a plan
    closes, a blocker is recorded, or the portfolio denominator moves. This is a
    write step, not a read step: steps 10 and 11 stage and report the update, but
    neither creates it. At minimum the Current State table's objective and
    active/future progress figure, and the Live Queue's binding next task, must
    match the post-commit MASTER-PLAN ledger exactly. Recompute the progress
    figure from the ledger; never carry the previous value forward. A closed plan
    that left the live inventory under rule 4 changes both the numerator and the
    denominator, so the figure usually moves more than one task's worth.
10. Run `git status --short --branch` before staging.
11. Stage only files belonging to the completed plan and its required
    reports/session-state updates. Never stage the ignored live work ledger.
12. After required validation succeeds, or after recording that validation is
    deferred or not applicable, transition to the `commit-push` ledger step.
    Write the complete message to a file before invoking Git. The subject uses
    the required MASTER progress header followed by a concise action summary.
    The substantive body must contain non-empty `Why:`, `Ownership:`, `What:`,
    `Validation:`, `Baselines/Artifacts:`, and `Review:` sections with exact
    commands, results, counts or hashes where available. A worker sends this
    complete proposed message to the integration owner for approval; neither
    worker nor integration owner may commit from a subject-only `-m` command.
    Run the deterministic pre-commit body gate, then commit from that same file:

```bat
Agentic\Skills\orchestrator\scripts\work_ledger.bat verify-commit-message -MessageFile "<message-file>"
git commit -F "<message-file>"
```

    A failed body gate blocks the commit. Do not weaken, bypass, or defer it.
    Use the post-commit ledger values and update MASTER in the same commit
    whenever task completion or the portfolio denominator changes.
13. Push normally. Never force-push.
14. Call `work_ledger.bat finish-task -Commit HEAD` only after the push
    succeeds. This closes commit/push timing, revalidates the actual committed
    subject and six-section body as a post-commit backstop, verifies upstream
    containment, writes the full hash into the task group, and makes the
    completed ledger immediately queryable before advancing the queue.

In plain mode, advance after the current item is reviewed, validated, committed,
and pushed, or after its blocker record is pushed. In parallel mode, fan in any
ready plan while other dependency-, subsystem-lease-, and resource-independent
branches continue; one plan's review, validation, or blocker never idles
unrelated work.

## Validation Discipline

Do not run `tools\validate_*` scripts reflexively during normal iteration. They
are evidence gates, not progress rituals. During implementation, use focused
builds, launches, tests, or inspections only when they answer a specific
question. Before a substantial or high-risk commit, run the mapped focused
validation required by the risk-proportional cadence; before a trivial or
low-risk increment, defer it when the terminal gate will cover unchanged scope.

When validation is required, run it in a visible console when available and
mirror output to a log when practical. Never claim validation success without
command output. Quote the key result lines and log path in the handoff.

Use `AGENTS.md` as the source of truth for validation selection. If scope is
broad or unclear, accumulate every affected focused gate. `agent_validate` is
reserved for the terminal plan-completion cadence above.

## Final Handoff

Report:

- Branch name and push target.
- Plans completed, in order.
- Commit hashes and subjects for each plan.
- Main-agent implementation summary and rubber-duck sub-agent verdicts.
- Validation commands and key result lines, or "documentation-only, no
  validation required."
- Any skipped plan, blocker, dirty user-owned file, or residual risk.
- Goal status plus the next actionable MASTER-PLAN item, or proof that only
  explicitly blocked work remains.
- Confirmation that `Agentic/SessionState.md` matches the final MASTER-PLAN
  ledger, quoting the progress figure from both. They are the two documents a
  fresh agent reads first; if they disagree, the handoff is wrong regardless of
  how good the work was.
- Total elapsed wall-clock time and timings for long builds, validations,
  launches, or investigations.
- The live `Agentic/Plans/WORK_LEDGER.csv` path and its exact goal/task summary:
  elapsed time; explicit input, output, and cached-input counters with main and
  reviewer splits; Standard/Short API-cost estimate and rates; overall and
  active-plan progress; duck passes; fix cycles; findings; cumulative
  validation time; outcomes; and full commit hashes.
- The tracked branch-named HTML and PNG completion ledger paths under
  `Agentic/Ledgers/` and the terminal artifact commit hash.
- Rubber-duck verdicts keyed to their ledger step ids. If no review was
  appropriate, report the task's zero duck-pass count from the ledger.
