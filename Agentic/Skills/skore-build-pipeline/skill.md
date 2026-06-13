---
name: skore-build-pipeline
description: Verify SkullbonezCore changes for a PR gate, update required artifacts, and prepare feature-branch commits/pushes without extra permission.
---

# skore-build-pipeline

Use after code changes when the user wants the repository verified, prepared for
commit, or readied for PR. Documentation-only changes do not require validation.

## Scope

Validation scripts are pre-commit/PR gates, not normal iteration steps. Choose
the narrowest validation that matches the touched files. If the PR-bound scope
is truly unsure, run the full pipeline.

| Change | Command |
|--------|---------|
| Documentation only | No validation required |
| Small non-render code refactor | `tools\validate_fast.bat` |
| Renderer backend, shaders, screenshots, visual baselines | `tools\validate_renderers.bat` |
| Physics, collision, solver, determinism | `tools\validate_physics.bat` |
| Hot path, allocation-sensitive, performance work | `tools\validate_perf.bat` |
| Broad or uncertain scope | `tools\validate_full.bat` |

## Artifact Helpers

When renderer captures or perf JSONs should become the new committed baselines:

```bat
tools\update_baselines.bat --visuals --require
tools\update_baselines.bat --perf --require
```

Do not use `tools\update_baselines.bat` for physics CSV or SkullScope baselines.
For intentional physics baseline changes, copy the final Debug artifact into
`TestOutput\baselines`, then rerun `tools\validate_physics.bat` after the copy so
the committed baseline is proven byte-exact.

When current Profile artifacts should be archived under `TestOutput\NNN_<commit>`:

```bat
tools\archive_validation_artifacts.bat --require
```

Both helpers support `--help`.

## Commit Prep

Before committing:
1. Run the selected validation and keep the command output, unless the change is documentation-only.
2. Update baselines or archives only when the output change is intentional.
3. Update `Agentic\SessionState.md` with current branch, latest commit context, active notes, and known bugs.
4. Draft detailed commit notes following `AGENTS.md`: concise subject, explanatory body, validation command and result, and any baseline/artifact/session-state updates.
5. On feature branches, commit and push when ready without waiting for confirmation.
6. On `main`, show the proposed commit notes and changed-file summary first, then commit or push only if the user explicitly confirms.

Do not merge or submit PRs, force-push, rebase, or rewrite history.

## Reporting

Report:
- Validation command run.
- For documentation-only changes, state that no validation was required.
- Important pass/fail output.
- Artifact updates, if any.
- Files changed.
- Any tests not run and why.

If validation fails, stop at the failure, summarize the relevant output, and fix the issue before continuing unless the user redirects.
