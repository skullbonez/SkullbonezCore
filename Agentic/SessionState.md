# SkullbonezCore Session State

Date: 2026-07-12

Keep this file operational and short. Detailed evidence belongs in plans,
reports, and git history. `Agentic/Plans/MASTER-PLAN.md` is the authoritative
plan inventory.

## Current State

| Field | Value |
|---|---|
| Branch | `nightrunner-11th-july` |
| Current pushed baseline | Aggregate engine-cleanup closure is validated at the current branch tip; the prior pushed baseline was `5649a876` |
| Current objective | Validation-gate V3 is externally blocked; the active local lane is the 2026-07-12 adversarial-review remediation portfolio (four must-do plans, starting with dx12-descriptor-and-handle-lifetime) |
| Portfolio progress | 275 / 298 tasks = 92% rounded overall |
| Last broad local gate | `tools\\validate_full.bat` passed final aggregate source on 2026-07-12: formatting/metadata clean, every CPU lane passed (173 doctest cases, 3,964 assertions), zero-warning Profile/Debug builds, zero DX12 InfoQueue errors with matching screenshots, and the 44,401-line varied physics baseline byte-exactly |
| Validation for current edits | Independent aggregate and narrow repeat reviews are clear; explicit DX12, one-minute graphics stress, perf, focused broadphase/capacity tests, comment audit, and full gate all pass. |

## Live Queue

1. Validation-gate V3 is externally blocked at 5/6. The hosted CPU
   PR lane has a successful real run. Remaining work is a `merge_group` proof,
   required `main` branch protection, and trusted DX12-runner administration.
   Persistent self-hosted DX12 stays trusted-main/manual only; public-PR GPU
   evidence needs an ephemeral isolated runner.
2. 2026-07-12 adversarial-review remediation (MASTER section of the same
   name). Must-do order: dx12-descriptor-and-handle-lifetime (latent
   resize/churn `SB_FATAL`), determinism-contract-hardening (isolated commit
   window), upload-arena-overflow-policy. Nice-to-have after the must-do
   lane: frame-view-calling-convention, then
   render-interface-and-workerpool-slimming. The comment-rot sweep is
   owner-parked in `WNF/` (no comment changes yet, 2026-07-12 ruling).

## Current Plan Decisions

- `Plans/TODO/` contains live implementation work.
- `Plans/WNF/` contains only owner-parked “will not do now” work and is ignored
  unless the owner explicitly restores a plan to `TODO/`.
- The MASTER critical path is binding; preparation may run early only where it
  is explicitly named, and no work crosses a recorded dependency barrier.
- Every plan-runner commit and plan-implementation prompt starts with the
  resolved MASTER progress header: plan name, completed plan tasks, and rounded
  overall portfolio completion. Ordinary commits do not claim plan progress.
- A completed plan may remain in the tip tree only when MASTER explicitly marks
  it as evidence for an unmet aggregate closure gate; it is deleted when that
  gate passes.
- Scene v1→v2 defines versioning semantics: integer per-format history,
  deterministic named upgrades, current/previous compatibility, recoverable
  future-version rejection, and writers stamping current. Encodings and
  version histories remain format-owned.
- `Run` remains process/frame composition only. The god-object closure rule in
  `AGENTS.md` applies across the full logical runtime surface.
- No `SimulationController` and no unified `EntityId`; `PhysicsSceneObjectId`
  remains the cross-system identity while subsystem handles remain hot-path
  currency.

## Current External Evidence

- Mandatory CPU validation PR run 29148955729 passed on 2026-07-11:
  `https://github.com/skullbonez/SkullbonezCore/actions/runs/29148955729`.
- DX12 runtime runs 29149260881 and 29149344794 were skipped while trusted
  runner activation remains disabled; they are not runtime evidence.
- `main` is currently unprotected.
- V3 activation details:
  `Agentic/Reports/validation_ci_v3_20260710.md`.
- Runtime-shell final ownership evidence:
  `Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md`.
- Physics authority closure evidence:
  `Agentic/Reports/2026-07-11/physics-authority-and-identity-closure-review.md`.
- Entity-model closure evidence:
  `Agentic/Reports/2026-07-11/entity-model-endgame-closure.md`.
- Render-backend closure evidence:
  `Agentic/Reports/2026-07-12/render-backend-decomposition-closure.md`.
- DX12 post-cleanup closure evidence:
  `Agentic/Reports/2026-07-12/dx12-post-final-cleanup-closure.md`.
- Engine-config closure evidence:
  `Agentic/Reports/2026-07-12/engine-config-decomposition-closure.md`.
- Shader-pipeline closure evidence:
  `Agentic/Reports/2026-07-12/shader-pipeline-modernization-closure.md`.
- Render-visibility closure evidence:
  `Agentic/Reports/2026-07-12/render-visibility-architecture-closure.md`.
- Shadow-quality closure evidence:
  `Agentic/Reports/2026-07-12/shadow-edge-quality-closure.md`.
- Simulation/render interpolation closure evidence:
  `Agentic/Reports/2026-07-12/sim-render-interpolation-closure.md`.

## Next Handoff

Use `Agentic/Plans/MASTER-PLAN.md` for selection. No viable local implementation
work remains; V3 resumes when the required GitHub and runner authority exists.
