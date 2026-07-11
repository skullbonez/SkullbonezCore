# Engine Cleanup Execution Guide

Date: 2026-07-10
Status: Active protocol
Owner: architecture cleanup

## Start Here

1. Follow the repository startup contract in `AGENTS.md`.
2. Read `Agentic/Plans/MASTER-PLAN.md` for authoritative priority, phase counts,
   dependencies, and blocking decisions.
3. Read only the selected plan and directly required references.
4. Implementing a plan uses `Agentic/Skills/orchestrator/SKILL.md`; drafting or
   reconciling a plan is normal documentation work.

This guide does not duplicate plan order or status. If it conflicts with the
master inventory, the master is authoritative and this guide must be repaired.

## Slice Protocol

1. **Select one phase.** Choose the first unblocked phase in master priority
   order. A partial phase remains unchecked.
2. **Confirm the owner and deletion/behavior proof.** Do not start if the phase
   only says "refactor" or "clean up" without naming what authority moves and
   how completion is proven.
3. **Protect the worktree.** Record `git status --short --branch`; preserve all
   pre-existing changes as user-owned.
4. **State impact and validation before editing.** Name source areas and the
   formal pre-commit/PR gate. Use targeted builds/tests/inspection during
   iteration only when they answer a specific question.
5. **Implement one coherent slice.** Avoid adjacent cleanup and broad formatting.
   A mechanical file split does not satisfy an ownership phase.
6. **Audit touched source comments.** Apply the repository comment quality gate
   and create a `git ls-files` checklist for subsystem/full comment passes.
7. **Run the required final gates.** Use final source/data/baselines. Until
   validation-gate V2 completes, broad/unsure work must run the temporary CPU
   suite sequence documented in `AGENTS.md` before `validate_full`.
8. **Record evidence.** Add exact commands, result, time, logs/artifacts,
   deletion proof, inventory reconciliation, and any deferred risk to the plan
   or handoff.
9. **Check the phase only after evidence passes.** Update its count in the master
   and the operational next step in SessionState in the same commit.
10. **Commit intentionally.** Feature branches may commit/push when ready;
    direct main commits require explicit owner confirmation. Commit bodies name
    what changed, why, important ownership details, validation, and artifacts.

## Hard Rules

- Never force-push, rebase, rewrite history, use destructive clean/reset, or
  discard user-owned work.
- Never claim validation from a plan note; require command output from the final
  source state.
- Physics determinism remains byte-exact. DX12 validation remains zero.
- Do not update a baseline merely to make a gate pass; explain and prove the
  intended behavior change first.
- Do not create a compatibility bridge, broad host bag, hot-path callback, or
  new inheritance seam to make an ownership checkbox appear complete.
- A blocking owner decision remains blocking. Record the alternatives and
  smallest decision needed; do not guess.

## Current Priority Summary

The master currently orders work as:

1. Validation CPU umbrella and DX12 failure inventory.
2. DX12 command-state/failure propagation.
3. Behavioral manifold/serializer/fault-injection gaps.
4. Runtime shell + UI + interaction ownership.
5. Replay right-sizing + physics ownership/identity.
6. Render concrete-owner split.
7. Documentation-only stale reference cleanup in parallel.
8. Shadow quality; fracture replay remains blocked.

Use the master for exact phase identifiers and counts.

## Plan Closure

A plan is done only when its Definition of Done and every phase checkbox pass,
all decisions resolve, required formal gates pass, current inventories reconcile,
and independent review is complete where required. Then update master and
SessionState and delete the plan/checklist; git history is the archive.

Documentation-only edits to this guide require no repository validation, but
links, plan inventory, Markdown whitespace, and `git diff --check` must pass.
