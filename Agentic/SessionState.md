# SkullbonezCore Session State

Date: 2026-07-10

Keep this file operational and short. Detailed history belongs in plans,
reports, and git history.

## Current State

| Field | Value |
|---|---|
| Branch | `engine-cleanup-10th-july` in `C:\SkullbonezCore` |
| Baseline | Branched from `origin/main` at `7dd6256c`; plan-quality reconciliation is the first branch slice |
| Current objective | Implement every live MASTER plan in dependency order, with accepted parallel lanes where files do not overlap |
| Current validation | Planning baseline is documentation-only; implementation gates begin with validation V0/V1 and DX12 D0 |
| Next implementation | `validation-gate-integrity.md` V0/V1 and `dx12-failure-propagation.md` D0 |

## Binding Decisions

- Physics owner: promote scene-lifetime physics ownership through
  `SceneController`/`PhysicsScene`; `Run` wires it but does not own physics
  business behavior.
- Interaction selection: Inspect and Editor share one stable selection identity;
  gesture and presentation state remain workspace-specific.

## External Blocker

- Runtime CI: a real Windows/DX12 runner is required before GPU validation can
  be a required automated check.

## Active Plan Index

`Agentic/Plans/MASTER-PLAN.md` is authoritative. Key coordinated programs:

- Validation/CI + DX12 failure propagation.
- Runtime shell + runtime UI + interaction state machine.
- Replay right-sizing + physics authority/stable identity.
- Render concrete-owner decomposition.
- Behavioral depth and stale-plan-reference cleanup.

## Operational Notes

- Protect user-owned dirty files and rerun `git status --short --branch` before
  edits or commits.
- Implementing a plan uses `Agentic/Skills/orchestrator/SKILL.md`; drafting or
  reconciling plans is normal documentation work.
- Repository validation scripts are pre-commit/PR gates, not routine iteration.
- Kill launched engine processes by PID only.
- Known product bug: water/back-face intersection remains mitigated, not fully
  solved; see `Agentic/Bugs.md` and git history before new water work.
