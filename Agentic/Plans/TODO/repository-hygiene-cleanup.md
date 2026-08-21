# Repository Hygiene Cleanup

Date: 2026-08-20
Status: Active; 1/6 phases complete. Bound after `VALIDATION_TIME_AUDIT`.
Impact areas: ignored validation evidence, build and IDE output, detached Git
worktrees, tracked visual references, plan topology, and repository hygiene
Owner: repository hygiene and artifact-retention owners
Priority: Third active queue item; RC0 follows VTA5
Commit name: `REPOSITORY_CLEANUP`

## Goal

Remove local and tracked material that no longer serves a live build,
validation, investigation, or documentation purpose without losing user work,
owner-controlled evidence, reproducibility, or Git history. The cleanup must
separate four different decisions that must never be collapsed into one broad
delete:

1. ignored derived output that can be regenerated;
2. local investigation evidence whose owner may still need it;
3. detached worktrees that must be clean and history-reachable before removal;
4. tracked files whose deletion changes the repository contract.

The plan is not authority to run `git clean`, delete by wildcard, refresh a
baseline, remove a dirty worktree, or erase a current plan's evidence. Every
destructive phase first records an explicit absolute-path manifest and a
post-refresh owner disposition.

## Registration And Ordering

- The master ledger binds RC0 after `VALIDATION_TIME_AUDIT` VTA5. The validation
  audit must finish first so cleanup does not remove timing or defect evidence
  while the gate topology is still under review.
- The six phases are portfolio tasks RC0 through RC5. Their progress is
  reported under commit token `REPOSITORY_CLEANUP`.
- `INVARIANT_HARDENING`, `VALIDATION_TIME_AUDIT`, the untracked manual-test
  checklist, and any worktree that changes while this plan waits remain
  user-owned. Rebase every measurement and disposition before RC0 is checked.
- Ignored-file deletion and worktree removal are machine-local operations.
  Their phase commits record the manifest, before/after byte totals, commands,
  and exclusions; they do not pretend that Git can archive deleted ignored
  files.

## Safety And Retention Policy

- Run `git status --short --branch` before every phase and again before every
  commit. Stop on an overlapping or newly dirty target.
- Resolve every deletion target to an absolute path and prove it is either
  beneath `C:\SkullbonezCore` or one of the exact registered worktree roots in
  RC2. Do not delete a computed, unresolved, root, home, or workspace path.
- Use an explicit reviewed manifest. Do not use `git clean -xfd`, recursive
  globs, an age predicate as the deletion command, or a generated path list
  passed between shells.
- Preserve `TestOutput/baselines/`, current active-plan evidence, unresolved
  owner-review candidates, the current `Profile` and `Automation` outputs, and
  any artifact named by `Agentic/SessionState.md` until its owner disposition
  changes.
- Treat screenshots, Look Lab output, editor preferences, Codex-owned
  worktrees, and dirty worktrees as user data. Exclude them unless the owner
  explicitly moves an exact path into the deletion manifest.
- Remove a worktree with Git's worktree command after clean/reachability proof;
  never recursively delete a registered worktree directory.
- A tracked visual-baseline deletion is an intentional baseline change even
  when no current tool consumes it. It requires owner approval and the mapped
  DX12 visual gate; no phase may refresh or replace a golden.
- If a source file or symbol is proposed for deletion, first build the object
  roots required by `tools/inventory_unreachable_symbols.py`, run its strict
  inventory, resolve callbacks/virtual/configured/test seams, and obtain an
  owner ruling. Lexical absence alone is not deletion proof.

## Audit Snapshot To Rebase In RC0

The 2026-08-20 read-only audit found the following local measurements. They are
a starting inventory, not a deletion allowance or a frozen size budget.

| Area | Measured size | Initial disposition |
|---|---:|---|
| `TestOutput/` total | 74.582 GiB | Mixed; preserve tracked baselines and current evidence |
| Pre-2026-08-10 immediate children of `TestOutput/`, excluding `baselines` | 52.588 GiB | Candidate historical output; replace the age query with an explicit RC1 manifest |
| Pre-2026-08-10 immediate children of `TestOutput/validation/` | 4.456 GiB | Candidate historical validation output; explicitly name each retained/deleted child |
| `Debug/*.physicsdiag.ndjson` and `Debug/*.physicsdiag.sqlite` | 14.438 GiB | Regenerable diagnostic traces; preserve any live investigation named at RC0 |
| `.vs/` | 3.930 GiB | Derived IDE cache; delete only while Visual Studio is closed |
| `Release/`, `Profile-WPO/`, and portable `build/` | 2.154 GiB | Old derived build output |
| Standalone `Agentic/Tests/*/x64/` output | 0.354 GiB | Old derived test output |
| `Agentic/Reports/` and `Agentic/Temp/` | 0.053 GiB | Logs/scratch; Reports contradicts the current repository convention |

