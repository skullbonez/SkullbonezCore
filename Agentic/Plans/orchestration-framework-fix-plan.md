# YAML Orchestration Framework Fix Plan

Status: executable JSON loop implemented; later automation phases pending
Created: 2026-06-16
Scope: Agentic process, YAML workflow language, roadmap queue state,
cross-agent instructions, orchestrator guardrails, worker/verifier handoff
Implementation status: YAML/config foundation is present as a human-readable
mirror, and `tools/orchestrator.py` now provides the executable JSON loop over
`policy.json`, `queue.json`, and `machines/roadmap-item.json`. The helper checks
queue state, selects the next item, creates run state, creates/switches stacked
item branches, renders worker/verifier prompts, can invoke `codex exec`, drives
worker/verifier rounds through `run-loop`, records legal transitions, archives
successful plans, finalizes report commits, and checks committed report
content. Later richer role, hook, generated-doc, and workflow-eval automation
remains future work.

## Implementation Update: 2026-06-16

The orchestrator first gained a YAML-readable control layer. It now has an
executable JSON control plane because the local Python runtime has built-in JSON
support and no built-in YAML parser. `tools/orchestrator.bat` launches
`tools/orchestrator.py` for mechanical checks and transitions.

The official `openai-codex` Python package was installed on the machine so the
helper can find the bundled `codex.exe` runtime when the WindowsApps alias is
not executable from shell. `FIRST_TIME_SETUP.md` now lists this as the Codex
orchestration dependency and points fresh installs at
`tools\orchestrator.bat doctor`. Tooling changes require
`tools\validate_fast.bat` plus
`tools\orchestrator.bat check --self-test`.

The mandatory loop is:

1. Orchestrator selects a ready plan-backed item and updates the queue to
   `running`.
2. Orchestrator creates or switches to the item branch, then spawns a Codex
   worker with `run-loop` / `run-worker` or emits the worker prompt for manual
   dispatch.
3. Worker completion moves to review, then a rubber-duck verifier is spawned.
4. Verifier `needs_fixes` returns to the worker; this repeats until
   `accepted`, `blocked`, or `failed`.
5. Successful completion moves the queue to terminal success through
   `finalize`, archives the plan, drafts/checks the report, and can leave the
   report-only commit as the final branch commit.
6. The next ready item starts as a child branch of the previous queue item
   through explicit queue dependencies and branch policy.

## Goal

Replace the current prose-first orchestrator with a YAML-defined workflow
language for states, transitions, guards, actions, artifacts, and terminal
outcomes.

Markdown should become the explanation layer, not the source of truth. The
orchestrator should read YAML, validate YAML, execute or simulate YAML state
transitions, and generate Markdown runbooks/reports from that data where useful.

The current framework has good raw material: `AGENTS.md`, an explicit policy,
an explicit queue, worker/verifier prompts, and report-only delivery rules. The
problem is that too much behavior lives in Markdown prose. This plan moves the
behavior into a compact YAML state-machine DSL.

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
8. Most importantly: the true orchestration model is implicit. State semantics,
   transition rules, dependency gates, validation ownership, and report
   requirements should be data, not paragraphs.

## Design Principles

1. Keep `AGENTS.md` as the source of truth for global repository rules.
2. Use YAML as the source of truth for orchestration behavior.
3. Generate or summarize Markdown from YAML; do not require agents to infer
   behavior from Markdown.
4. Treat policy, queue, machines, guards, and actions as typed data.
5. Fail closed on merge, history rewrite, direct `main` push, missing verifier,
   missing validation evidence, invalid transition, and report-only commit
   pollution.
6. Keep validation scripts as PR/commit gates. Use new checks only for
   orchestration metadata, state integrity, generated-doc drift, and
   unambiguous guardrails.
7. Prefer a tiny repo-local YAML reader or committed Python dependency over
   hand-parsing YAML. If dependency setup becomes annoying, use YAML files that
   stay inside a conservative subset: maps, lists, strings, booleans, numbers,
   and no anchors.

## Target File Layout

```text
Agentic/Orchestrator/
  policy.yaml
  queue.yaml
  machines/
    roadmap-item.yaml
    queue.yaml
    report.yaml
  schemas/
    policy.schema.yaml
    queue.schema.yaml
    machine.schema.yaml
    run.schema.yaml
  templates/
    worker-prompt.md
    verifier-prompt.md
    report.md
  README.md
  runbook.md

tools/
  check_orchestrator_state.py
  check_orchestrator_state.bat
  render_orchestrator_docs.py
  render_orchestrator_docs.bat
  advance_orchestrator_state.py
  advance_orchestrator_state.bat
```

