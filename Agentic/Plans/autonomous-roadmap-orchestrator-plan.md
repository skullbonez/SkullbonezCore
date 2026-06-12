# Autonomous Roadmap Orchestrator Plan

Status: planning draft
Created: 2026-06-12
Scope: Agentic process, roadmap queueing, sub-agent handoff, PR reporting, merge policy
Implementation status: plan only, no code changes in this pass

## Goal

Set up this repository so a Codex orchestrator can work through roadmap items in
`Agentic/Plans` without requiring the user to repeatedly return to the computer
to start the next item, open the PR, gather artifacts, and prepare the handoff.

The orchestrator should run one roadmap item at a time, delegate the actual work
to a focused sub-agent, verify the result, create a report with useful evidence,
then move to the next eligible item.

## Current Constraints

- `AGENTS.md` currently says agents must not merge or submit PRs.
- Feature-branch commits and normal pushes are allowed without asking.
- Direct commits or pushes on `main` still require explicit user confirmation.
- Validation scripts are PR/commit gates, not commands to run repeatedly during
  iteration.
- The active plan folder contains a mix of implementation plans, architecture
  design notes, failed attempts, rejected plans, and completed work. Directory
  location is not enough to decide what should run.
- Session state currently references an `Agentic/Plans/agent-loop` demo folder
  that is not present in the worktree. The orchestrator setup should either
  restore it intentionally or remove the stale references in a separate cleanup.

## Design Principles

1. One active roadmap item at a time.
2. One implementation branch per roadmap item.
3. One worker sub-agent owns the implementation write scope for that item.
4. The orchestrator owns queue selection, policy checks, validation selection,
   PR creation, reporting, and merge decisions.
5. Merge authority is explicit, revocable, and recorded in the repo.
6. Every completed item leaves a durable report, even if the PR fails or is
   blocked.
7. Failed items do not poison the queue. They are reported, paused, and the
   orchestrator only advances according to the configured failure policy.

## Proposed Repository Additions

### 1. Orchestrator Policy

Add:

```text
Agentic/Orchestrator/policy.json
```

Suggested fields:

```json
{
  "enabled": false,
  "max_active_items": 1,
  "allow_pr_creation": true,
  "allow_merge": false,
  "merge_requires_green_checks": true,
  "merge_requires_approval": false,
  "merge_method": "squash",
  "advance_after_failed_item": false,
  "report_channels": ["markdown"],
  "email_reports": {
    "enabled": false,
    "to": []
  }
}
```

This gives the user a single obvious place to revoke automation:

- set `enabled` to `false` to stop the loop,
- set `allow_merge` to `false` to keep PRs open for manual merge,
- set `allow_pr_creation` to `false` to allow only local branches and reports.

Before `allow_merge` can be used, `AGENTS.md` must be amended to permit merges
only through this policy file. Until then, the universal repo instruction blocks
agent merges.

### 2. Roadmap Queue Manifest

Add:

```text
Agentic/Orchestrator/queue.json
```

The manifest should list only intentional work items. Do not infer queue order
from every file in `Agentic/Plans`.

Suggested entry shape:

```json
{
  "id": "pix-profiling-integration",
  "plan": "Agentic/Plans/pix-profiling-integration-plan.md",
  "status": "ready",
  "priority": 10,
  "branch": "codex/pix-profiling-integration",
  "impact_area": ["DX11", "DX12", "rendering", "tooling"],
  "validation_gate": "tools\\validate_full.bat",
  "screenshot_scenes": [
    {
      "name": "perf-test-gl",
      "command": "Profile\\SKULLBONEZ_CORE.exe --renderer gl --scene SkullbonezData\\scenes\\perf_test.scene --screenshot"
    }
  ],
  "depends_on": [],
  "merge_policy": "policy-default",
  "report_required": true
}
```

Allowed statuses:

- `ready`
- `running`
- `pr-open`
- `merged`
- `blocked`
- `failed`
- `skipped`

The queue should initially be populated manually from the top-level active plans,
not from `Done`, `Failed`, or `Rejected`.

### 3. Per-Item Run State

Add a run directory per attempt:

```text
Agentic/Runs/<yyyy-mm-dd>/<item-id>/
  run.json
  worker-prompt.md
  worker-result.md
  validation.log
  pr.md
  report.md
  screenshots/
  artifacts/
```

`run.json` records machine-readable state:

