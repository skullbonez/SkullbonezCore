---
name: orchestrator
description: Coordinate SkullbonezCore plan implementation with fresh Codex agents. Use when the user asks for the orchestrator, night runner, nightrunner, overnight runner, queued plan runner, or asks Codex to complete one or more Agentic/Plans items by dispatching implementation workers, running rubber-duck review agents, then validating, committing, and pushing each accepted plan.
---

# Orchestrator

Coordinate a SkullbonezCore plan queue without the retired repository-owned
JSON/Python state machine. This skill is coordinator-only: it resolves plan
scope and branch policy, delegates implementation to fresh agents, reviews the
result, asks a separate `$rubber-duck` review agent to critique the work, runs
the required final validation gate, and commits/pushes one accepted plan at a
time.

## Inputs

Accept any number of plan names in the user's requested order. Normalize bare
names by adding `.md`, and resolve them under `Agentic/Plans/` unless the user
gives a path. If a plan cannot be found, search by stem with
`rg --files Agentic/Plans` and ask only if multiple plausible matches remain.

Determine the branch before edits:

- If the user gives a branch name, use it exactly.
- If the current branch is already `nightrunner-*`, reuse it unless the user
  asked for a different branch.
- Otherwise create `nightrunner-<local-date-slug>`, such as
  `nightrunner-20th-june`.

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

## Agent Tools

Use subagents or Codex thread tools for implementation and review. If the tools
are not already loaded, search for them with `tool_search` using names such as
`create_thread`, `send_message_to_thread`, `read_thread`, `handoff_thread`, and
`list_threads`.

If a tool creates a separate worktree, ensure the worker is on the selected
branch or a clearly named child branch before it edits. Keep one active
implementation plan at a time unless the user explicitly asks for parallel work
and isolated worktrees are available.

## Plan Loop

For each plan, in order:

1. Read the plan enough to understand scope, required validation, and
   archival/report expectations.
2. Launch a fresh worker agent to complete exactly that plan. The worker may
   edit files but should not commit, push, open PRs, merge PRs, or overwrite
   unrelated dirty files.
3. Give the worker a compact prompt:

```text
Use the SkullbonezCore startup contract. On branch <branch>, complete <plan-path> only.
Use Agentic/Skills/orchestrator/SKILL.md as the coordination contract.
Do not commit or push. Treat pre-existing dirty files as user-owned.
Return changed files, validation required by AGENTS.md, commands run, blockers, and any follow-up needed.
```

4. Inspect the worker result with `git status --short` and targeted file reads
   or diffs. Do not assume the worker's summary is complete.
5. Launch a separate read-only rubber-duck review agent:

```text
Use $rubber-duck to review the completed work for <plan-path> on branch <branch>.
Stay read-only. Prioritize bugs, behavior regressions, missing tests, validation gaps,
baseline mistakes, determinism risks, DX12 validation risks, and hot-path allocation concerns.
Return findings with file/line references and a clear verdict.
```

6. Address blocking rubber-duck findings before committing. Prefer sending
   focused fixes back to a worker agent; make small fixes directly only when
   doing so is simpler and safe.
7. Repeat the rubber-duck pass if the fix changed meaningful behavior or
   touched the reviewed risk area.
8. Run the smallest required pre-commit validation from `AGENTS.md` for that
   plan's final changed-file set. Documentation-only changes require no
   validation.
9. Run `git status --short --branch` before staging.
10. Stage only files belonging to the completed plan and its required
    reports/session-state updates.
11. Commit with useful notes: what changed, why, implementation details by
    area, exact validation command and result, and baseline/report/session-state
    updates.
12. Push normally. Never force-push.

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
- Worker and rubber-duck agent verdicts.
- Validation commands and key result lines, or "documentation-only, no
  validation required."
- Any skipped plan, blocker, dirty user-owned file, or residual risk.
- Total elapsed wall-clock time and timings for long builds, validations,
  launches, or investigations.