The existing JSON files can remain during migration, but the target state is
YAML-first.

## YAML State Machine Shape

Use a small state-machine DSL rather than a giant general workflow engine.

Example:

```yaml
schema_version: 1
machine: roadmap_item
initial: ready

states:
  ready:
    kind: selectable
    on:
      start:
        target: running
        guards:
          - policy.enabled
          - queue.no_other_running_item
          - dependencies.satisfied
        actions:
          - run.create_directory
          - run.snapshot_policy
          - queue.mark_running

  running:
    kind: active
    on:
      worker_done:
        target: reviewing
        actions:
          - run.save_worker_result
          - git.inspect_status
      worker_blocked:
        target: blocked
        actions:
          - run.record_blocker
          - report.required
      worker_failed:
        target: failed
        actions:
          - run.record_failure
          - report.required

  reviewing:
    kind: active
    on:
      review_ready:
        target: verifying
        guards:
          - changes.in_scope
          - validation.ready_or_deferred
        actions:
          - verification.write_prompt

  verifying:
    kind: active
    on:
      accepted:
        target: validating
      needs_fixes:
        target: running
        actions:
          - verification.save_findings
          - worker.send_findings
      blocked:
        target: blocked
        actions:
          - verification.save_blocker
          - report.required

  validating:
    kind: active
    on:
      passed:
        target: reporting
        actions:
          - validation.save_log
      not_required:
        target: reporting
      failed:
        target: failed
        actions:
          - validation.save_log
          - report.required

  reporting:
    kind: active
    on:
      report_committed:
        target: pr_open
        guards:
          - policy.allow_pr_creation
        actions:
          - git.push_branch
          - pr.open_or_update
      report_committed_no_pr:
        target: done
        guards:
          - policy.pr_not_required

  pr_open:
    kind: terminal_success
    dependency_satisfied: stacked_only

  done:
    kind: terminal_success
    dependency_satisfied: true

  merged:
    kind: terminal_success
    dependency_satisfied: true

  blocked:
    kind: terminal_failure
    stop_queue: true

  failed:
    kind: terminal_failure
    stop_queue: true

  skipped:
    kind: terminal_skip
    dependency_satisfied: explicit_only
```

The checker should reject transitions that are not declared in YAML. No more
"the runbook kind of implies this is okay" energy.

## YAML Policy Shape

Example:

```yaml
schema_version: 1
enabled: false
base_branch: main
branch_prefix: codex/
max_active_items: 1

pull_requests:
  allow_creation: true
  require_user_request: false

merge:
  allow: false
  requires_agents_md_permission: true
  requires_green_checks: true
  requires_report_commit: true
  method: squash

verification:
  requires_independent_verifier: true
  allow_same_agent_worker_fallback: true
  allow_same_agent_verifier_fallback: false

artifacts:
  run_root: Agentic/Runs
  report_root: Agentic/Reports
  commit_reports: true
  commit_report_images: true
  commit_run_state: false

queue:
  stop_after_blocked: true
  stop_after_failed: true
  advance_after_failed: false
```

## YAML Queue Shape

Example:

```yaml
schema_version: 1
policy: Agentic/Orchestrator/policy.yaml
machine: Agentic/Orchestrator/machines/roadmap-item.yaml

items:
  - id: water-rendering-cleanup
    plan: Agentic/Plans/water-rendering-cleanup-plan.md
    state: ready
    priority: 30
    branch: codex/water-rendering-cleanup
    impact_area:
      - DX12
      - rendering
      - water
    validation_gate: tools\validate_dx12_renderer.bat
    depends_on: []
    report_required: true
    artifact_commands: []
    screenshot_scenes: []

  - id: child-runtime-cleanup
    plan: Agentic/Plans/runtime-cleanup-plan.md
    state: ready
    priority: 31
    branch: codex/child-runtime-cleanup
    parent_branch: codex/water-rendering-cleanup
    dependency_mode: stacked_pr
    depends_on:
      - water-rendering-cleanup
    report_required: true
```

Use `state`, not `status`, for queue entries so it lines up with the state
machine vocabulary.

## Phase 1: Define The YAML DSL

### Changes

1. Add `Agentic/Orchestrator/machines/roadmap-item.yaml`.
2. Add `Agentic/Orchestrator/policy.yaml`.
3. Add `Agentic/Orchestrator/queue.yaml`.
4. Add YAML schemas under `Agentic/Orchestrator/schemas/`.
5. Define the core vocabulary:
   - states,
   - state kinds,
   - events,
   - transitions,
   - guards,
   - actions,
   - terminal states,
   - dependency satisfaction rules.
