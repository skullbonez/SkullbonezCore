# Agentic Orchestrator

This folder contains the repository-owned control files for JSON-only,
parallel-capable roadmap orchestration. Implementing work from `Agentic/Plans`
defaults to this orchestrator workflow unless the user explicitly asks to
bypass it.

JSON is the single source of truth for orchestrator behavior. Markdown explains
the process for humans, but only JSON files drive tool decisions.

## Files

| File | Purpose |
|------|---------|
| `policy.json` | Executable revocable automation policy. Set `enabled` to `false` to stop the loop. |
| `queue.json` | Executable roadmap queue. Only listed items are eligible for orchestration. |
| `agent-loop.json` | Executable loop map for implementing work sourced from `Agentic/Plans`. |
| `machines/roadmap-item.json` | Executable item state machine: states, legal transitions, guards, and actions. |
| `tools/orchestrator.py` | Mechanical state checker, transition helper, prompt renderer, Codex exec wrapper, and report-commit checker. |
| `tools/orchestrator.bat` | Windows launcher for the Python helper. |
| `schemas/*.json` | JSON schemas for structured worker, verifier, and run-state artifacts. |
| `runbook.md` | Manual orchestrator procedure before scripting. |
| `templates/worker-prompt.md` | Prompt template for one implementation worker. |
| `templates/verifier-prompt.md` | Prompt template for the independent completion verifier. |
| `templates/report.md` | Required report shape for every completed, failed, or blocked item. |

To kick off the loop without pasting a one-off prompt, use
`Agentic/Skills/skore-roadmap-orchestrator/skill.md`.

Historical setup report from before `Agentic/Reports` became the report
destination:
[`generic-roadmap-orchestrator-skill`](../Reports/2026-06-13/generic-roadmap-orchestrator-skill/report.md).

## Current Safety Defaults

- The orchestrator is enabled by default for implementation work from
  `Agentic/Plans`; set `policy.json` `enabled` to `false` to pause the loop.
- JSON files define executable policy, queue state, loop rules, and legal transitions.
- Codex worker/verifier runs use the sandbox configured in `policy.json`;
  current Windows CLI smoke tests require `danger-full-access`, with verifier
  tracked-worktree comparison as the safety guard.
- PR creation is allowed by policy only after explicit user authorization; the
  default successful terminal path is `done` without PR creation.
- Merge automation is disabled by default.
- `AGENTS.md` still requires explicit user authorization for PR submission and
  merges, so policy alone cannot authorize them.
- Queue execution is parallel-capable: independent ready items may start while
  other items are active only when `policy.max_active_items`, dependencies,
  `owned_globs`, policy `exclusive_globs`, and impact-area limits all allow it.
  Items without `owned_globs` are treated as exclusive.
- Parallel writer agents should run in isolated git worktrees; the orchestrator
  owns final integration, validation, and report state.
- Successful completion requires an independent verifier pass after the worker
  claims the task is done. Blocking verifier findings go back to the worker,
  and the worker/verifier loop repeats until a verifier accepts the work or the
  item becomes blocked or failed.
- Chained roadmap items use stacked child branches. If the user asks for tasks
  1-3 as one chain, task 1 branches from `main`, task 2 branches from task 1,
  and task 3 branches from task 2. Review each child PR against its parent until
  the parent branch lands.

## Run Artifacts

When an item runs, create:

```text
Agentic/Runs/<yyyy-mm-dd>/<item-id>/
```

Use that folder for generated prompts, worker results, validation logs,
verifier prompts/results, screenshots, artifacts, PR notes, and local
orchestration state. This folder is not the user-facing report commit.

## Reports

When the task is done, create the committed report at:

```text
Agentic/Reports/<yyyy-mm-dd>/<item-id>/report.md
Agentic/Reports/<yyyy-mm-dd>/<item-id>/images/
```

A roadmap item is not complete merely because implementation commits were
pushed. Completion requires both:

- a completed worker/verifier feedback loop with no blocking verifier findings,
- a committed report under `Agentic/Reports/<yyyy-mm-dd>/<item-id>/`, and
- a terminal queue state: `done`, `pr_open`, `merged`, `blocked`, `failed`, or
  `skipped`.

Use `done` for successful completed work when no PR or merge is being recorded.
Do not send a final successful user response until the report path or report web
URL and the terminal queue state are both known.

The final feature-branch commit for a task is a report-only commit. It must
contain only `report.md` and image files under `images/` that are referenced by
relative Markdown links from the report. Do not include run JSON, worker
prompts, validation logs, PR notes, queue updates, plan moves, source changes,
raw artifacts, or unreferenced images in that commit.

The first section of `report.md` must explain what was done in plain language
for a non-engineer. Embed report images and diagrams throughout the report next
to the text they support instead of collecting them in a dedicated
standalone image section. Report visuals can include screenshots, focused zoom
crops, heat maps, image diffs, artifact previews, and before/after architectural
diagrams; err on the side of more useful visuals rather than fewer.

After pushing the report-only commit, return a GitHub web link to the committed
`report.md` file.

## Core Commands

```bat
tools\orchestrator.bat check
tools\orchestrator.bat check --self-test
tools\orchestrator.bat doctor
tools\orchestrator.bat next
tools\orchestrator.bat start <item-id>
tools\orchestrator.bat run-worker <item-id>
tools\orchestrator.bat transition <item-id> worker_done --result <path>
tools\orchestrator.bat verifier-prompt <item-id>
tools\orchestrator.bat run-verifier <item-id>
tools\orchestrator.bat run-loop [item-id] --finalize --commit-finalize
tools\orchestrator.bat finalize <item-id> --commit
tools\orchestrator.bat archive-plan <item-id>
tools\orchestrator.bat report-draft <item-id>
tools\orchestrator.bat report-check --commit <sha>
```
