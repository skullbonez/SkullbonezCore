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

## Current Safety Defaults

- The orchestrator is disabled by default.
- PR creation is allowed by policy, but only when the orchestrator is enabled.
- Merge automation is disabled by default.
- PR approval is not part of the normal automation path. GitHub rejects
  self-approval for PRs authored by the same account/token, and this repository
  does not require approval for ordinary owner-created PRs.
- `AGENTS.md` still forbids merges and PR submission, so policy alone cannot
  authorize merges.
- Queue execution is sequential: one active roadmap item at a time.

## Run Artifacts

When an item runs, create:

```text
Agentic/Runs/<yyyy-mm-dd>/<item-id>/
```

Use that folder for generated prompts, worker results, validation logs,
screenshots, artifacts, PR notes, and the final report.