The same audit found seven clean detached worktrees totaling about 22.07 GiB.
Each recorded HEAD was an ancestor of the then-current branch:

| Worktree | Measured size | Audit state |
|---|---:|---|
| `C:\SkullbonezCore-premerge-306b040-32c41301314b4a5e` | 0.45 GiB | Clean; history-reachable |
| `C:\SkullbonezCore-qn4-prechange` | 1.11 GiB | Clean; history-reachable |
| `C:\SkullbonezCore-replay-baseline` | 0.41 GiB | Clean; history-reachable |
| `C:\Temp\SkullbonezCore-contact-perf-parent` | 1.16 GiB | Clean; history-reachable |
| `C:\Temp\SkullbonezCore-sr0-exact` | 5.56 GiB | Clean; history-reachable |
| `C:\Temp\SkullbonezCore-sr0-pair` | 5.49 GiB | Clean; history-reachable |
| `C:\Temp\SkullbonezCore-sr0-pre` | 7.89 GiB | Clean; history-reachable |

Initial exclusions were `C:\SkullbonezCore_RS5_BASE` because it contained a
modified Physics file, `C:\Users\sesch\.codex\worktrees\0595\SkullbonezCore`
because it is Codex-owned, and the dirty orchestrator candidate under the user
temporary directory. RC2 must rediscover the current worktree set rather than
assuming these states remain true.

## Tracked Candidate Decision Table

RC0 rechecks every reference and records an explicit keep/delete/relocate
decision before RC4 changes the tree.

| Candidate | Audit evidence | Required owner decision |
|---|---|---|
| `TestOutput/baselines/baseline_water_ball_test.png` | No live consumer; history ends in the OpenGL-era renderer path; DX12 uses `baseline_dx12_water_ball_test.png` | Delete after intentional visual-baseline review, or record the current owner and consumer |
| Seven `TestOutput/baselines/shadow_*` images | About 25 MiB; deliberately created by the completed shadow-quality campaign, but no current gate or durable document consumes them | Wire them into a maintained gate/document, or delete them after visual review |
| `Agentic/Concepts.png` | Unreferenced 2.4 MiB rendering mood board | Keep as a named design reference, relocate into a durable documented home, or delete |
| `Agentic/Plans/DONE/imgui-tracy-e17-comment-audit.md` | Sole completed-plan exception; `MASTER-PLAN.md` already records the inconsistency | Owner chooses an explicit exception or deletion; reconcile the ledger in the same change |
| `Agentic/Plans/WNF/contact-stack-stability-techniques.md` report link | Points to a deleted `Agentic/Reports/2026-08-02/...` document that is absent from the repository and local report cache | Replace with durable commit/source evidence; do not recreate Reports |

The committed technical manual, its figures/source generator, live and parked
plan patches, submodules, README images, baked DXIL files, and tracked physics,
replay, query, and performance baselines were reviewed and are excluded unless
new evidence reopens them. Identical DXIL bytes under different shader names
are separate filename-addressed runtime assets, not duplicate-file proof.

## RC0 - Rebase Inventory And Obtain Dispositions

- [x] Record the current branch, HEAD, dirty files, registered worktrees, active
  processes that may own build/IDE output, and free disk space.
- [x] Recompute directory and file sizes without reading large diagnostics into
  model context. Inventory current plan/candidate evidence by exact path.
- [x] Recheck references, Git history, and current consumers for every tracked
  candidate. Record keep/delete/relocate and the responsible owner in the table.
- [x] Run the compiled reachability inventory only after its required object roots
  are current. Record that no source deletion is authorized if the inventory is
  stale or a reported symbol's invocation mechanism is unresolved.
- [x] Produce explicit RC1-RC3 absolute-path manifests and an exclusion manifest.
  The manifests, not an age cutoff or wildcard, become the deletion authority.

**RC0 acceptance:** every candidate has an owner disposition; every destructive
target is explicit and resolved; dirty/current evidence is excluded; no file is
deleted in this phase.

## RC1 - Retire Historical Test And Validation Output

- Delete only the approved historical `TestOutput` children from the RC0
  manifest. Keep `baselines`, current active-plan evidence, owner-review
  candidates, and named session-state artifacts.
- Remove obsolete `Agentic/Reports` logs and `Agentic/Temp` scratch after
  confirming no current process holds or names them. Do not recreate Reports.
- Record per-target bytes removed, failures/deferred paths, resulting free disk
  space, and `git status` proof that tracked files did not change.

**RC1 acceptance:** approved historical artifacts are gone, protected evidence
remains, and the tracked worktree is byte-for-byte unchanged.

## RC2 - Retire Clean Historical Worktrees

- Re-run `git worktree list --porcelain` and inspect every candidate with
  `git status --porcelain --ignore-submodules=all`.
- Prove each candidate HEAD remains reachable from a retained branch or tag and
  that no active Codex task, owner workflow, or process uses the path.
