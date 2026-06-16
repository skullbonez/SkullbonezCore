# Orchestration Framework Fix Plan

Status: planning draft
Created: 2026-06-16
Scope: Agentic process, roadmap queue state, cross-agent instructions,
orchestrator guardrails, worker/verifier handoff
Implementation status: plan only, no code changes in this pass

## Goal

Make the roadmap orchestrator reliable enough to run repeated PR-bound work
without relying on memory, prompt luck, or manually interpreted Markdown.

The current framework has the right bones: a repository-wide agent contract,
an explicit orchestrator policy, an explicit queue, a manual runbook, worker
and verifier prompts, and report-only delivery rules. This plan fixes the
remaining sharp edges so Codex, Claude, and Copilot can all discover the same
rules, reach the same queue decisions, and leave auditable evidence.

## Current Findings

1. `Agentic/Orchestrator/README.md` says the orchestrator is disabled by
   default, while `Agentic/Orchestrator/policy.json` currently has
   `"enabled": true`.
2. `pr-open` is treated as terminal in completion/reporting rules, but the
   dependency selection rule does not treat `pr-open` as an acceptable
   dependency state unless the user explicitly approves it.
3. Some queue entries are already `pr-open` while their source plans still live
   under active `Agentic/Plans`, even though the runbook says successful
   terminal items should archive source plans into `Agentic/Plans/Done`.
4. The Copilot instruction file is only a pointer to `AGENTS.md`. That is fine
   for a coding agent that follows file references, but too thin for Copilot
   code review and path-scoped review behavior.
5. The queue and policy are JSON files, but there is no schema or checker that
   can catch invalid statuses, missing report artifacts, stale plan paths,
   multiple running items, or policy/runbook drift.
6. The runbook requires an independent verifier, but fallback behavior when
   sub-agent tooling is unavailable is not represented in policy or queue data.
7. The report-only commit rule is important but not mechanically enforced.

## Design Principles

1. Keep `AGENTS.md` as the source of truth for global repository rules.
2. Keep scoped agent instructions short and local; they should narrow context,
   not fork the global contract.
3. Treat policy and queue state as data, not prose.
4. Fail closed on merge, history rewrite, direct `main` push, missing verifier,
   missing validation evidence, and report-only commit pollution.
5. Keep validation scripts as PR/commit gates. Use new checks only for agent
   workflow metadata, state integrity, and unambiguous guardrails.
6. Make every terminal queue state auditable from committed reports and local
   run state.

## Phase 1: Normalize Current Orchestrator State

### Changes

1. Decide whether the orchestrator should be active by default.
   - If not actively running a loop, set `policy.json` `"enabled": false`.
   - If active by design, update the README so it says the orchestrator is
     currently enabled and explains how to revoke it.
2. Define terminal status categories in one place:
   - successful terminal: `done`, `pr-open`, `merged`,
   - non-successful terminal: `blocked`, `failed`, `skipped`,
   - active: `running`,
   - selectable: `ready`.
3. Define dependency satisfaction explicitly:
   - `merged` and `done` are always acceptable,
   - `skipped` is acceptable only when the skip is intentional,
   - `pr-open` is acceptable only for explicitly stacked child branches or
     explicit user approval.
4. Update queue entries so terminal `pr-open` items either:
   - have their source plan path updated to `Agentic/Plans/Done`, or
   - are downgraded to a non-terminal state that reflects reality.
5. Add a short "State Semantics" section to
   `Agentic/Orchestrator/runbook.md`.

### Validation

Documentation/process JSON only. No repository validation required.

## Phase 2: Add Queue And Policy Schema Checks

### Changes

Add:

```text
Agentic/Orchestrator/policy.schema.json
Agentic/Orchestrator/queue.schema.json
tools/check_orchestrator_state.py
tools/check_orchestrator_state.bat
```

Initial checker behavior:

