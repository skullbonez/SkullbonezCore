# SkullbonezCore Session State

Date: 2026-07-13

Keep this file operational and short. Detailed evidence belongs in plans,
reports, and git history. `Agentic/Plans/MASTER-PLAN.md` is the authoritative
plan inventory.

## Current State

| Field | Value |
|---|---|
| Branch | `nightrunner-13th-july` |
| Current baseline | Replay prediction fidelity is complete: engine restore is byte-exact and the scrub gate compares 120 predicted/live solver hashes |
| Current objective | Execute `Plans/TODO/replay-monolith-decomposition.md` from M2 through M8; validation-gate V3 remains externally blocked |
| Portfolio progress | 319 / 327 tasks = 98% rounded overall |
| Last broad local gate | `tools\\validate_full.bat` passed on 2026-07-13 in 114.5s: mandatory CPU lanes, zero-warning Profile/Debug builds, DX12 screenshots/InfoQueue, standalone physics, and the 44,401-line byte-exact varied baseline all passed |
| Validation for current edits | Fidelity final: `validate_fast` passed; 180/180 unit cases and 4,665 assertions passed; all four replay scrub probes passed including 120 hash-equal ticks; physics matched 44,401 lines byte-exactly. |

## Live Queue

1. Validation-gate V3 is externally blocked at 5/6. The hosted CPU
   PR lane has a successful real run. Remaining work is a `merge_group` proof,
   required `main` branch protection, and trusted DX12-runner administration.
   Persistent self-hosted DX12 stays trusted-main/manual only; public-PR GPU
   evidence needs an ephemeral isolated runner.
2. Round-1 adversarial-review remediation is locally complete. The
   comment-rot sweep is owner-parked in `WNF/` (no comment changes yet,
   2026-07-12 ruling) and is not live portfolio work.
3. Round-2 runtime-contract remediation is locally complete and recorded in
   `Reports/2026-07-12/runtime-contract-enforcement-closure.md`.
4. Round-3 adversarial-review remediation is complete at 10/10. R10 added
   three-frame headroom and a backend-owned b1 bindless texture-index payload;
   closure evidence and the 29/29 touched-file comment audit live in
   `Reports/2026-07-13/adversarial-review-round-3-closure.md`. Out-of-scope
   rulings remain recorded in MASTER to avoid re-litigation.
5. Replay prediction fidelity is complete at 5/5 with evidence in
   `Reports/2026-07-13/replay-prediction-fidelity-probe-closure.md`. Replay
   monolith decomposition is now live at 2/9: M0/M1 record the prerequisite
   and bind all 84 header declarations plus 213 free functions to owners.
   M2 next shatters the everything-header. MASTER is at 319/327.

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

- Mandatory CPU validation PR runs 29148955729 and 29179364775 passed on
  2026-07-11 and 2026-07-12 respectively; no real `merge_group` run exists yet.
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
- Replay prediction fidelity closure evidence:
  `Agentic/Reports/2026-07-13/replay-prediction-fidelity-probe-closure.md`.

## Next Handoff

Continue `Plans/TODO/replay-monolith-decomposition.md` at M2. V3 resumes when
the required GitHub merge-group/branch-protection authority and trusted or
ephemeral DX12 runner administration exist.