- Remove only clean approved worktrees through Git, then run `git worktree
  prune --dry-run` before any metadata pruning. Preserve every dirty, missing-
  proof, app-owned, or owner-retained worktree.
- Record worktree path, old HEAD, reachability proof, removal result, and bytes
  reclaimed.

**RC2 acceptance:** no dirty or app-owned worktree is removed, every removed
HEAD remains reachable, and Git reports no stale registration for an approved
removal.

## RC3 - Reclaim Derived Build, IDE, And Diagnostic Output

- Close Visual Studio before removing the approved `.vs` cache.
- Remove approved old `Release`, `Profile-WPO`, portable `build`, and standalone
  test `x64` outputs. Keep current `Profile`/`Automation` outputs unless RC0
  proves they are no longer needed.
- Remove only approved diagnostic trace/database files from `Debug`; retain the
  current executable/object roots needed by pending validation and reachability
  work.
- Remove explicit root probe objects and historical root build logs while
  preserving editor preferences and current launch arguments unless the owner
  opts in to their deletion.

**RC3 acceptance:** the approved derived output is removed, protected current
builds and preferences remain, and a clean incremental build is still possible
from tracked inputs.

## RC4 - Reconcile Tracked Artifacts And Documentation

- Apply the RC0 owner decisions to the old water baseline, shadow references,
  Concepts mood board, sole completed-plan exception, and stale parked-plan
  report link.
- When deleting the completed plan, remove its live-file ledger row and retain
  closure in Git history. When keeping it, document the narrow exception so it
  no longer contradicts the stated convention.
- Replace the deleted Reports link with a durable source, root document,
  `Agentic/Reference` path, `tools` path, or commit identity.
- Do not refresh, regenerate, or substitute a baseline. A deletion decision is
  reviewed on its own diff.

**RC4 acceptance:** every tracked candidate has an intentional disposition,
all surviving assets have a named consumer or durable purpose, all references
resolve, and plan topology matches the repository convention.

## RC5 - Prevent Recurrence And Close

- Re-run the local size, ignored-artifact, registered-worktree, tracked special-
  file, exact-duplicate, project-membership, related-path, and current compiled
  reachability inventories. Treat the outputs as current measurements, never
  count budgets.
- Add only the minimum durable retention guidance needed to keep generated
  output out of tracked/history-bearing locations. Do not add a broad deletion
  script or automatic cleanup that can erase investigation evidence.
- Obtain an independent review of the final deletion manifest, tracked diff,
  worktree evidence, protected exclusions, and validation selection. Any
  credible data-loss, baseline, stale-reference, or source-reachability finding
  reopens the owning phase.
- Update `MASTER-PLAN.md` and `Agentic/SessionState.md`, delete this completed
  plan, and retain the complete commands, byte totals, dispositions, and
  validation results in the closing commit body.

**RC5 acceptance:** the repository and machine-local checkout retain only
intentional artifacts, no protected/user-owned data was lost, no source
deletion lacks compiled evidence, and independent review has no blocker.

## Validation Map

| Change | Required evidence |
|---|---|
| RC0 inventory/documentation only | No repository validation; record commands and bounded output |
| Ignored local output deletion | No repository validation; before/after `git status`, explicit manifest, byte totals, and protected-path proof |
| Clean worktree removal | No repository validation; clean status, retained-history reachability, registered-worktree checks |
| Documentation-only reference or ledger repair | No repository validation |
| Visual baseline deletion | Intentional owner review plus `tools\validate_dx12_renderer.bat`; no baseline refresh |
| Any tool or project-file change introduced by RC5 | `tools\validate_fast.bat`, then the changed tool directly; add mapped focused gates |
| Source or externally declared symbol deletion, if separately approved | Current Debug/Profile/Automation builds, strict unreachable-symbol inventory, mapped focused tests/gates, and regression coverage |
| Terminal plan closure with tracked non-documentation changes | Independent review, cumulative focused gates, then `tools\agent_validate.bat --plan-completion`; preserve and report any inherited owner-controlled oracle stop without refreshing it |

## Plan Acceptance Criteria

- [ ] RC0-RC5 are complete with commands and evidence recorded.
- [ ] Every deletion came from an explicit reviewed absolute-path manifest.
- [ ] No dirty, active, app-owned, or history-unreachable worktree was removed.
- [ ] `TestOutput/baselines`, current plan evidence, unresolved candidates, and
      user preferences/captures were preserved unless an exact owner decision
      says otherwise.
- [ ] Every tracked candidate has a keep/delete/relocate disposition and every
      retained artifact has a durable purpose or consumer.
- [ ] No Reports tree or stale Reports reference remains.
- [ ] No source file or symbol was deleted without current compiled reachability
      evidence and an owner ruling.
- [ ] The final tracked diff passes its cumulative mapped validation.
- [ ] Independent review reports no blocker.
- [ ] `MASTER-PLAN.md` and `SessionState.md` are reconciled and this completed
      plan is deleted in the closing commit.
