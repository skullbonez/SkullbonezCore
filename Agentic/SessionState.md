# SkullbonezCore Session State

Date: 2026-07-11

Keep this file operational and short. Detailed evidence belongs in plans,
reports, and git history. `Agentic/Plans/MASTER-PLAN.md` is the authoritative
plan inventory.

## Current State

| Field | Value |
|---|---|
| Branch | `nightrunner-11th-july` |
| Current pushed baseline | `68b09031 instant-prediction-velocity-chaos, TASK 5 / 52, 62% OVERALL COMPLETE — fix selected-ball path rebuild regression` |
| Current objective | Close `entity-model-endgame`, while preparing the engine-cleanup aggregate ownership review in parallel |
| Portfolio progress | 217 / 276 tasks = 79% overall |
| Last broad local gate | `tools\validate_full.bat` passed final instant-prediction source on 2026-07-11 in about 107s: every CPU target, 133/133 doctest cases with 2,827 assertions, zero-warning Profile/Debug builds, zero DX12 InfoQueue errors with matching screenshots, handle smoke, and the 44,401-line varied physics baseline byte-exactly |
| Validation for current edits | Instant prediction passed `validate_fast`, `validate_tests`, `validate_full`, `validate_perf`, the chaos drag probe, and forced Instant-vs-Amortized fingerprint comparison. |

## Live Queue

1. Prepare the engine-cleanup aggregate review in parallel with the critical path;
   fix findings, pass the closure gate, and delete all eight retained completed
   plans.
2. Close `entity-model-endgame` before dependent features
   retain obsolete `GameModel`/`GameModelCollection` identities or APIs.
3. Continue the binding path recorded in MASTER: render A1-A2, DX12 cleanup,
   config decomposition, shader P0-P5, visibility, shadows, interpolation,
   editor undo/redo, then data-format versioning. Phase-level preparation and
   dependency barriers are authoritative in MASTER and the owning plans.
4. Validation-gate V3 remains an external parallel lane at 5/6. The hosted CPU PR lane
   has a successful real run. Remaining work is a `merge_group` proof, required
   `main` branch protection, and trusted DX12-runner administration. Persistent
   self-hosted DX12 stays trusted-main/manual only; public-PR GPU evidence needs
   an ephemeral isolated runner.

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

## Next Handoff

Use `Agentic/Plans/MASTER-PLAN.md` for selection and the repo-local orchestrator
skill when implementing a plan. Do not infer that owner-priority prediction work
is paused merely because another implementation slice runs first.
