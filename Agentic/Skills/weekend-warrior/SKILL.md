---
name: weekend-warrior
description: Run a full SkullbonezCore Weekend Warrior cycle. Use when the user asks for Weekend Warrior, weekend-warrior, or an automated Carmack-test-to-implementation campaign that creates a dated weekend-warrior branch, runs the Carmack test, writes one plan per lacking area, implements each plan in order, rubber-duck reviews before every commit, acts on feedback, commits one plan at a time, pushes the branch, and opens a draft PR when the campaign is complete.
---

# Weekend Warrior

Turn a Carmack-test verdict into a full implementation campaign. This skill is
deliberately heavier than ordinary plan work: it creates the branch at run time,
generates plans from evidence-backed gaps, implements each plan, requires a
rubber-duck review before each commit, fixes blocking feedback, and commits one
plan at a time.

## Required Skill Order

Before task actions, read these repo-local skills when available:

1. `Agentic/Skills/carmack-test/SKILL.md` for the evaluation rubric.
2. `Agentic/Skills/orchestrator/SKILL.md` for plan execution, validation,
   commit, push, handoff, and rubber-duck accounting rules.
3. `Agentic/Skills/rubber-duck/SKILL.md` for review expectations.

If any required skill is missing, say which file is missing, continue with the
closest repo-local fallback, and record the limitation in the handoff.

## Startup

Follow the SkullbonezCore startup contract before editing:

1. Read `AGENTS.md`, `README.md`, `Agentic/README.md`, and
   `Agentic/SessionState.md`.
2. Run `git status --short --branch`; pre-existing dirty files are user-owned.
3. Start a wall-clock timer.
4. State the mixed impact area and name that validation will be selected
   per-plan at the pre-commit gate.

Do not create the Weekend Warrior branch while creating or updating this skill.
Create it only when the user runs Weekend Warrior.

## Branch Policy

At the start of a Weekend Warrior run, create or switch to:

```text
weekend-warrior-<yyyy-mm-dd>
```

Use the local date. If that branch already exists, reuse it. If the current
branch is already the correct Weekend Warrior branch, stay on it. Never rebase,
force-push, rewrite history, merge PRs, or commit/push on `main` without
explicit user confirmation.

## Carmack Test Pass

Run `$carmack-test` against the requested scope. If the user gives no scope,
evaluate the current checkout as a whole-engine pass.

Use the Carmack-test output as the source of truth for plan generation. Do not
invent gaps that are not supported by evidence. Keep facts, inferences, and
explicit evidence gaps separate.

## Plan Generation

Create one Markdown plan under `Agentic/Plans/` for each lacking area that must
be fixed to reach the Carmack-style standard. Use stable, hyphenated filenames:

```text
Agentic/Plans/weekend-warrior-<area>-plan.md
```

Each generated plan must include:

- title and scope,
- exact Carmack-test finding it addresses,
- evidence citations from source, logs, plans, or reports,
- smallest required validation command from `AGENTS.md`,
- clear definition of "up to Carmack standard" for this area,
- implementation checklist,
- validation checklist,
- evidence-capture checklist,
- rubber-duck review checklist,
- commit/handoff checklist,
- residual risks and explicit out-of-scope items.

Use unchecked boxes for future work. Mark a checkbox only when the work and its
evidence already exist.

## Implementation Loop

Process generated plans in priority order from the Carmack-test verdict. For
each plan, finish the whole plan before starting the next one:

1. Read the plan and relevant source.
2. Implement the plan in the main agent. Do not delegate implementation to a
   worker agent.
3. Update the plan checklist as implementation details are completed.
4. Inspect `git diff`, touched files, and relevant logs.
5. Run a read-only `$rubber-duck` review of the entire completed plan before
   committing. Use a separate reviewer/sub-agent if available; otherwise do a
   deliberate read-only critique pass in the current session.
6. Record rubber-duck accounting using the orchestrator skill format.
7. Fix every blocking rubber-duck finding in the main agent.
8. Repeat rubber-duck review after meaningful fixes in the reviewed risk area.
9. Run the smallest required pre-commit validation for the final changed-file
   set. Documentation-only plan edits require no repository validation.
10. Run `git status --short --branch`.
11. Stage only files for the completed plan, including plan/report/session
    updates that belong to that plan.
12. Commit one plan with useful notes: what changed, why, implementation
    details by area, validation command and key result, rubber-duck verdict,
    and residual risks.
13. Push normally.

Move to the next plan only after the current plan is implemented, reviewed,
validated as required, committed, and pushed.

After all generated plans are completed, pushed, and represented in the final
handoff, open a draft PR from the Weekend Warrior branch unless the user
explicitly asks not to. Prefer the GitHub app when available; use `gh` only as
a fallback.

## Rubber-Duck Gate

The commit gate is not satisfied until the entire plan has been rubber-ducked.
The review must prioritize:

- correctness and behavior regressions,
- missing tests or validation gaps,
- physics determinism and baseline risks,
- DX12 validation and screenshot risks,
- hot-path allocation or performance risks,
- stale or over-optimistic checklist items,
- source ownership and lifetime leaks.

If the reviewer reports no blockers, record that verdict. If the reviewer
reports blockers, fix them before validation and commit.

## Validation Discipline

Use `AGENTS.md` as the validation source of truth. Do not run broad repository
validation during ordinary iteration. Use targeted builds, launches, or focused
checks only when they answer a specific implementation question.

At the plan commit gate:

- run the smallest required `tools\validate_*` command for the touched files,
- mirror output to a log when practical,
- quote key success or failure lines in the handoff,
- never claim validation passed without command output.

## Final Handoff

Report:

- branch name and push target,
- Carmack-test scope and verdict summary,
- generated plan paths,
- completed plans in order,
- commit hash and subject for each plan,
- validation command, log path, and key result lines for each plan,
- rubber-duck accounting table,
- draft PR URL, or the blocker that prevented PR creation,
- dirty user-owned files left untouched,
- residual risks and remaining unchecked plan items,
- total elapsed wall-clock time and timings for substantial builds,
  validations, launches, or investigations.
