# Agentic Orchestrator

This folder contains the repository-owned control files for sequential roadmap
orchestration.

The current implementation is intentionally policy and runbook only. It does
not grant merge authority, run scripts by itself, or change the repository's
universal agent rules.

## Files

| File | Purpose |
|------|---------|
| `policy.json` | Revocable automation policy. Set `enabled` to `false` to stop the loop. |
| `queue.json` | Explicit roadmap queue. Only listed items are eligible for orchestration. |
| `runbook.md` | Manual orchestrator procedure before scripting. |
| `templates/worker-prompt.md` | Prompt template for one implementation worker. |
| `templates/report.md` | Required report shape for every completed, failed, or blocked item. |

To kick off the loop without pasting a one-off prompt, use
`Agentic/Skills/skore-roadmap-orchestrator/skill.md`.

Historical setup report from before `Agentic/Reports` became the report
destination:
[`generic-roadmap-orchestrator-skill`](../Reports/2026-06-13/generic-roadmap-orchestrator-skill/report.md).

## Current Safety Defaults

- The orchestrator is disabled by default.
- PR creation is allowed by policy, but only when the orchestrator is enabled.
- Merge automation is disabled by default.
- `AGENTS.md` still forbids merges and PR submission, so policy alone cannot
  authorize merges.
- Queue execution is sequential: one active roadmap item at a time.

## Run Artifacts

When an item runs, create:

```text
Agentic/Runs/<yyyy-mm-dd>/<item-id>/
```

Use that folder for generated prompts, worker results, validation logs,
screenshots, artifacts, PR notes, and local orchestration state. This folder is
not the user-facing report commit.

## Reports

When the task is done, create the committed report at:

```text
Agentic/Reports/<yyyy-mm-dd>/<item-id>/report.md
Agentic/Reports/<yyyy-mm-dd>/<item-id>/images/
```

The final feature-branch commit for a task is a report-only commit. It must
contain only `report.md` and image files under `images/` that are referenced by
relative Markdown links from the report. Do not include run JSON, worker
prompts, validation logs, PR notes, queue updates, plan moves, source changes,
raw artifacts, or unreferenced images in that commit.

The first section of `report.md` must explain what was done in plain language
for a non-engineer. Report images can include screenshots, focused zoom crops,
heat maps, image diffs, and before/after architectural diagrams.

After pushing the report-only commit, return a GitHub web link to the committed
`report.md` file.