- item id,
- start/end timestamps,
- branch,
- commit SHA,
- PR URL/number,
- validation commands and status,
- screenshot artifact paths,
- merge status,
- failure reason if any.

### 4. Worker Prompt Template

Add:

```text
Agentic/Orchestrator/templates/worker-prompt.md
```

The prompt should include:

- read `AGENTS.md`, `README.md`, `Agentic/README.md`, and `Agentic/SessionState.md`,
- read only the assigned plan plus needed skill files,
- state impact area and deferred validation gate,
- own only the assigned roadmap item,
- do not merge, rebase, force-push, or rewrite history,
- do not revert unrelated user or agent changes,
- commit on the feature branch only when the item is ready,
- report changed files, validation output, screenshots, and blockers.

Even though the future workflow is sequential, each worker must still be told
that other agents or the user may have changed the repo.

### 5. Report Template

Add:

```text
Agentic/Orchestrator/templates/report.md
```

Each item report should include:

- roadmap item and source plan,
- branch, commit, and PR link,
- summary of implementation,
- changed files by area,
- validation command output or explicit reason validation was not required,
- screenshots and artifact links,
- PR status,
- merge status,
- conflicts encountered and how they were resolved,
- residual risk,
- next queue item selected or reason the loop stopped.

Reports should be committed into `Agentic/Runs/.../report.md` or uploaded as PR
artifacts/comments. Markdown is the baseline because it is durable in the repo.
Email can be added later as a delivery adapter after the report exists.

### 6. Screenshot And Artifact Contract

Each queue item should declare screenshot or artifact commands that match its
scope.

Examples:

- renderer/shader/material/water: renderer screenshots, diff composites, and
  `dx12_validation.txt`,
- physics: SkullScope summary/events queries and bounded report output, not raw
  CSV/NDJSON ingestion,
- tooling/docs: command output or rendered docs, no renderer screenshots unless
  relevant,
- perf: profiler JSON/CSV summary and baseline comparison output.

The report should include file paths for local artifacts and PR-uploaded links
when available.

### 7. Merge Policy

There are two viable modes.

Mode A: PR-only automation

- Orchestrator creates PRs and reports.
- User manually merges.
- Lowest risk and compatible with the current `AGENTS.md`.

Mode B: policy-gated merge automation

- `AGENTS.md` is updated to allow merges only when:
  - `Agentic/Orchestrator/policy.json` has `enabled: true`,
  - `allow_merge: true`,
  - the current branch is not `main`,
  - the PR is for the current queue item,
  - required validation/checks are green,
  - the report has been produced,
  - no unresolved review threads remain unless policy permits them.
- The merge action, method, and resulting SHA are recorded in `run.json` and
  `report.md`.
- After merge, the orchestrator updates the local base branch before starting
  the next item.

The first implementation should start with Mode A. Move to Mode B only after one
or two dry runs prove the reports and queue state are reliable.

### 8. Delivery Channels

Start with durable Markdown:

- write `Agentic/Runs/.../report.md`,
- post the same report as a PR comment,
- optionally add a final issue comment on a tracking issue.

Add email only after the Markdown report is stable. The email integration should
send the exact same generated report body plus links to screenshots/artifacts.
Avoid making email the source of truth.

## Orchestrator Loop

For each queue item:

1. Load `policy.json`.
2. Stop immediately if `enabled` is false.
3. Select the highest-priority `ready` item whose dependencies are complete.
4. Mark it `running` in `queue.json` and create `Agentic/Runs/...`.
5. Ensure the base branch is up to date.
6. Create or switch to the item feature branch.
7. Generate the worker prompt from the queue entry and source plan.
8. Spawn one worker sub-agent for the item.
9. Wait for the worker to finish.
10. Review changed files and reject out-of-scope edits.
11. Run the required pre-commit/PR validation gate in a visible console window.
12. Capture declared screenshots/artifacts.
13. Commit and push the feature branch if needed.
14. Create or update the PR if `allow_pr_creation` is true.
15. Generate `report.md`.
16. Deliver the report through configured channels.
17. If merge mode is enabled, merge only after all policy gates pass.
18. Update the queue item to `merged`, `pr-open`, `blocked`, or `failed`.
19. Update local base branch after merge.
20. Continue to the next item only if policy allows it.

## Conflict Handling

- If the branch cannot rebase or merge cleanly from the updated base, stop the
  item and mark it `blocked`.
