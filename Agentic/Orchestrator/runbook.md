# Roadmap Orchestrator Runbook

This runbook explains the manual procedure for JSON-only roadmap orchestration.
`tools/orchestrator.py` enforces the JSON policy, queue, loop map, and state
machine. Markdown is explanatory; JSON is the only orchestrator control source.

## Scope

The orchestrator owns:

- queue selection,
- branch setup,
- worker prompt generation,
- worker result review,
- independent verifier delegation,
- worker/verifier feedback loops,
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
3. Run `tools\orchestrator.bat check`.
4. Read `Agentic/Orchestrator/policy.json`.
5. Read `Agentic/Orchestrator/queue.json`.
6. Read `Agentic/Orchestrator/machines/roadmap-item.json`.
7. Read `Agentic/Orchestrator/agent-loop.json` as the loop map.
8. Stop if `policy.json` has `enabled: false`, unless the user explicitly asks
   for a dry-run setup step or policy editing.
9. Do not merge PRs. `AGENTS.md` requires explicit user authorization for PR
   submission and merges.

## Item Selection

1. Use `tools\orchestrator.bat next` or find the highest-priority item with
   `state: ready`.
2. Confirm every item in `depends_on` is terminal and acceptable:
   `done`, `merged`, `pr_open` for stacked children, `skipped` only when the
   queue says explicit-only or the user approved it.
3. Confirm the selected item fits `policy.max_active_items`,
   `policy.parallelism`, dependencies, `owned_globs`, and active-item conflict
   rules.
4. Mark the selected item `running` only through
   `tools\orchestrator.bat start <item-id>` or
   `tools\orchestrator.bat run-loop <item-id>`, which creates the run
   directory and switches to the item branch.

## Run Directory

Create:

```text
Agentic/Runs/<yyyy-mm-dd>/<item-id>/
```

Expected files:

```text
run.json
worker-prompt.md
worker-result.json
verification-rounds/
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

Use `verification-rounds/` for independent verifier prompts, verifier results,
and worker responses to verifier feedback. Name files by round, for example:

```text
verification-rounds/
  round-01-verifier-prompt.md
  round-01-verifier-result.md
  round-01-worker-response.md
```

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

Do not put `run.json`, `worker-prompt.md`, `worker-result.json`,
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

Do not collect report visuals in a standalone image section.
Embed screenshots, diagrams, crops, diffs, heat maps, and artifact previews
throughout the report beside the text they support. Err on the side of more
useful visuals rather than fewer when they make the report easier to understand.

Do not ingest large raw diagnostic files into the model. For physics work, use
SkullScope query output and report the query cost required by `AGENTS.md`.

## Completion Gate

Do not send a final successful response to the user after implementation commits
alone. The orchestrator is not finished until all required completion artifacts
exist.

Before the final successful response, verify this checklist:

1. The implementation work is committed and pushed on the item branch.
2. An independent verifier reviewed the completed worker handoff.
3. All blocking verifier findings were fixed, answered, or converted into an
   explicit blocked/failed terminal state.
4. The required validation gate passed, or the report states why validation was
   not required for documentation-only work.
5. `Agentic/Reports/<yyyy-mm-dd>/<item-id>/report.md` exists.
6. The final report-only commit contains only `report.md` and referenced
   images under that report directory.
7. The report-only commit is pushed, unless pushing is explicitly blocked.
8. The queue item has a terminal state:
   - `done` for successful completed work with no PR or merge recorded,
   - `pr_open` when a PR exists and merge is not permitted,
   - `merged` when policy and repo rules permitted a merge and it succeeded,
   - `blocked`, `failed`, or `skipped` for non-successful terminal outcomes.
9. The final response names the terminal queue state and gives the GitHub
   report web URL. If a web URL cannot be produced, it must say why and provide
   the local report path.

If any checklist item is missing, continue the orchestration work instead of
finalizing. If progress is impossible, set the queue item to `blocked` or
`failed`, generate the corresponding report, and then finalize with that report.

## Branch Setup

1. Start from `policy.base_branch`, normally `main`, for the first independent
   roadmap item.
2. For chained roadmap items, use stacked child branches:
   - task 1 branch starts from `policy.base_branch`,
   - task 2 branch starts from task 1's branch,
   - task 3 branch starts from task 2's branch,
   - continue the chain in that pattern until the requested chain ends.
3. Record each item's parent branch in `run.json`, the generated
   `worker-prompt.md`, and any PR notes. For a queue entry, prefer an explicit
   parent field such as `parent_branch` or `stack_base_branch` when the item is
   part of a chain.
4. Fetch the required base or parent branch before creating the child branch.
5. Create or switch to the item's configured branch from that exact parent tip.
6. Do not rebase.
7. Do not force-push.
8. Do not push directly to `main`.

Stacked branch example:

```text
main
  \
   codex/task-1
       \
        codex/task-2
            \
             codex/task-3