6. Mark the existing JSON files as migration inputs, not final authority.

### Validation

Documentation/config-only unless parser scripts are added. No repository
validation required for YAML docs alone.

## Phase 2: Migrate Current Queue And Policy To YAML

### Changes

1. Convert `policy.json` to `policy.yaml`.
2. Convert `queue.json` to `queue.yaml`.
3. Resolve the current policy drift:
   - set `enabled: false` unless an active run is intentionally in progress,
   - or update generated docs to say automation is currently enabled.
4. Normalize current terminal states:
   - terminal `pr_open` items must have report evidence,
   - terminal successful items must have archived plan paths unless explicitly
     marked `archive_deferred: true` with a reason.
5. Decide whether `pr_open` satisfies dependencies:
   - `true` only for explicitly stacked child branches,
   - otherwise `false` unless user-approved.

### Validation

YAML/config only. No repository validation required unless checker scripts are
changed in the same patch.

## Phase 3: Add YAML State Checker

### Changes

Add:

```text
tools/check_orchestrator_state.py
tools/check_orchestrator_state.bat
```

Checker behavior:

1. Parse YAML files using the approved parser.
2. Validate policy, queue, and machine schemas.
3. Confirm every queue state exists in the machine.
4. Confirm every declared transition target exists.
5. Fail if more than one item is in an active state.
6. Fail if a dependency is not in a dependency-satisfied state.
7. Fail if an item references a missing plan path.
8. Fail if a successful terminal item still points at an active plan without
   explicit `archive_deferred`.
9. Fail if `merge.allow: true` but `AGENTS.md` does not permit the merge path.
10. Print the next legal events for each active or ready item.

### Validation

At PR gate:

```bat
tools\validate_fast.bat
tools\check_orchestrator_state.bat --self-test
tools\check_orchestrator_state.bat
```

## Phase 4: Add State Advancement Tool

### Changes

Add:

```text
tools/advance_orchestrator_state.py
tools/advance_orchestrator_state.bat
```

Tool behavior:

1. Accept an item id and event name.
2. Load the YAML machine and queue.
3. Check whether the event is legal from the current state.
4. Evaluate guards in dry-run mode first.
5. Apply the transition only when guards pass.
6. Record the transition in run state.
7. Print required actions for the orchestrator to perform.
8. Refuse undeclared transitions.

Example:

```bat
tools\advance_orchestrator_state.bat water-rendering-cleanup start
tools\advance_orchestrator_state.bat water-rendering-cleanup worker_done
tools\advance_orchestrator_state.bat water-rendering-cleanup accepted
```

The first implementation can be dry-run only. Mutating `queue.yaml` can come
after the checker is trusted.

### Validation

At PR gate:

```bat
tools\validate_fast.bat
tools\advance_orchestrator_state.bat --self-test
```

## Phase 5: Generate Markdown From YAML

### Changes

Add:

```text
tools/render_orchestrator_docs.py
tools/render_orchestrator_docs.bat
```

Generated or refreshed docs:

```text
Agentic/Orchestrator/README.md
Agentic/Orchestrator/runbook.md
```

Generated docs should include:

- state list,
- terminal states,
- transition table,
- guard descriptions,
- action descriptions,
- queue-selection rules,
- report-only commit rules,
- current policy summary.

Markdown should say where it was generated from. Agents should be told that the
YAML files win when docs disagree.

### Validation

At PR gate:

```bat
tools\validate_fast.bat
tools\render_orchestrator_docs.bat --check
```

## Phase 6: Add Report-Only Commit Guardrails

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
   code, queue updates, YAML state files, or unreferenced images are included in
   the report-only commit.
5. Print a concise success summary that can be pasted into the final report.

### Validation

At PR gate:

```bat
tools\validate_fast.bat
tools\check_agent_report_commit.bat --self-test
```

## Phase 7: Strengthen Cross-Agent Instruction Discovery

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

- YAML files under `Agentic/Orchestrator` define orchestration behavior,
- Markdown runbooks are generated or secondary,
- no force-push, rebase, history rewrite, or direct `main` push,
- validation scripts are PR/commit gates,
- documentation-only changes require no validation,
- report-only commits must contain only report Markdown and referenced images,
- physics diagnostics must use SkullScope summaries instead of raw log dumps.