- Do not force-push or rewrite history.
- Do not auto-resolve broad conflicts in high-risk areas such as DX12 resource
  barriers, physics determinism, validation scripts, or baseline files.
- Include conflict files and the attempted command in the report.

## Failure Handling

An item is `failed` when implementation or validation proves the approach is
wrong but the repo is otherwise healthy.

An item is `blocked` when progress requires user input, external credentials,
missing tools, unresolved merge conflicts, or a policy decision.

Default behavior should be conservative:

- stop after `blocked`,
- stop after `failed`,
- do not advance to unrelated work until the user opts in.

Later, `advance_after_failed_item` can allow the loop to continue to independent
items.

## Initial Queue Candidates

Based on the current top-level `Agentic/Plans` inventory, initial candidates
could include:

1. `pix-profiling-integration-plan.md`
2. `validation-harness-upgrade-plan.md`
3. `render-resource-lifetime-plan.md`
4. `render-pipeline-extraction-plan.md`
5. `shader-architecture-cleanup-plan.md`
6. `asset-texture-registry-plan.md`
7. `material-system-v1-implementation-plan.md`
8. `water-rendering-cleanup-plan.md`
9. `replay-system-plan.md`
10. `worker-system-plan.md`

These should not all be marked `ready` immediately. Several are large and
overlapping. The first queue pass should pick a narrow, low-risk item that tests
the reporting workflow before enabling automated merging.

## Recommended Rollout

### Phase 0: Policy Decision

- Decide whether the first version is PR-only or merge-capable.
- If merge-capable, update `AGENTS.md` with exact policy-gated merge language.
- Keep `allow_merge` false by default.

Validation: documentation-only, no validation required.

### Phase 1: Queue And Templates

- Add `Agentic/Orchestrator/policy.json`.
- Add `Agentic/Orchestrator/queue.json`.
- Add worker and report templates.
- Add one or two dry-run queue entries.

Validation: documentation/process files only, no validation required.

### Phase 2: Manual Orchestrator Runbook

- Add `Agentic/Orchestrator/runbook.md`.
- Document the exact human-readable loop before scripting it.
- Include rules for branch creation, validation choice, screenshot capture, PR
  creation, report delivery, and queue advancement.

Validation: documentation-only, no validation required.

### Phase 3: Scripted State Helpers

- Add a small helper under `tools/` or `Agentic/Orchestrator/` to:
  - select the next item,
  - mark item status,
  - create a run directory,
  - render worker/report templates,
  - validate queue schema.

Validation at PR gate: `tools\validate_fast.bat`, then run the changed helper's
own self-check command.

### Phase 4: PR Report Automation

- Generate `report.md` from `run.json`.
- Post report as a PR comment when the GitHub app/CLI is available.
- Keep Markdown file generation as the source of truth.

Validation at PR gate: `tools\validate_fast.bat`, plus a dry-run report
generation command.

### Phase 5: Screenshot Artifact Automation

- Add item-scoped screenshot commands.
- Store artifacts under `Agentic/Runs/.../screenshots`.
- Include local paths and PR artifact links in the report.

Validation depends on scope:

- docs/tooling-only changes: `tools\validate_fast.bat`,
- renderer screenshot harness changes: `tools\validate_renderers.bat`.

### Phase 6: Optional Merge Automation

- Enable only after PR-only mode works.
- Keep revocation as a one-line policy edit.
- Require green validation/checks and generated report before merge.
- Record merge SHA and status.

Validation: documentation/process update is no validation; any tool changes use
`tools\validate_fast.bat` plus the changed helper self-check.

## Open Decisions

1. Should the first working version be PR-only, or should the repo policy be
   changed now to permit policy-gated merges?
2. Should reports be delivered only as Markdown/PR comments first, or should an
   email adapter be part of the initial implementation?
3. Should blocked/failed items stop the whole loop by default?
4. Which one or two roadmap items should be used for the first dry run?
5. Should reports be stored permanently in the repo, or only as PR comments and
   CI artifacts?

## Acceptance Criteria

- The user can revoke automation by editing one policy file.
- The queue contains explicit, ordered roadmap items.
- The orchestrator never runs more than one roadmap item at a time.
- Every item produces a report, including failed or blocked items.
- Reports include PR status, validation status, screenshots/artifacts when
  relevant, and next-step state.
- Automated merging is impossible unless both `AGENTS.md` and
  `policy.json` explicitly allow it.
- The next item starts only after the previous item reaches a terminal queue
  state according to policy.
