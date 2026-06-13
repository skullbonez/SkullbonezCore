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
- final report-only commit creation,
- source plan archiving for completed items,
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
screenshots/
artifacts/
```

The `<item-id>` folder name must match the task id. This folder is local
orchestration state: prompts, worker results, validation logs, raw artifacts,
PR notes, and any bulky diagnostics. Do not include `Agentic/Runs` files in the
final report-only commit. Commit run-state files only when explicitly useful
outside the report, and never in the report-only commit.

## Report Directory

Create the user-facing committed report under:

```text
Agentic/Reports/<yyyy-mm-dd>/<item-id>/
  report.md
  images/
```

The final report commit must contain only files in this report folder:

- `report.md`,
- PNG or JPG images under `images/` that are referenced by `report.md`.

Do not put `run.json`, `worker-prompt.md`, `worker-result.md`,
`validation.log`, `pr.md`, source code, queue updates, source-plan moves, raw
artifacts, or unreferenced images in the report commit.

Every image committed with the report must be referenced from `report.md` using
a relative Markdown link, for example:

```markdown
![Renderer parity](images/renderer-parity.png)
```

Report images can be screenshots, zoomed-in crops that focus on one relevant
part of the screen, heat maps, image diffs, and before/after architectural
diagrams. Choose images that explain what changed or why the result is
trustworthy, not just whatever artifacts were produced.

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
- report images: full-screen screenshots, focused zoom crops, heat maps, image
  diffs, and before/after architectural diagrams.

Store generated files under the run directory. Copy only selected
phone-readable PNG or JPG images into the report directory's `images/` folder.

Prefer PNG or JPG for committed phone-review images. If the runtime produces BMP
captures, convert selected captures to PNG before embedding them in `report.md`.
Commit only the report Markdown and referenced images in the final report-only
commit. Keep bulky raw validation outputs and intermediate artifacts out of that
commit.

## PR Handling

If `allow_pr_creation` is true and the branch is ready:

1. Commit the work with useful commit notes.
2. For a successful item, archive the source plan as described in
   [Plan Archive](#plan-archive).
3. Commit any source-plan archive and queue/status updates before the report
   commit. These are task-state changes, not report files.
4. Push the feature branch.
5. Open or update the PR through the GitHub app or `gh` fallback.
6. Save PR metadata in `pr.md` under the run directory.
7. Generate `Agentic/Reports/<yyyy-mm-dd>/<item-id>/report.md` and copy only
   referenced PNG/JPG images into `Agentic/Reports/<yyyy-mm-dd>/<item-id>/images/`.
8. Commit the report directory as the final report-only commit. This commit
   must contain only `report.md` and images referenced by that Markdown file.
9. Push the report-only commit.
10. Post the generated report, including the report web URL, as a PR comment
    when the configured channel is available.

If `allow_pr_creation` is false, successful items still run
[Plan Archive](#plan-archive) before the final local report commit. The
orchestrator should still push the report-only commit and provide a web link
unless policy or the user forbids pushing; if a push is forbidden or fails,
state that no report web link could be produced.

## Merge Handling

Do not merge.

Future merge automation requires both:

- an explicit `AGENTS.md` policy update, and
- `policy.json` with `allow_merge: true`.

Until both exist, the report must say `Merge status: not permitted by repo
policy`.

When merge automation is permitted, do not merge until the final report-only
commit is present on the PR. Because the merge SHA does not exist until after
merge, the committed report may say merge pending; record the merge SHA in the
final response and PR comment unless the user explicitly requests a separate
post-merge report update.

## Reporting

Generate `Agentic/Reports/<yyyy-mm-dd>/<item-id>/report.md` from
`Agentic/Orchestrator/templates/report.md` for every terminal outcome:

- `pr-open`,
- `merged`,
- `blocked`,
- `failed`,
- `skipped`.

Reports must include:

- a first section that explains what was done in plain language for a
  non-engineer. This section must come before metadata, commits, validation,
  file lists, or implementation details,
- item id and source plan,
- archived source plan path when the item completed successfully,
- branch, implementation commit, report commit, PR link, and report web URL
  when present,
- a report web URL that opens the committed Markdown file in GitHub. Use the
  feature-branch URL for PR-open work and the `main` URL after a successful
  merge,
- started, finished, elapsed, and substantial sub-run timings,
- a short progress timeline,
- validation command and output summary,
- screenshot and artifact paths,
- embedded relative links for selected phone-readable images such as
  screenshots, focused zoom crops, heat maps, image diffs, or before/after
  architectural diagrams,
- short interesting code snippets with file paths,
- merge status,
- conflicts and resolutions,
- residual risk,
- sub-agent result summary and `worker-result.md` path,
- next queue action.

The final response must include the report web URL. If the orchestrator cannot
produce a web URL because pushing is blocked or credentials are unavailable, it
must say so explicitly and provide the local report path instead.

## Plan Archive

When an item reaches a successful terminal state, move its source plan out of
the active plan folder so future agents do not confuse completed plans for
runnable work.

Successful terminal states are:

- `pr-open`, when the implementation and required PR gate passed and the
  repository policy does not permit merging,
- `merged`, when policy and repository rules permitted a merge and it
  succeeded,
- an explicit user decision that the roadmap item is complete.

Archive rules:

- Move only the assigned source plan, not unrelated plans.
- Move from `Agentic/Plans/<file>.md` to `Agentic/Plans/Done/<file>.md`.
- Use `git mv` when possible so the rename is preserved in history.
- Do not overwrite an existing file in `Agentic/Plans/Done`; stop and report
  the collision instead.
- Update the queue entry's `plan` path to the archived path in `queue.json`.
- Record both the original source plan path and archived path in `run.json` and
  `report.md`.
- Commit source-plan moves and queue updates before the report-only commit, not
  inside it.
- Keep blocked and failed plans in the active folder unless the user explicitly
  asks to move them to `Failed` or another archive folder.
- Do not ask the implementation worker to archive the plan. The orchestrator
  owns this step because it also owns validation, reports, PR state, and queue
  state.

## Queue Update

Set the item status:

- `pr-open` when a PR exists and merge is not permitted,
- `merged` only when policy and repo rules permitted a merge and it succeeded,
- `blocked` when user input or external state is required,
- `failed` when implementation or validation invalidated the approach,
- `skipped` only by explicit user or policy decision.

Stop after `blocked` or `failed` unless policy explicitly allows advancement.
