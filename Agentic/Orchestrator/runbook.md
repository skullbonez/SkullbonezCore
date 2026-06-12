# Roadmap Orchestrator Runbook

This runbook is the manual contract for sequential roadmap orchestration. Keep
it authoritative until helper scripts exist.

## Scope

The orchestrator owns:

- queue selection,
- branch setup,
- worker prompt generation,
- worker result review,
- validation gate selection,
- artifact and screenshot collection,
- PR creation when policy allows it,
- report delivery,
- queue state updates.

The worker owns only the assigned roadmap implementation.

## Preconditions

1. Read `AGENTS.md`, `README.md`, `Agentic/README.md`, and
   `Agentic/SessionState.md`.
2. Confirm `git status --short --branch` is understood.
3. Read `Agentic/Orchestrator/policy.json`.
4. Read `Agentic/Orchestrator/queue.json`.
5. Stop if `policy.json` has `enabled: false`, unless the user explicitly asks
   for a dry-run setup step.
6. Do not merge PRs. `AGENTS.md` currently forbids merges and PR submission.

## Item Selection

1. Find the highest-priority item with `status: ready`.
2. Confirm every item in `depends_on` is terminal and acceptable:
   `merged`, `skipped`, or explicitly user-approved.
3. Confirm no other item is `running`.
4. Mark the selected item `running` only after the run directory is created.

## Run Directory

Create:

```text
Agentic/Runs/<yyyy-mm-dd>/<item-id>/
```

Expected files:

```text
run.json
worker-prompt.md
worker-result.md
validation.log
pr.md
report.md
screenshots/
artifacts/
```

Do not ingest large raw diagnostic files into the model. For physics work, use
SkullScope query output and report the query cost required by `AGENTS.md`.

## Branch Setup

1. Start from `policy.base_branch`, normally `main`.
2. Fetch the base branch.
3. Create or switch to the item's configured branch.
4. Do not rebase.
5. Do not force-push.
6. Do not push directly to `main`.

## Worker Delegation

Generate `worker-prompt.md` from
`Agentic/Orchestrator/templates/worker-prompt.md`.

Spawn exactly one worker for the selected item. The worker prompt must include:

- source plan path,
- impact area,
- branch name,
- validation gate and notes,
- screenshot and artifact commands,
- explicit write ownership,
- current merge restrictions.

The orchestrator should not begin another roadmap item while a worker is active.

## Review And Validation

After the worker returns:

1. Record the worker final message in `worker-result.md`.
2. Review `git status --short`.
3. Review `git diff --stat` and the changed files.
4. Reject unrelated edits unless they are necessary and explained.
5. Run the smallest required PR gate from the queue entry and `AGENTS.md`.
6. For documentation-only changes, state that no repository validation is
   required.
7. Preserve validation output in `validation.log` when validation is run.

Repository validation scripts are PR/commit gates. Do not run them repeatedly
during iteration.

## Artifacts

Run only artifact commands declared by the queue item or needed by the touched
area.

Examples:

- renderer work: screenshots, renderer diff summaries, `dx12_validation.txt`,
- physics work: SkullScope summaries and focused queries,
- tooling work: command output and dry-run summaries,
- documentation work: no screenshots unless useful.

Store generated files under the run directory.

## PR Handling

If `allow_pr_creation` is true and the branch is ready:

1. Commit the work with useful commit notes.
2. Push the feature branch.
3. Open or update the PR through the GitHub app or `gh` fallback.
4. Save PR metadata in `pr.md`.
5. Do not attempt to approve the PR from the same account/token that authored
   it. GitHub rejects self-approval, and approval is not required unless branch
   protection explicitly says a separate review is needed.
6. Post the generated report as a PR comment when the configured channel is
   available.

If `allow_pr_creation` is false, stop after local branch, commit, and report.

## Merge Handling

Do not merge.

Future merge automation requires both:

- an explicit `AGENTS.md` policy update, and
- `policy.json` with `allow_merge: true`.

Until both exist, the report must say `Merge status: not permitted by repo
policy`.

If the user grants one-off permission to merge a specific PR before permanent
merge automation exists, do not insert an approval step. Check mergeability,
required checks, and the expected head SHA, then merge directly if GitHub allows
it. If GitHub reports that an approving review is required, stop and report that
an external reviewer is needed; self-approval will not satisfy that requirement.

## Reporting

Generate `report.md` from `Agentic/Orchestrator/templates/report.md` for every
terminal outcome:

- `pr-open`,
- `merged`,
- `blocked`,
- `failed`,
- `skipped`.

Reports must include:

- item id and source plan,
- branch, commit, and PR link when present,
- validation command and output summary,
- screenshot and artifact paths,
- merge status,
- conflicts and resolutions,
- residual risk,
- next queue action.

## Queue Update

Set the item status:

- `pr-open` when a PR exists and merge is not permitted,
- `merged` only when policy and repo rules permitted a merge and it succeeded,
- `blocked` when user input or external state is required,
- `failed` when implementation or validation invalidated the approach,
- `skipped` only by explicit user or policy decision.

Stop after `blocked` or `failed` unless policy explicitly allows advancement.