```

If a parent branch changes after a child branch exists, update the child by
merging the parent branch into the child branch and pushing normally. Do not
rebase the child branch onto the parent, and do not rewrite already-pushed child
history.

For stacked PRs, open each child PR against its parent branch while the parent
is still unmerged. After the parent branch lands in `main`, retarget the child
PR to `main` or merge the updated `main` into the child branch, whichever keeps
the diff clear without rewriting history.

## Worker Delegation

Generate `worker-prompt.md` from
`Agentic/Orchestrator/templates/worker-prompt.md`.

Spawn exactly one worker for the selected item. Use
`tools\orchestrator.bat run-worker <item-id>` when Codex CLI automation is
available, or render the prompt with `tools\orchestrator.bat worker-prompt
<item-id>` for a manual worker handoff. The worker prompt must include:

- source plan path,
- impact area,
- branch name,
- validation gate and notes,
- screenshot and artifact commands,
- explicit write ownership,
- current merge restrictions.

The orchestrator may begin another roadmap item while a worker is active only
when `tools\orchestrator.bat next` or `start` accepts the item under the
parallel capacity and conflict rules. Parallel writer agents should use
isolated git worktrees; final integration remains orchestrator-owned.

For the executable path, use `tools\orchestrator.bat run-loop [item-id]`.
That command starts the selected queue item, runs the worker through `codex
exec`, advances `worker-result.json` into the state machine, runs verifier
rounds until `accepted`, runs the configured validation gate unless
`--skip-validation` is supplied, and can call `finalize` with `--finalize`.

## Review, Evidence, And Validation

After each worker completion, including a worker response to verifier feedback:

1. Save the first worker final message in `worker-result.json` when `codex exec`
   uses a JSON schema, or `worker-result.md` for manual fallback. Save later
   worker responses under the matching `verification-rounds/` file.
2. Review `git status --short`.
3. Review `git diff --stat` and the changed files.
4. Reject unrelated edits unless they are necessary and explained.
5. Select the smallest required PR gate from the queue entry and `AGENTS.md`.
6. Run the selected gate once the item is ready for verifier review. For
   documentation-only changes, state that no repository validation is required.
7. Preserve validation output in `validation.log` when validation is run.
8. Capture declared screenshots/artifacts or state why none are needed.
9. Record substantial timings, including validation, builds, launches, artifact
   generation, verifier rounds, and long investigations.

Repository validation scripts are PR/commit gates. Do not run them repeatedly
during iteration. In a verifier feedback loop, rerun a validation gate only when
the worker changed files or the prior evidence is no longer trustworthy.

## Verification Loop

After the review/evidence step, hand the work to a separate verifier agent
before marking the item successful. The verifier is a rubber-duck reviewer: it
checks the requested outcome, the source plan, the diff, validation evidence,
artifacts, commenting standards, and worker handoff.

Generate each verifier prompt from
`Agentic/Orchestrator/templates/verifier-prompt.md` and save it under
`verification-rounds/`.

The Codex verifier runs with the sandbox configured in `policy.json`. On this
Windows Codex CLI setup, the repository default is `danger-full-access` because
`workspace-write` fails during shell spawn setup. The orchestrator compares
tracked worktree status before and after verifier execution. Any
verifier-created tracked edit blocks success until inspected and handled
intentionally.

For each verification round:

1. Save the verifier prompt as
   `verification-rounds/round-XX-verifier-prompt.md`.
2. Spawn a verifier agent with that prompt. If sub-agent tooling is unavailable,
   stop and ask the user whether a same-agent verification fallback is
   acceptable; do not claim independent verification without a separate agent.
3. Save the verifier response as
   `verification-rounds/round-XX-verifier-result.md`.
4. If the verifier verdict is `accepted`, continue to validation and reporting.
5. If the verifier verdict is `needs-fixes`, send the blocking findings back to
   the implementation worker and save the worker response as
   `verification-rounds/round-XX-worker-response.md`.
6. Repeat the worker/verifier loop until a verifier returns `accepted`, or the
   item becomes blocked or failed.
7. If the verifier verdict is `blocked`, or if the worker cannot resolve a
   blocking finding, set the item to `blocked` or `failed` and generate the
   corresponding report.

The verifier must distinguish blocking findings from non-blocking suggestions.
Only blocking findings prevent successful completion. Non-blocking suggestions
may be recorded as residual risk or follow-up work.

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

Store generated files under the run directory. Copy selected PNG or JPG report
images into the report directory's `images/` folder.

Prefer PNG or JPG for committed report images. If the runtime produces BMP
captures, convert selected captures to PNG before embedding them in `report.md`.
Commit only the report Markdown and referenced images in the final report-only
commit. Keep bulky raw validation outputs and intermediate artifacts out of that
commit.

## PR Handling

If `pull_requests.allow_creation` is true and the branch is ready:

1. Commit the work with useful commit notes.
2. Push the feature branch.
3. Open or update the PR through the GitHub app or `gh` fallback when a PR is
   part of the requested workflow.
4. Save PR metadata in `pr.md` under the run directory when a PR exists.
5. For a successful item, archive the source plan as described in
   [Plan Archive](#plan-archive).
6. Set the queue state to `pr_open` once the PR is open, or to `done` if the
   successful item is complete without recording a PR.
7. Commit any source-plan archive and queue updates before the report
   commit. These are task-state changes, not report files.
8. Push the queue-state commit.
9. Generate `Agentic/Reports/<yyyy-mm-dd>/<item-id>/report.md` and copy only
   referenced PNG/JPG images into `Agentic/Reports/<yyyy-mm-dd>/<item-id>/images/`.
10. Commit the report directory as the final report-only commit. This commit
   must contain only `report.md` and images referenced by that Markdown file.
11. Push the report-only commit.
12. Post the generated report, including the report web URL, as a PR comment
    when the configured channel is available.

Use `tools\orchestrator.bat finalize <item-id> --commit` to move a reporting
item to `done`, archive its source plan, commit queue/archive state, draft the
report, and make the final report-only commit. The command refuses automated
commits on `main` unless `--allow-main-commit` is explicitly supplied.

If `pull_requests.allow_creation` is false, successful items still run
[Plan Archive](#plan-archive), set the queue state to `done`, commit and push
the source-plan archive plus queue-state update, then create and push the final
report-only commit. The orchestrator should still provide a report web link
unless policy or the user forbids pushing; if a push is forbidden or fails,
state that no report web link could be produced and provide the local report
path.

## Merge Handling

Do not merge.

Future merge automation requires both:

- an explicit `AGENTS.md` policy update, and
- `policy.json` with `merge.allow: true`.

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

- `done`,
- `pr_open`,
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
- queue state and queue-state commit SHA,
- a report web URL that opens the committed Markdown file in GitHub. Use the
  feature-branch URL for PR-open work and the `main` URL after a successful
  merge,
- started, finished, elapsed, and substantial sub-run timings,
- a short progress timeline,
- validation command and output summary,
- screenshot and artifact paths,
- embedded relative image links throughout the relevant report sections, using
  screenshots, focused zoom crops, heat maps, image diffs, before/after
  architectural diagrams, or artifact previews wherever they clarify the report,
- short interesting code snippets with file paths,
- merge status,
- conflicts and resolutions,
- residual risk,
- sub-agent result summary and `worker-result.json` or `worker-result.md` path,
- verifier result summary and `verification-rounds/` paths,
- next queue action.

The final response must include the report web URL. If the orchestrator cannot
produce a web URL because pushing is blocked or credentials are unavailable, it
must say so explicitly and provide the local report path instead.

## Plan Archive

When an item reaches a successful terminal state, move its source plan out of
the active plan folder so future agents do not confuse completed plans for
runnable work.

Successful terminal states are:

- `done`, when implementation and required validation are complete, no PR or
  merge is being recorded, and the report-only commit is pushed,
- `pr_open`, when the implementation and required PR gate passed and the
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

Set the item state:

- `done` when the implementation is complete, validation passed or was not
  required, the report-only commit is pushed, and no PR or merge is being
  recorded,
- `pr_open` when a PR exists and merge is not permitted,
- `merged` only when policy and repo rules permitted a merge and it succeeded,
- `blocked` when user input or external state is required,
- `failed` when implementation or validation invalidated the approach,
- `skipped` only by explicit user or policy decision.

Stop after `blocked` or `failed` unless policy explicitly allows advancement.
