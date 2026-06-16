---
name: skore-roadmap-orchestrator
description: Kick off and run the SkullbonezCore roadmap orchestrator generically for one or more queued roadmap plans, including branch setup, worker delegation, independent verifier feedback loops, validation gates, committed visual-rich run reports, PR handling, and policy-gated merge handling.
---

# skore-roadmap-orchestrator

Use this skill when the user asks to start, continue, or configure the
SkullbonezCore roadmap orchestrator instead of pasting a one-off prompt.

## Required Reads

Read these first, in order:

1. `AGENTS.md`
2. `README.md`
3. `Agentic/README.md`
4. `Agentic/SessionState.md`
5. `Agentic/Orchestrator/README.md`
6. `Agentic/Orchestrator/runbook.md`
7. `Agentic/Orchestrator/policy.json`
8. `Agentic/Orchestrator/queue.json`

Then read only the selected queue item plan files and task-relevant skills.

## Inputs

Accept any of these user inputs:

- explicit plan paths,
- explicit queue item ids,
- "next ready item",
- "run N items",
- a stop condition such as "PR only" or "merge if green".

If the user provides explicit plan paths that are not in `queue.json`, create
temporary run entries from the paths, using hyphen-case task ids derived from
the file names.

## Safety Contract

- Run one roadmap item at a time.
- After the implementation worker claims completion, hand the work to a
  separate verifier agent. Feed blocking verifier findings back to the worker
  and repeat until the verifier accepts the result or the item becomes blocked
  or failed.
- Do not rebase, force-push, rewrite git history, or push directly to `main`.
- Respect the current `AGENTS.md` and `policy.json` even if an old kickoff
  prompt claimed broader authority.
- Do not create PRs unless repo policy and the current user request allow it.
- Do not merge unless all of these are true:
  - the current user request explicitly allows this run to merge,
  - `AGENTS.md` permits the merge path,
  - `policy.json` has `enabled: true` and `allow_merge: true`,
  - required validation/checks are green,
  - the final report-only commit exists on the feature branch,
  - the merge method matches policy.
- Stop instead of guessing when a merge conflict, missing credential, failing
  validation, or policy mismatch would make the next action unsafe.

## Generic Kickoff

Use this prompt shape when spawning or resuming an orchestrator:

```text
You are the SkullbonezCore roadmap orchestrator.

Repository: <repo-root>

Use Agentic/Skills/skore-roadmap-orchestrator/skill.md.

Run scope:
- Queue source: <Agentic/Orchestrator/queue.json or explicit plans>
- Items: <next ready item | item ids | plan paths | N items>
- PR mode: <none | create/update PR when policy allows>
- Merge mode: <do not merge | merge only when policy, checks, and user permission allow>

Run the orchestrator loop sequentially, one roadmap item at a time.
After each implementation worker claims completion, run an independent verifier
agent. Send blocking verifier findings back to the worker and repeat until the
verifier accepts the work or the item becomes blocked or failed.
Produce a final report-only commit under Agentic/Reports for each item before
any merge. The report commit must contain only report.md and images referenced
by that Markdown file.
Stop at the first unsafe, blocked, failed, or policy-forbidden action.
```

## Per-Item Loop

For each item:

1. Start a wall-clock timer and create
   `Agentic/Runs/<yyyy-mm-dd>/<task-id>/`.
2. Save `run.json` with item id, source plan, branch, timestamps, selected
   validation gate, policy snapshot, and current git state.
3. Fetch the configured base branch and create or switch to the item branch.
4. Generate `worker-prompt.md` from
   `Agentic/Orchestrator/templates/worker-prompt.md`.
5. Spawn exactly one worker if sub-agent tools are available. If not available,
   perform the worker role in the current agent and state that no sub-agent was
   spawned.
6. Save the exact final worker response, interruption, or failure text to
   `worker-result.md`.
7. Review changed files, reject unrelated edits, and select the smallest
   required PR-gate validation from `AGENTS.md`.
8. Run validation only at PR/commit readiness, in a visible console window when
   feasible, and save output to `validation.log`.
9. Capture declared screenshots/artifacts. Prefer PNG or JPG copies for report
   embeds; convert BMP captures to PNG before embedding in reports. Useful
   report visuals include screenshots, focused zoom crops, heat maps, image
   diffs, artifact previews, and before/after architectural diagrams.
