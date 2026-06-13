---
name: skore-roadmap-orchestrator
description: Kick off and run the SkullbonezCore roadmap orchestrator generically for one or more queued roadmap plans, including branch setup, worker delegation, validation gates, committed phone-readable run reports, PR handling, and policy-gated merge handling.
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
- Do not rebase, force-push, rewrite git history, or push directly to `main`.
- Respect the current `AGENTS.md` and `policy.json` even if an old kickoff
  prompt claimed broader authority.
- Do not create PRs unless repo policy and the current user request allow it.
- Do not merge unless all of these are true:
  - the current user request explicitly allows this run to merge,
  - `AGENTS.md` permits the merge path,
  - `policy.json` has `enabled: true` and `allow_merge: true`,
  - required validation/checks are green,
  - the committed report bundle exists as the final feature-branch commit,
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
Produce a committed run evidence folder for each item before any merge.
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
9. Capture declared screenshots/artifacts. Prefer PNG or JPG copies for phone
   review; convert BMP captures to PNG before embedding in reports.
10. Generate a phone-readable `report.md` from
    `Agentic/Orchestrator/templates/report.md`.
11. Commit implementation changes first, then make the final pre-merge commit a
    task-named run evidence folder commit containing `report.md`, `run.json`,
    `worker-result.md`, selected images, and any small useful artifacts.
12. Push the branch and open/update the PR when permitted.
13. Merge only after the final evidence commit is present on the PR and all
    merge gates pass.
14. Sync local `main` after a successful merge and move to the next item only
    when policy allows.

## Run Evidence Folder

Use this required shape:

```text
Agentic/Runs/<yyyy-mm-dd>/<task-id>/
  run.json
  worker-prompt.md
  worker-result.md
  validation.log
  pr.md
  report.md
  screenshots/
  artifacts/
```

The folder name must match the task id. Keep the report and selected images in
that folder so the commit is easy to inspect on GitHub from a phone.

## Report Expectations

`report.md` must be concise, scrollable, and useful on a phone. Include:

- item id and source plan,
- branch, implementation commit, final evidence commit if known, PR link, and
  merge SHA if known,
- started, finished, elapsed, and substantial sub-run timings,
- a short progress timeline,
- implementation summary,
- changed files by area,
- validation command and output summary,
- embedded relative image links for the best screenshots or artifact previews,
- interesting code snippets with file paths and short excerpts,
- conflicts and how they were handled,
- residual risk,
- exact sub-agent result summary and `worker-result.md` path,
- next queue action.

The report bundle is inside the final evidence commit, so the committed report
may list the evidence commit as pending. The report bundle also happens before
merge, so merge status may be pending. Record the final evidence commit SHA and
merge SHA afterward in the final response and PR comment unless the user
explicitly asks for a separate post-merge report update.

## Final Response

After the requested run scope finishes or stops, summarize:

- each item id,
- branch,
- PR link,
- implementation commit,
- final evidence commit,
- merge SHA or merge blocker,
- validation result,
- report path,
- worker-result path,
- remaining blocked work,
- total elapsed wall-clock time.
