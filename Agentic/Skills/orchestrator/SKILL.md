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

Default to zero rubber-duck rows while ordinary implementation is in progress.
Keep an in-memory row for every rubber-duck review pass that actually runs.
Assign each pass a
stable run id such as `<plan-stem>-duck-01`, `<plan-stem>-duck-02`, and so on.
For each row, record:

- plan path,
- run id and reviewer/thread identifier,
- pass reason, such as initial review or follow-up after fixes,
- prompt/context characters sent to the sub-agent, including any pasted diff or
  artifact text,
- review response characters returned by the sub-agent,
- token counts if the sub-agent tool exposes them, otherwise `n/a`,
- elapsed wall-clock time for the review pass when measurable,
- verdict and whether follow-up work was required.

Do not invent token counts. If token usage is unavailable, report character
counts and mark token columns `n/a`. Keep this accounting separate from
SkullScope diagnostics accounting; it measures review-agent prompt/response
usage, not repository artifacts or validation logs.

## Plan Loop

For each plan or source slice, in order:

1. Read the plan enough to understand scope and required validation. Read the
   authoritative progress ledger in `Agentic/Plans/MASTER-PLAN.md`, resolve the
   owning plan's post-slice completed-task count and total, and include this
   fully resolved line in the active implementation prompt/task framing before
   edits. Both counts are plan-local; do not compute a cross-plan percentage.

```text
Required commit subject first line: <PLAN_NAME>, TASK <DONE>/<TASK_COUNT> — <ACTION SUMMARY>
```

   Recalculate it if scope or task completion changes before commit.
2. Complete exactly that plan in the main agent. Do not launch an implementation
   worker or ask a sub-agent to edit files.
3. Inspect the result with `git status --short` and targeted file reads or
   diffs.
4. For ordinary incremental slices, skip rubber-duck review and keep moving.
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

5. Address blocking rubber-duck findings in the main agent before committing
   when a review was actually run.
6. Repeat the rubber-duck pass only if the fix changed meaningful behavior in
   the reviewed risk area or the reviewer requested a follow-up. Record every
   repeat as its own accounting row.
7. Run the smallest cumulative pre-commit validation from `AGENTS.md` for that
   task's changed-file set. Documentation-only changes require no validation.
   Do not run full validation for an intermediate task or ordinary PR commit.
   Run `tools\agent_validate.bat --plan-completion` once, after independent
   review and immediately before the terminal commit that closes the entire
   implementation plan.
8. Update `Agentic/SessionState.md` whenever a task or phase completes, a plan
    closes, a blocker is recorded, or the portfolio denominator moves. This is a
    write step, not a read step: steps 9 and 10 stage and report the update, but
    neither creates it. At minimum the Current State table's objective and
    active/future progress figure, and the Live Queue's binding next task, must
    match the post-commit MASTER-PLAN ledger exactly. Recompute the progress
    figure from the ledger; never carry the previous value forward. A closed plan
    that left the live inventory under rule 4 changes both the numerator and the
    denominator, so the figure usually moves more than one task's worth.
9. Run `git status --short --branch` before staging.
10. Stage only files belonging to the completed plan and its required
    reports/session-state updates.
11. Commit with the required MASTER progress header as the subject's first
    fields, followed by a concise action summary. Use the post-commit ledger
    values and update MASTER in the same commit whenever task completion or the
    portfolio denominator changes. The body records what changed, why,
    implementation details by area, exact validation command and result, and
    baseline/report/session-state updates.
12. Push normally. Never force-push.

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
- A final rubber-duck accounting table, one row per review pass. If no review
  was appropriate, say that no rubber-duck pass was run for the slice:

```markdown
| Plan | Duck run | Reviewer/thread | Reason | Prompt chars | Response chars | Tokens | Elapsed | Verdict | Follow-up |
|------|----------|-----------------|--------|--------------|----------------|--------|---------|---------|-----------|
| Agentic/Plans/example.md | example-duck-01 | thread id/name | Initial review | 1234 | 5678 | n/a | 2m 10s | No blockers | None |
```