10. After the worker claims completion and current evidence is available,
   generate a verifier prompt from
   `Agentic/Orchestrator/templates/verifier-prompt.md` and save it under
   `verification-rounds/round-XX-verifier-prompt.md`.
11. Spawn a separate verifier agent to inspect the plan, diff, worker result,
   validation evidence, and artifacts without editing files. If no separate
   agent is available, stop and ask whether a same-agent verification fallback
   is acceptable.
12. Save verifier output under
   `verification-rounds/round-XX-verifier-result.md`. If there are blocking
   findings, send them back to the worker, save the worker response under
   `verification-rounds/round-XX-worker-response.md`, refresh review,
   validation, and artifacts as needed, then repeat with a new verifier round.
13. Continue only after the verifier returns `accepted`, or mark the item
    `blocked`/`failed` if the feedback loop cannot resolve blocking findings.
14. If the item reached a successful terminal state, archive the source plan:
    move `Agentic/Plans/<file>.md` to `Agentic/Plans/Done/<file>.md`, update
    the queue entry's `plan` path, and record both original and archived paths
    in `run.json` and `report.md`. Successful terminal states are `pr-open`,
    `merged`, or explicit user-declared completion. Do not archive blocked or
    failed plans unless the user explicitly asks for that.
15. Commit implementation changes, source-plan archive moves, queue updates,
    and other task-state changes before the report commit.
16. Push the branch and open/update the PR when permitted.
17. Generate a visual-rich report from
    `Agentic/Orchestrator/templates/report.md` under
    `Agentic/Reports/<yyyy-mm-dd>/<task-id>/report.md`. Copy only PNG/JPG
    images referenced by the Markdown file into
    `Agentic/Reports/<yyyy-mm-dd>/<task-id>/images/`.
18. Make the final feature-branch commit a report-only commit containing only
    the report Markdown and its referenced images. Do not include `Agentic/Runs`
    files, logs, queue updates, source-plan moves, source code, raw artifacts,
    or unreferenced images in this commit.
19. Push the report-only commit and compute the GitHub web URL for
    `report.md`.
20. Merge only after the final report-only commit is present on the PR and all
    merge gates pass.
21. Sync local `main` after a successful merge and move to the next item only
    when policy allows.

## Run And Report Folders

Use this local run-state shape:

```text
Agentic/Runs/<yyyy-mm-dd>/<task-id>/
  run.json
  worker-prompt.md
  worker-result.md
  verification-rounds/
  validation.log
  pr.md
  screenshots/
  artifacts/
```

Use this committed user-facing report shape:

```text
Agentic/Reports/<yyyy-mm-dd>/<task-id>/
  report.md
  images/
```

The report folder name must match the task id. The report-only commit must
contain just `report.md` and image files under `images/` that are referenced by
relative Markdown links from `report.md`.

## Report Expectations

`report.md` must be concise, visual-rich, and easy to scan. Include:

- first, a plain-language explanation of what was done for a non-engineer,
  before metadata, commits, validation, file lists, or implementation details,
- item id and source plan,
- archived source plan path for completed items,
- branch, implementation commit, report commit if known, PR link, report web
  URL, and merge SHA if known,
- started, finished, elapsed, and substantial sub-run timings,
- a short progress timeline,
- implementation summary,
- changed files by area,
- validation command and output summary,
- embedded relative image links throughout the relevant report sections for the
  best screenshots, focused zoom crops, heat maps, image diffs, before/after
  architectural diagrams, or artifact previews. Do not collect visuals in a
  standalone image section; err on the side of more useful images and diagrams
  rather than fewer,
- interesting code snippets with file paths and short excerpts,
- conflicts and how they were handled,
- residual risk,
- exact sub-agent result summary and `worker-result.md` path,
- verifier verdict, blocking findings resolved, non-blocking suggestions, and
  `verification-rounds/` paths,
- next queue action.

The report-only commit happens before merge, so merge status may be pending in
the committed report. Record the final report commit SHA, report web URL, and
merge SHA afterward in the final response and PR comment unless the user
explicitly asks for a separate post-merge report update.

## Final Response

After the requested run scope finishes or stops, summarize:

- each item id,
- branch,
- PR link,
- implementation commit,
- report commit,
- report web URL,
- archived plan path when the source plan moved to `Agentic/Plans/Done`,
- merge SHA or merge blocker,
- validation result,
- report path,
- worker-result path,
- verifier result path and final verifier verdict,
- remaining blocked work,
- total elapsed wall-clock time.
