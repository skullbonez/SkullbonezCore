# Overnight Run Protocol

Date: 2026-07-06
Status: Active — governs unattended implementation of the authoritative plan set
Applies to: `authoritative-plan-01` through `authoritative-plan-05` CSV rows
Validation for this documentation-only change: none required

This protocol defines how an unattended agent behaves while implementing rows
from the authoritative CSVs. The plan documents say *what* to do; this file
says how to run, commit, fail, and stop. It supplements — never overrides —
`AGENTS.md` and the orchestrator skill, which remains the mandated
implementation path for plan work.

## Scope rule (read first)

**Tonight's workqueue is the five `authoritative-plan-0*.csv` files. Nothing
else.** The files under `In_Progress/Inventories/` (the carmack 579-hit
checklist and the physics-standalone checklist/workqueue) are *inventories
that feed plans 02 and 03* — reference material, not queues. Do not open them
as work sources. The closed physics-standalone queue (165/165 done) stays
closed.

## One plan per night

Exactly one plan owns the night. Plans 01, 03, and 04 all touch
`Run`/`RunFrame`/render-host bindings; interleaving them produces
self-conflicts. Row clusters within the chosen plan are fine; rows from a
second plan are not, with one exception: FILL rows (below) may be taken from
this file when the primary plan is blocked.

On-ramp ownership note: the `LauncherLaser`/editor-tracer/`UiTextPass` overlay
cluster belongs to **plan 04** (it is a render-pass dependency problem).
Plan 03's first-night slice is its checker ratchet plus diagnostics/UI
snapshot rows — see the amended plan-03 document.

## Pre-flight (mandatory, in order)

1. `git status --short --branch` — worktree must be clean apart from known
   user-owned files. If unexpected dirty source exists, stop and report; do
   not adopt or revert it.
2. Read `Agentic/SessionState.md`; update it with the chosen plan and start
   time so concurrent sessions can see the night is claimed.
3. Build Debug and Profile (`tools\validate_build.bat` both).
4. Run `tools\validate_full.bat` once to establish the green baseline. If it
   is not green, the night's job becomes "report why" — do not start slices
   on a red baseline.

## Slice loop

For each row cluster (1–4 tightly related rows):

1. **Guardrail first.** The plan's checker/ratchet slice must land before any
   behavioral row. Every ratchet stores the current census as an explicit
   budget number (Gfx/Cfg/Instance counts, `GameModelCollection` authority
   hits, `m_host.m_` accesses, aggregate `IRenderBackend&` dependencies) and
   ships a self-test. A ratchet without a baseline number is lint, not a
   ratchet.
2. Implement the cluster per its rows. Respect the comment quality gate on
   every touched source file.
3. Run the cluster's validation gate (the strictest gate named by any row in
   the cluster). Extra evidence requirements:
   - Rows touching `Runtime/Replay/*` interaction or overlays: also run the
     tracked interaction proofs (`memory_overlay_f6_toggle`,
     `replay_branch_restore_live_edge`,
     `prediction_ragdoll_wall_200_predict`).
   - Barrier- or graph-execution-adjacent rows (plan 05): run
     `tools\validate_dx12_renderer.bat` three consecutive times per the
     danger-zone table.
4. **Commit per completed cluster**, including the CSV `status` update in the
   same commit, following the repository commit-note standard (validation
   command + result named explicitly). Push after every commit. A crash then
   loses at most one cluster.
5. Update the CSV row `status` to `done` (or `blocked`, below) — the CSV is
   the ledger; the commit is the evidence.

## Failure rule

- Two failed attempts at the same cluster's gate → revert the slice cleanly
  (the per-cluster commit discipline makes this a `git revert` or a clean
  checkout of uncommitted work), mark the row(s) `blocked` with a one-line
  reason in the CSV, commit the ledger update, and move to the next cluster.
- Never fix-forward into baselines: physics CSVs, SkullScope baselines, and
  visual baselines are not updated to make a slice pass. A slice that needs a
  baseline change is `blocked` by definition overnight.
- Never leave the worktree dirty across cluster boundaries.
- Three consecutive `blocked` clusters → stop the night early and write the
  post-run report; something systemic is wrong.

## The `overnight` column

Every row in the five CSVs carries an `overnight` value:

- `safe` — suitable for unattended work once the plan's guardrail slice has
  landed. Mechanical authority moves whose failure modes the named gate
  actually detects (screenshot diff, byte-exact CSV, checker, proofs).
- `defer` — requires a human-awake session or an explicit user instruction
  naming the row. These are rows where the gate cannot see the failure mode
  (interactive input/camera semantics, window resize), where determinism or
  restore paths are on the line (replay solver restore, scene load, timestep
  policy, creation pipeline), or where the row is a broad umbrella/endgame
  slice (EngineContext split, backend family split, barrier execution moves).

An unattended run takes `safe` rows only. `defer` rows are night-2+ material,
after the ratchets and the first wave have proven the loop on that plan.

## FILL queue (zero-risk fillers when blocked)

Any of these may be taken regardless of the chosen plan; each is one commit:

| id | Work | Evidence |
|----|------|----------|
| FILL-001 | Add `Agentic/Temp/` to `.gitignore`; `git rm -r --cached` the tracked temp tree (tip only — **no history rewrite**). See `fable_plans/04` phase 1. | `git status` clean of temp artifacts; commit body lists untracked paths |
| FILL-002 | Add a `throw`-site census ratchet to `tools/check_runtime_boundaries.py` with stored budget (355 as of 2026-07-06) and self-test. See `fable_plans/05` phase 1. | `validate_fast`, then run the changed checker |
| FILL-003 | Bootstrap the doctest harness: vendored header, `SKULLBONEZ_TESTS` project, smoke test, `tools\validate_tests.bat`. See `fable_plans/01` phase 0. | `validate_fast` + new tests green |
| FILL-004 | Add a staged-file size check (>5 MB outside allowlisted data/baseline dirs) to the validation tooling, with self-test. See `fable_plans/04` phase 1. | `validate_fast`, then run the changed checker |

## Post-run (mandatory)

1. Final `tools\validate_full.bat` on the night's HEAD.
2. Update `Agentic/SessionState.md`: plan chosen, clusters done/blocked,
   final validation result.
3. Write a handoff report under `Agentic/Reports/<date>/`: rows completed
   (ids), rows blocked (ids + reasons), commits (hashes + subjects),
   validation evidence per cluster, ratchet budget numbers before/after, and
   wall-clock timings for builds/gates per the timing rule.
4. Leave the worktree clean and pushed.
