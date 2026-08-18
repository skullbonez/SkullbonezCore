---
name: orchestrator
description: Run SkullbonezCore's persistent MASTER-PLAN implementation queue in the main Codex agent, using a deterministic Night Runner branch and sub-agents only for rubber-duck review. Use when the user asks for the orchestrator, night runner, nightrunner, overnight runner, queued plan runner, MASTER-PLAN runner, or asks Codex to complete one or more Agentic/Plans items, then continue through blockers, validate, commit, and push each accepted plan.
---

# Orchestrator

Coordinate the persistent SkullbonezCore MASTER-PLAN queue without the retired
repository-owned JSON/Python state machine. Resolve the Night Runner branch and
goal before edits, implement plans in the main agent, continue past documented
blockers, save any independent `$rubber-duck` critique for a major completed
plan or whole-job checkpoint, run required final gates, and commit/push one
accepted slice or blocker record at a time.

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
4. State the impact area and the validation that will be deferred to the
   commit/PR gate.
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
Complete Agentic/Plans/MASTER-PLAN.md on <branch> in binding priority order; validate, commit, and push accepted slices; record genuine blockers and continue with the next dependency-safe item without stopping.
```

4. If an unrelated unfinished goal prevents goal creation, record that tooling
   conflict as a blocker and continue the MASTER-PLAN queue. Create the intended
   goal as soon as the goal slot becomes available; do not abandon repository
   work because goal metadata is temporarily unavailable.
5. Keep the goal active while any actionable MASTER-PLAN work remains. One
   blocked item never blocks or completes the whole goal.

Use `Agentic/Plans/MASTER-PLAN.md` as the live queue. Read its Current Execution
Priority, Portfolio Progress Ledger, plan-state tables, and linked `TODO/`
plans. Select the next unfinished, dependency-safe item in binding order. Ignore
`Agentic/Plans/WNF/` unless the owner explicitly reactivates an item. Re-read
MASTER-PLAN after every pushed slice because the queue and denominator may have
changed.

## Live Work Ledger

Use the batch-owned live ledger for the complete orchestrator goal. It writes
`Agentic/Plans/WORK_LEDGER.md` beside `MASTER-PLAN.md`; the path is ignored by
Git because the final post-push hash update cannot be part of the commit whose
hash it records. Never stage or hand-edit this runtime artifact.

Every batch call reads the exact cumulative token counter for
`CODEX_THREAD_ID`, captures a local ISO-8601 timestamp with timezone, and
atomically rewrites the Markdown plus its embedded recovery state. A failed
call is an orchestration blocker: do not replace exact telemetry with an
estimate or an in-memory row. The unfinished row identifies the live step, so
the owner can inspect the ledger at any time while work is running.

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

For a newly created rubber-duck agent/thread, transition before launching it,
then attach its returned thread/session id with a zero baseline. Zero is the
exact start of that new reviewer session, including its prompt and context:

```bat
Agentic\Skills\orchestrator\scripts\work_ledger.bat attach-worker -Task "<PLAN>-T<n>" -WorkerThreadId "<reviewer id>" -WorkerBaselineZero
```

Use the reviewer's actual `CODEX_THREAD_ID`, not an orchestration handle that
cannot be resolved under `.codex\sessions`. If the launch API returns only a
handle, have the reviewer read and return `$env:CODEX_THREAD_ID`; attaching it
afterward with a zero baseline still accounts for that complete new session.

When reusing an existing reviewer session, attach or open the step with
`-WorkerThreadId` and omit `-WorkerBaselineZero`; the helper snapshots its
current cumulative counter. Close every rubber-duck step with `-Findings <n>`.
Count every enumerated item under the review's Findings and Missing evidence
sections. Put the verdict in `-Outcome`. Use `finding-fix`, `rubber-duck`, and
`validation` kinds for repeats so the ledger derives duck-pass count, fix-cycle
count, total findings, reviewer tokens, and cumulative validation duration.

At minimum, record implementation/investigation, every rubber-duck pass, every
finding-fix cycle, final validation, and commit/push as separate steps. Use
`other` for another material phase. After validation, transition to
`commit-push`; after the commit has been pushed, close the task with its commit:

```bat
Agentic\Skills\orchestrator\scripts\work_ledger.bat finish-task -Task "<PLAN>-T<n>" -Outcome "pushed" -Commit HEAD
```

`finish-task` resolves the full commit hash and refuses to close until that
commit is an ancestor of the configured upstream. After the final task, close
the goal immediately before the final handoff:

```bat
Agentic\Skills\orchestrator\scripts\work_ledger.bat finish-goal -Outcome "complete"
```

The ledger groups step rows beneath each task and maintains task/run summaries
with elapsed time; explicit input, output, and cached-input counters; main and
reviewer splits; duck passes; fix cycles; findings; validation time; outcomes;
and full commit hashes. Never report an unqualified `tokens` or `total tokens`
column. Codex input includes the cached-input subset, so show cached input
separately but do not add it to input again. The embedded state survives context
compaction and process restart.

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

Use sub-agents or Codex thread tools only for independent `$rubber-duck` review
at the end of a major plan/checkpoint or whole job. Earlier review is allowed
only when the user explicitly asks for one, or when the same failure mode has
repeated and independent critique is the cheapest way to get unstuck. Do not run
a review per edit, per checklist row, per source file, per commit, or per small
slice. Do not dispatch plan implementation, cleanup, validation, staging,
committing, or pushing to a sub-agent. If the tools are not already loaded,
search for them with `tool_search` using names such as `create_thread`,
`send_message_to_thread`, `read_thread`, `handoff_thread`, and `list_threads`.

If a review tool creates a separate worktree, keep it read-only. Keep one active
implementation plan at a time unless the user explicitly asks for a different
queue policy.

### Ownership Evidence For The End-Of-Plan Review

Before dispatching the end-of-plan `$rubber-duck` pass on a plan that changed
C++ source, run the six read-only inventories and include their output in the
review prompt:

```bash
python tools/check_build_config_consistency.py --repo .
python tools/inventory_unreachable_symbols.py --repo . --strict
python tools/inventory_authority_free_aggregates.py --repo .
python tools/inventory_extraction_scars.py --repo .
python tools/inventory_wide_signatures.py --repo .
python tools/inventory_function_complexity.py --repo . --strict
```

`AGENTS.md` delegates its build-configuration, reachability, aggregate,
capability-slice, extraction-scar, wide-signature, and function-complexity rules
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

## Plan Loop

For each plan or source slice, in order:

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
3. Complete exactly that plan in the main agent. Do not launch an implementation
   worker or ask a sub-agent to edit files.
4. Inspect the result with `git status --short` and targeted file reads or
   diffs.
5. For ordinary incremental slices, skip rubber-duck review and transition
   directly from implementation to validation. When review is required,
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
   If there are no fixes, transition directly to validation.
7. Repeat the rubber-duck pass only if the fix changed meaningful behavior in
   the reviewed risk area or the reviewer requested a follow-up. Transition at
   both sides of every repeat so each pass and fix cycle is a separate live
   ledger row.
8. Open the `validation` ledger step before running the smallest cumulative
   pre-commit validation from `AGENTS.md` for that
   task's changed-file set. Documentation-only changes require no validation.
   Still record a bounded documentation/diff inspection in the validation row
   so its duration and outcome are explicit. Do not run full validation for an
   intermediate task or ordinary PR commit.
   Run `tools\agent_validate.bat --plan-completion` once, after independent
   review and immediately before the terminal commit that closes the entire
   implementation plan.
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
12. After validation succeeds, transition to the `commit-push` ledger step.
    Commit with the required MASTER progress header as the subject's first
    fields, followed by a concise action summary. Use the post-commit ledger
    values and update MASTER in the same commit whenever task completion or the
    portfolio denominator changes. The body records what changed, why,
    implementation details by area, exact validation command and result, and
    baseline/report/session-state updates.
13. Push normally. Never force-push.
14. Call `work_ledger.bat finish-task -Commit HEAD` only after the push
    succeeds. This closes commit/push timing, verifies upstream containment,
    writes the full hash into the task group, and makes the completed ledger
    immediately queryable before advancing the queue.

Advance after the current item is either reviewed, validated, committed, and
pushed, or its blocker record is committed and pushed under Blocker
Continuation. A single plan failure is not a terminal condition.

## Validation Discipline

Do not run `tools\validate_*` scripts during normal iteration. They are
pre-commit/PR gates. During implementation, use focused builds, launches, tests,
or inspections only when they answer a specific question.

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
- The live `Agentic/Plans/WORK_LEDGER.md` path and its exact goal/task summary:
  elapsed time; explicit input, output, and cached-input counters with main and
  reviewer splits; duck passes; fix cycles; findings; cumulative validation
  time; outcomes; and full commit hashes.
- Rubber-duck verdicts keyed to their ledger step ids. If no review was
  appropriate, report the task's zero duck-pass count from the ledger.
