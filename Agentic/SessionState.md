# SkullbonezCore Session State

Date: 2026-07-12

Keep this file operational and short. Detailed evidence belongs in plans,
reports, and git history. `Agentic/Plans/MASTER-PLAN.md` is the authoritative
plan inventory.

## Current State

| Field | Value |
|---|---|
| Branch | `nightrunner-12th-july` |
| Current pushed baseline | Determinism-contract-hardening is pushed at `1a0ad165`; upload-arena-overflow-policy is commit-ready on top |
| Current objective | Validation-gate V3 is externally blocked; the active local lane advances to frame-view-calling-convention |
| Portfolio progress | 288 / 298 tasks = 97% rounded overall |
| Last broad local gate | `tools\\validate_full.bat` passed final aggregate source on 2026-07-12 in 88.84s: formatting/metadata clean, every CPU lane passed (174 doctest cases, 3,966 assertions), zero-warning Profile/Debug builds, zero DX12 InfoQueue errors with matching screenshots, and the 44,401-line varied physics baseline byte-exactly |
| Validation for current edits | Upload-arena closure passed independent review, all CPU tests, three DX12 runs, perf without regressions, one-minute stress at 0 flushes/drops, and the broad gate. |

## Live Queue

1. Validation-gate V3 is externally blocked at 5/6. The hosted CPU
   PR lane has a successful real run. Remaining work is a `merge_group` proof,
   required `main` branch protection, and trusted DX12-runner administration.
   Persistent self-hosted DX12 stays trusted-main/manual only; public-PR GPU
   evidence needs an ephemeral isolated runner.
2. 2026-07-12 adversarial-review remediation (MASTER section of the same
   name). Descriptor/handle lifetime, determinism-contract-hardening, and
   upload-arena-overflow-policy are complete. Next: frame-view-calling-convention, then
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

Commit and push upload-arena-overflow-policy, then use
`Agentic/Plans/MASTER-PLAN.md` to execute frame-view-calling-convention. V3
resumes when the required GitHub and runner authority exists.