1. Validate JSON parse and schema version.
2. Confirm `policy.json` and `queue.json` agree on status vocabulary.
3. Fail if more than one queue item is `running`.
4. Fail if an item references a missing plan path.
5. Warn or fail when a successful terminal item still points at an active plan.
6. Fail when a dependency is not in an allowed dependency-satisfied state.
7. Fail when a branch does not use the configured branch prefix.
8. Confirm terminal items with `report_required: true` have a report path or
   report URL recorded once the report metadata exists.
9. Check that `policy.allow_merge` cannot authorize merges unless `AGENTS.md`
   also permits the merge path.

Keep the first version deterministic and local. It should not call GitHub.

### Validation

At PR gate:

```bat
tools\validate_fast.bat
tools\check_orchestrator_state.bat --self-test
tools\check_orchestrator_state.bat
```

## Phase 3: Add Report-Only Commit Guardrails

### Changes

Add:

```text
tools/check_agent_report_commit.py
tools/check_agent_report_commit.bat
```

Checker behavior:

1. Accept a commit range or explicit file list.
2. Confirm the final report commit contains only:
   - `Agentic/Reports/<yyyy-mm-dd>/<item-id>/report.md`,
   - PNG or JPG files under that report folder's `images/` directory.
3. Confirm every committed image is referenced by relative Markdown from
   `report.md`.
4. Fail if `Agentic/Runs` files, validation logs, raw traces, PR notes, source
   code, queue updates, or unreferenced images are included in the report-only
   commit.
5. Print a concise success summary that can be pasted into the final report.

### Validation

At PR gate:

```bat
tools\validate_fast.bat
tools\check_agent_report_commit.bat --self-test
```

## Phase 4: Strengthen Cross-Agent Instruction Discovery

### Changes

Keep `AGENTS.md` as the global contract, then add short scoped instruction
surfaces:

```text
Agentic/AGENTS.md
tools/AGENTS.md
SkullbonezData/shaders/AGENTS.md
TestOutput/baselines/AGENTS.md
.github/instructions/orchestrator.instructions.md
.github/instructions/tools.instructions.md
.github/instructions/shaders.instructions.md
.github/instructions/baselines.instructions.md
```

Update `.github/copilot-instructions.md` so it still points to `AGENTS.md`,
but also includes a compact reviewer-facing subset:

- no force-push, rebase, history rewrite, or direct `main` push,
- validation scripts are PR/commit gates,
- documentation-only changes require no validation,
- report-only commits must contain only report Markdown and referenced images,
- physics diagnostics must use SkullScope summaries instead of raw log dumps.

For Claude, keep `CLAUDE.md` as a small import/pointer file. Add Claude rules
or subagent definitions only after Phase 5 so they reflect the final contract.

### Validation

Documentation-only unless helper scripts are touched. No repository validation
required for instruction-file-only changes.

## Phase 5: Make Worker And Verifier Roles Tool-Native

### Changes

Keep the existing Markdown prompt templates, then add tool-native role files
where supported:

```text
.codex/agents/roadmap-worker.toml
.codex/agents/roadmap-verifier.toml
.claude/agents/roadmap-worker.md
.claude/agents/roadmap-verifier.md
```

Worker role:

- may edit only the assigned implementation scope,
- may run targeted builds/tests during iteration only when they answer a
  specific implementation question,
- must not archive source plans, update queue terminal state, create reports,
  merge, rebase, force-push, or push to `main`.

Verifier role:

- read-only by default,
- checks source plan, diff, validation evidence, artifacts, and worker handoff,
- returns `accepted`, `needs-fixes`, or `blocked`,
- distinguishes blocking findings from non-blocking suggestions,
- treats missing required validation evidence as blocking unless explicitly
  delegated to the orchestrator.

Add policy fields:

```json
{
  "verification": {
    "requires_independent_verifier": true,
    "allow_same_agent_worker_fallback": true,
    "allow_same_agent_verifier_fallback": false
  }
}
```

### Validation