For Claude, keep `CLAUDE.md` as a small import/pointer file. Add Claude rules
or subagent definitions only after Phase 8 so they reflect the final YAML DSL.

### Validation

Documentation-only unless helper scripts are touched. No repository validation
required for instruction-file-only changes.

## Phase 8: Make Worker And Verifier Roles Tool-Native

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
- returns `accepted`, `needs_fixes`, or `blocked`,
- distinguishes blocking findings from non-blocking suggestions,
- treats missing required validation evidence as blocking unless explicitly
  delegated to the orchestrator.

These role files should reference the YAML state machine instead of restating
transition rules.

### Validation

Documentation/config-only role files require no repository validation. If a
script begins consuming the role metadata, use `tools\validate_fast.bat` and
the script's self-test.

## Phase 9: Add Run-State Schema And Resume Safety

### Changes

Define a stable run-state shape:

```text
Agentic/Orchestrator/schemas/run.schema.yaml
```

`run.yaml` should record:

- schema version,
- item id,
- current machine state,
- transition history,
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

Add resume rules:

1. If `run.yaml` exists for an item, resume from it instead of reconstructing
   state from memory.
2. If the policy snapshot changed mid-run, stop and require user confirmation
   before continuing risky actions.
3. If the worktree differs from the run's recorded baseline before the worker
   starts, record the difference and avoid reverting unrelated changes.

### Validation

Schema and documentation only: no repository validation required. If a helper
validates run state, use `tools\validate_fast.bat` plus its self-test.

## Phase 10: Add Optional Hard-Block Hooks

### Changes

Only add hooks for unambiguous danger zones:

- block force-push,
- block rebase,
- block direct push to `main`,
- block broad process kills such as `taskkill /IM`,
- warn before committing `Agentic/Runs`,
- warn before a report-only commit contains non-report files,
- warn when generated orchestrator Markdown is stale relative to YAML.

Hooks should print the exact rule and the file that owns it. They should not run
renderer, physics, perf, or full repository validation.

### Validation

At PR gate:

```bat
tools\validate_fast.bat
```

Also run any changed hook self-test if one exists.

## Phase 11: Workflow Evals

### Changes

Add deterministic agent-workflow eval fixtures:

```text
Agentic/Evals/
  README.md
  cases/
    docs-only-change.yaml
    shader-change.yaml
    physics-baseline-change.yaml
    tools-change.yaml
    orchestrator-report-only-commit.yaml
    illegal-transition.yaml
    stacked-pr-dependency.yaml
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
- whether direct `main` push, rebase, or force-push actions are forbidden,
- whether illegal YAML state transitions are rejected,
- whether `pr_open` dependencies require stacked-branch or user-approved mode.

### Validation

At PR gate:

```bat
tools\validate_fast.bat
tools\agent_workflow_eval.bat --self-test
```

## Recommended Order

1. Phase 1: define the YAML DSL.
2. Phase 2: migrate policy and queue to YAML.
3. Phase 3: add YAML state checker.
4. Phase 4: add dry-run state advancement.
5. Phase 5: generate Markdown from YAML.
6. Phase 6: add report-only commit checker.
7. Phase 7: add scoped cross-agent instructions.
8. Phase 8: add tool-native worker/verifier roles.
9. Phase 9: add run-state schema and resume safety.
10. Phase 10: add hard-block hooks.
11. Phase 11: add workflow evals.

Do not start merge automation until Phases 1-9 are working in PR-only mode.

## Acceptance Criteria

- YAML, not Markdown, defines orchestration states and transitions.
- `policy.yaml` and generated orchestrator docs agree about whether automation
  is enabled.
- Queue state names come from `roadmap-item.yaml`.
- A checker catches stale queue state before the orchestrator starts work.
- Undeclared transitions are rejected.
- `pr_open` dependencies are handled deliberately, especially for stacked
  branches.
- Terminal successful items have reports and archived source plans, or the YAML
  queue explains why they are not truly terminal.
- Copilot, Claude, and Codex all discover that YAML files are the
  orchestration source of truth.
- Worker and verifier roles are explicit, repeatable, and tool-native where
  supported.
- A missing independent verifier cannot accidentally become a claimed
  independent verification.
- Report-only commits are mechanically checkable.
- Hooks block only clear danger-zone actions and do not replace PR-gate
  validation.
- Agent workflow evals cover illegal transitions and historical agent failure
  modes.

## Validation Plan For This Markdown Change

This file is documentation-only under `Agentic/Plans`. No repository validation
is required for creating or editing this plan.
