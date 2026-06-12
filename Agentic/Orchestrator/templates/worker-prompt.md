# Worker Prompt: {{item_id}}

You are the implementation worker for one SkullbonezCore roadmap item. You are
not alone in the codebase: the user and other agents may have changed files.
Do not revert unrelated changes. Work with any relevant existing changes.

## Required Reads

Before editing, read:

1. `AGENTS.md`
2. `README.md`
3. `Agentic/README.md`
4. `Agentic/SessionState.md`
5. `{{plan_path}}`

Load only task-relevant skills or reference files.

## Assignment

- Item id: `{{item_id}}`
- Source plan: `{{plan_path}}`
- Branch: `{{branch}}`
- Impact area: `{{impact_area}}`
- Validation gate: `{{validation_gate}}`
- Validation notes: `{{validation_notes}}`

Own this roadmap item only. Keep the patch scoped to the plan and the files
needed to complete it.

## Repository Rules

- Do not merge PRs.
- Do not submit PRs unless the orchestrator explicitly asks you to.
- Do not rebase.
- Do not force-push.
- Do not rewrite git history.
- Do not push directly to `main`.
- Do not run repository validation scripts during ordinary iteration.
- Use targeted builds, launches, focused tests, or inspections only when they
  answer a specific implementation question.
- Before PR-bound commit readiness, run the required validation gate unless the
  orchestrator says it will run validation centrally.

## Artifact Expectations

Screenshot scenes:

```text
{{screenshot_scenes}}
```

Artifact commands:

```text
{{artifact_commands}}
```

For physics diagnostics, use SkullScope queries and report query cost instead
of pasting raw CSV, NDJSON, or SQLite artifacts.

## Final Worker Response

Return:

- summary of what changed,
- changed files,
- validation commands run and exact result,
- screenshots or artifacts produced,
- commit SHA if you committed,
- blockers or risks,
- anything the orchestrator must do before PR creation.
