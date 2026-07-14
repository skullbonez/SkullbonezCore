# SkullbonezCore Session State

Date: 2026-07-14

Keep this file operational and short. Detailed evidence belongs in plans,
reports, and git history. `Agentic/Plans/MASTER-PLAN.md` is the authoritative
plan inventory.

## Current State

| Field | Value |
|---|---|
| Branch | `nightrunner-13th-july` |
| Current baseline | Replay visual-fidelity V0-V6 are complete: one prediction generation, one 2,401-tick presented cascade, CPU-only durable reconstruction, and 187/200 bricks grounded and sleeping through the final second |
| Current objective | Reclose replay M3-M7 ownership findings from mandatory M8 review, with the unchanged frame-exact 200-box gate after every task |
| Portfolio progress | 322 / 336 tasks = 96% rounded overall |
| Last broad local gate | `tools\\validate_full.bat` passed on 2026-07-14: mandatory CPU lanes, zero-warning Profile/Debug builds, DX12 screenshots with zero InfoQueue errors, standalone physics, and the 44,401-line byte-exact varied baseline all passed |
| Validation for current edits | M3-M7 remain reopened by M8 review. The eleventh remediation checkpoint moved prediction scheduling out of render traversal into the post-physics frame update and removed config/world/worker/timing authority from the overlay context and offline presentation probes. After two compile-only signature cleanups, the exact one-process 200-box oracle passed (2,401 ticks, 200 moved, 187 strict topples, 199 causal nodes, one generation, one presentation, all controls), and the full CPU/Profile/Debug/DX12/physics gate passed with zero warnings/DX12 errors and byte-exact physics. No baseline changed. |

## Live Queue

1. `replay-visual-fidelity-mega-probe` is complete at 7/7 on
   `nightrunner-13th-july`. It uses one generation and one presented reveal,
   then only non-presenting CPU/artifact verification.
   `Toppled` now means more than half the wall is directly grounded and sleeping;
   the approved base has 187/200 through the final second. Independent V6 review
   found no blocking issue.
2. `replay-monolith-decomposition` is active at 3/9 after the mandatory M8
   ownership review reopened M3-M7. M0 certified the exact
   starting tree; M1 bound all 55 current header type definitions/aliases and
   212 free functions to named owners; M2 mechanically split the six owner/value
   headers; M3 extracted the concrete presentation state, query, packet, and
   diagnostic owner; M4 extracted timeline retention and scrubber cursor/
   restore authority; M5 extracted authoring and its queued prediction-refresh
   value boundary; M6 extracted private prediction ownership and its
   never-stored published view while leaving mixed drawing statics for M8; M7
   published the HUD value seam, removed replay ownership from the late UI
   pass, and moved Run/query/probe behavior behind replay operations. The
   historical two-consumer grep premise was stale. M8 review then confirmed
   that mutable owner/root access, external frame-view reach-back, probe
   authority, and the mixed prediction/presentation TU are blocking closure.
   Every M0-M8 task,
   including inventory documentation, must run the
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
7. Future-path presentation lane is registered (2026-07-14 owner request):
   `Plans/TODO/future-path-vector-splines.md` (7 tasks — near-black prediction
   sky, thin anti-aliased vector-spline trajectory ribbons, comma-cycled color
   modes with UI reflection, selected-object-only glow, and deletion of the
   temporary authoring-look cycler). Independent presentation-only lane; its
   restyle changes presented prediction frames, so any refresh of the 200-box
   visual-fidelity golden manifest requires explicit owner approval and must
   be sequenced against the decomposition lane's unchanged-golden rule.

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
`Plans/TODO/replay-monolith-decomposition.md` reopened M3-M7. Remove mutable
state and forwarding access through `ReplayRuntime`, physically separate the
now frame-updated prediction scheduler from presentation drawing, remove the
replay root from external frame views, and make probes consume published views/
owner commands. Reclose each task only after its unchanged one-presentation
200-box oracle, then repeat M8's independent ownership review.
