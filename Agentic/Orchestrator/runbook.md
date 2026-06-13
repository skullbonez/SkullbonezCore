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

The `<item-id>` folder name must match the task id. This folder is the durable
evidence bundle for the item and must be committed on the feature branch as the
final pre-merge commit. Keep `report.md` and selected phone-readable images in
this folder so the user can review progress from GitHub on a phone.

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
8. Record substantial timings, including validation, builds, launches, artifact
   generation, and long investigations.

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

Prefer PNG or JPG for committed phone-review images. If the runtime produces BMP
captures, convert selected captures to PNG before embedding them in `report.md`.
Commit only the useful report bundle and small supporting artifacts; keep bulky
raw validation outputs out of the commit unless they are necessary evidence.

## PR Handling

If `allow_pr_creation` is true and the branch is ready:

1. Commit the work with useful commit notes.
2. Generate or update the task evidence folder, including `report.md`,
   `run.json`, `worker-result.md`, selected phone-readable images, and any
   small useful artifacts.
3. Commit the evidence folder as the final pre-merge commit on the feature
   branch.
4. Push the feature branch.
5. Open or update the PR through the GitHub app or `gh` fallback.
6. Save PR metadata in `pr.md`.
7. Post the generated report as a PR comment when the configured channel is
   available.

If `allow_pr_creation` is false, stop after local branch, commit, and report.

## Merge Handling

Do not merge.

Future merge automation requires both:

- an explicit `AGENTS.md` policy update, and
- `policy.json` with `allow_merge: true`.

Until both exist, the report must say `Merge status: not permitted by repo
policy`.

When merge automation is permitted, do not merge until the final evidence commit
is present on the PR. Because the merge SHA does not exist until after merge,
the committed report may say merge pending; record the merge SHA in the final
response and PR comment unless the user explicitly requests a separate
post-merge report update.

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
- implementation commit and final evidence commit when they differ. Because
  `report.md` is committed inside the evidence commit, the committed report may
  list the evidence commit as pending and the final response or PR comment
  should record the actual SHA,
- started, finished, elapsed, and substantial sub-run timings,
- a short progress timeline,
- validation command and output summary,
- screenshot and artifact paths,
- embedded relative links for selected phone-readable images,
- short interesting code snippets with file paths,
- merge status,
- conflicts and resolutions,
- residual risk,
- sub-agent result summary and `worker-result.md` path,
- next queue action.

## Queue Update

Set the item status:

- `pr-open` when a PR exists and merge is not permitted,
- `merged` only when policy and repo rules permitted a merge and it succeeded,
- `blocked` when user input or external state is required,
- `failed` when implementation or validation invalidated the approach,
- `skipped` only by explicit user or policy decision.

Stop after `blocked` or `failed` unless policy explicitly allows advancement.
