# SkullbonezCore Session State

Date: 2026-07-14

Keep this file operational and short. Detailed evidence belongs in plans,
reports, and git history. `Agentic/Plans/MASTER-PLAN.md` is the authoritative
plan inventory.

## Current State

| Field | Value |
|---|---|
| Branch | `nightrunner-13th-july` |
| Current baseline | Replay visual-fidelity V2 is complete: the one-generation 2,401-tick visual golden is exact and all 2,401 predicted body packets match later live solver packets bit for bit |
| Current objective | Build the permanent frame-exact 200-box replay visual fidelity mega probe, then execute replay monolith decomposition with that gate after every task |
| Portfolio progress | 315 / 329 tasks = 96% rounded overall |
| Last broad local gate | `tools\\validate_full.bat` passed on 2026-07-14: mandatory CPU lanes, zero-warning Profile/Debug builds, DX12 screenshots with zero InfoQueue errors, standalone physics, and the 44,401-line byte-exact varied baseline all passed |
| Validation for current edits | V2: `tools\validate_replay_visual_fidelity.bat` passed in about 143s with one hidden engine process, 2,401 exact visual ticks, 199 causal nodes, 2,401 bit-exact predicted/live ticks, all 200 bricks moved, and five negative/incomplete controls rejected |

## Live Queue

1. `replay-visual-fidelity-mega-probe` is active at 3/7 on
   `nightrunner-13th-july`. V0 froze the known-good 200-box cascade, V1 made one
   typed packet the render/validation seam, and V2 proved one-generation causal
   plus predicted/live equality. V3 now round-trips durable presentation state
   through save, load, and scrub.
2. `replay-monolith-decomposition` is blocked at 0/9 until the mega probe
   closes. Every M0-M8 task, including inventory documentation, must run the
   unchanged 200-box gate before it can be checked or committed.
3. Validation-gate V3 is externally blocked at 5/6. The hosted CPU
   PR lane has a successful real run. Remaining work is a `merge_group` proof,
   required `main` branch protection, and trusted DX12-runner administration.
   Persistent self-hosted DX12 stays trusted-main/manual only; public-PR GPU
   evidence needs an ephemeral isolated runner.
4. Round-1 adversarial-review remediation is locally complete. The
   comment-rot sweep is owner-parked in `WNF/` (no comment changes yet,
   2026-07-12 ruling) and is not live portfolio work.
5. Round-2 runtime-contract remediation is locally complete and recorded in
   `Reports/2026-07-12/runtime-contract-enforcement-closure.md`.
6. Round-3 adversarial-review remediation is complete at 10/10. R10 added
   three-frame headroom and a backend-owned b1 bindless texture-index payload;
   closure evidence and the 29/29 touched-file comment audit live in
   `Reports/2026-07-13/adversarial-review-round-3-closure.md`. Out-of-scope
   rulings remain recorded in MASTER to avoid re-litigation.

## Current Plan Decisions

- `Plans/TODO/` contains live implementation work.
- `Plans/WNF/` contains only owner-parked “will not do now” work and is ignored
  unless the owner explicitly restores a plan to `TODO/`.
- The MASTER critical path is binding; preparation may run early only where it
  is explicitly named, and no work crosses a recorded dependency barrier.
- Both replay plans execute on `nightrunner-13th-july`. The visual mega probe
  closes first; every decomposition task then reruns its unchanged golden
  200-box manifest. Refactors do not authorize baseline refresh.
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

## Next Handoff

Run the repository-local orchestrator against
`Plans/TODO/replay-visual-fidelity-mega-probe.md` V3. Replay decomposition does
not begin until V0-V6 close with the permanent 200-box gate passing.
