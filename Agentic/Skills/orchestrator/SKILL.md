---
name: orchestrator
description: Coordinate SkullbonezCore plan implementation in the main Codex agent, using sub-agents only for rubber-duck review. Use when the user asks for the orchestrator, night runner, nightrunner, overnight runner, queued plan runner, or asks Codex to complete one or more Agentic/Plans items, then validate, commit, and push each accepted plan.
---

# Orchestrator

Coordinate a SkullbonezCore plan queue without the retired repository-owned
JSON/Python state machine. This skill is coordinator-only: it resolves plan
scope and branch policy, implements each plan in the main agent, asks a
separate `$rubber-duck` review sub-agent to critique the work, runs the required
final validation gate, and commits/pushes one accepted plan at a time.

## Inputs

Accept any number of plan names in the user's requested order. Normalize bare
names by adding `.md`, and resolve them under `Agentic/Plans/` unless the user
gives a path. If a plan cannot be found, search by stem with
`rg --files Agentic/Plans` and ask only if multiple plausible matches remain.

Determine the branch before edits:

- If the user gives a branch name, use that exact branch name. Do not add,
  remove, normalize, prefix, suffix, or replace any part of the requested name.
- If the current branch is already `nightrunner-*`, reuse it unless the user
  asked for a different branch.
- Otherwise, first look for an existing reusable nightrunner branch before
  creating anything new. Prefer the most recently updated local branch from
  `git branch --list "nightrunner-*" --sort=-committerdate`; if none exists,
  look for the most recently updated remote-tracking branch from
  `git branch -r --list "origin/nightrunner-*" --sort=-committerdate` and
  create a local tracking branch for it.
- Create `nightrunner-<local-date-slug>`, such as `nightrunner-20th-june`,
  only when no reusable local or remote-tracking nightrunner branch exists.

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

## Sub-Agent Tools

Use sub-agents or Codex thread tools only for independent `$rubber-duck` review.
Do not dispatch plan implementation, cleanup, validation, staging, committing,
or pushing to a sub-agent. If the tools are not already loaded, search for them
with `tool_search` using names such as `create_thread`, `send_message_to_thread`,
`read_thread`, `handoff_thread`, and `list_threads`.

If a review tool creates a separate worktree, keep it read-only. Keep one active
implementation plan at a time unless the user explicitly asks for a different
queue policy.

## Rubber-Duck Accounting

Keep an in-memory row for every rubber-duck review pass. Assign each pass a
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

For each plan, in order:

1. Read the plan enough to understand scope, required validation, and
   archival/report expectations.
2. Complete exactly that plan in the main agent. Do not launch an implementation
   worker or ask a sub-agent to edit files.
3. Before treating a plan as complete, inspect every file named or clearly
   covered by the plan, plus the plan checklist itself. Confirm the work is
   actually done in those files; do not rely only on unchecked-item counts,
   stale notes, or a sample of files.
4. Inspect the result with `git status --short` and targeted file reads or
   diffs.
5. Launch a separate read-only rubber-duck review sub-agent:

```text
Use $rubber-duck to review the completed work for <plan-path> on branch <branch>.
Stay read-only. Prioritize bugs, behavior regressions, missing tests, validation gaps,
baseline mistakes, determinism risks, DX12 validation risks, and hot-path allocation concerns.
Return findings with file/line references and a clear verdict.
```

6. Address blocking rubber-duck findings in the main agent before committing.
7. Repeat the rubber-duck pass if the fix changed meaningful behavior or
   touched the reviewed risk area. Record every repeat as its own accounting
   row.
8. Move the plan file to `Agentic/Plans/Done` only after the file-by-file
   completion check and rubber-duck review both confirm there are no blockers.
   This applies even when the checklist has zero unchecked items. If the plan is
   still blocked, incomplete, or too large to close safely, leave it in
   `Agentic/Plans/IN PROGRESS` and add explicit TODO notes there instead.
9. Run the smallest required pre-commit validation from `AGENTS.md` for that
   plan's final changed-file set. Documentation-only changes require no
   validation.
10. Run `git status --short --branch` before staging.
11. Stage only files belonging to the completed plan and its required
    reports/session-state updates.
12. Commit with useful notes: what changed, why, implementation details by
    area, exact validation command and result, and baseline/report/session-state
    updates.
13. Push normally. Never force-push.

Only advance to the next plan after the current plan is reviewed, validated as
required, committed, and pushed.

## Validation Discipline

Do not run `tools\validate_*` scripts during normal iteration. They are
pre-commit/PR gates. During implementation, use focused builds, launches, tests,
or inspections only when they answer a specific question.

When validation is required, run it in a visible console when available and
mirror output to a log when practical. Never claim validation success without
command output. Quote the key result lines and log path in the handoff.

Use `AGENTS.md` as the source of truth for validation selection. If scope is
broad or unclear, use `tools\agent_validate.bat`.

## Final Handoff

Report:

- Branch name and push target.
- Plans completed, in order.
- Commit hashes and subjects for each plan.
- Main-agent implementation summary and rubber-duck sub-agent verdicts.
- Validation commands and key result lines, or "documentation-only, no
  validation required."
- Any skipped plan, blocker, dirty user-owned file, or residual risk.
- Total elapsed wall-clock time and timings for long builds, validations,
  launches, or investigations.
- A final rubber-duck accounting table, one row per review pass:

```markdown
| Plan | Duck run | Reviewer/thread | Reason | Prompt chars | Response chars | Tokens | Elapsed | Verdict | Follow-up |
|------|----------|-----------------|--------|--------------|----------------|--------|---------|---------|-----------|
| Agentic/Plans/example.md | example-duck-01 | thread id/name | Initial review | 1234 | 5678 | n/a | 2m 10s | No blockers | None |
```