Documentation/config-only role files require no repository validation. If a
script begins consuming the role metadata, use `tools\validate_fast.bat` and
the script's self-test.

## Phase 6: Add Run-State Schema And Resume Safety

### Changes

Define a stable run-state shape:

```text
Agentic/Orchestrator/run.schema.json
```

`run.json` should record:

- schema version,
- item id,
- source plan path and archived plan path,
- branch and parent branch,
- start/end timestamps and elapsed time,
- baseline dirty worktree status before the run,
- policy snapshot hash,
- queue item snapshot,
- selected worker and verifier role names,
- validation owner,
- validation commands and result,
- report path, report commit, and report web URL,
- PR link and merge status,
- blocker/failure reason when terminal state is not successful.

Add resume rules to the runbook:

1. If `run.json` exists for an item, resume from it instead of reconstructing
   state from memory.
2. If the policy snapshot changed mid-run, stop and require user confirmation
   before continuing risky actions.
3. If the worktree differs from the run's recorded baseline before the worker
   starts, record the difference and avoid reverting unrelated changes.

### Validation

Schema and documentation only: no repository validation required. If a helper
validates run state, use `tools\validate_fast.bat` plus its self-test.

## Phase 7: Add Optional Hard-Block Hooks

### Changes

Only add hooks for unambiguous danger zones:

- block force-push,
- block rebase,
- block direct push to `main`,
- block broad process kills such as `taskkill /IM`,
- warn before committing `Agentic/Runs`,
- warn before a report-only commit contains non-report files.

Hooks should print the exact rule and the file that owns it. They should not run
renderer, physics, perf, or full repository validation.

### Validation

At PR gate:

```bat
tools\validate_fast.bat
```

Also run any changed hook self-test if one exists.

## Phase 8: Workflow Evals

### Changes

Add deterministic agent-workflow eval fixtures:

```text
Agentic/Evals/
  README.md
  cases/
    docs-only-change.json
    shader-change.json
    physics-baseline-change.json
    tools-change.json
    orchestrator-report-only-commit.json
  expected/
  results/
tools/agent_workflow_eval.py
tools/agent_workflow_eval.bat
```

Initial evals should check:

- selected impact area,
- selected PR gate,
- whether validation can be skipped for docs-only work,
- whether SkullScope accounting is required,
- whether report-only commit file lists pass,
- whether direct `main` push, rebase, or force-push actions are forbidden.

### Validation

At PR gate:

```bat
tools\validate_fast.bat
tools\agent_workflow_eval.bat --self-test
```

## Recommended Order

1. Phase 1: state semantics and current queue cleanup.
2. Phase 2: schema and state checker.
3. Phase 3: report-only commit checker.
4. Phase 4: scoped cross-agent instructions.
5. Phase 5: tool-native worker/verifier roles.
6. Phase 6: run-state schema and resume safety.
7. Phase 7: hard-block hooks.
8. Phase 8: workflow evals.

Do not start merge automation until Phases 1-6 are working in PR-only mode.

## Acceptance Criteria

- `policy.json` and orchestrator documentation agree about whether automation
  is enabled.
- Queue status semantics are explicit and consistently applied.
- A checker catches stale queue state before the orchestrator starts work.
- `pr-open` dependencies are handled deliberately, especially for stacked
  branches.
- Terminal successful items have reports and archived source plans, or the
  queue explains why they are not truly terminal.
- Copilot, Claude, and Codex all discover the same high-level repository rules.
- Worker and verifier roles are explicit, repeatable, and tool-native where
  supported.
- A missing independent verifier cannot accidentally become a claimed
  independent verification.
- Report-only commits are mechanically checkable.
- Hooks block only clear danger-zone actions and do not replace PR-gate
  validation.
- Agent workflow evals cover the failure modes that have historically required
  human correction.

## Validation Plan For This Markdown Change

This file is documentation-only under `Agentic/Plans`. No repository validation
is required for creating or editing this plan.
