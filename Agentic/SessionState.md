# SkullbonezCore Session State

Date: 2026-07-11

Keep this file operational and short. Detailed evidence belongs in plans,
reports, and git history. `Agentic/Plans/MASTER-PLAN.md` is the authoritative
plan inventory.

## Current State

| Field | Value |
|---|---|
| Branch | `claude/directx-12-rendering-review-wolqlc`, tracking `origin/claude/directx-12-rendering-review-wolqlc` |
| Current pushed baseline | `e158123f docs: lock in the 2026-07-11 owner rulings — no SimulationController, no EntityId` |
| Current objective | Reconcile the plan control plane, preserve owner-priority prediction work, and hand off the next implementation sequence without overlapping ownership |
| Last broad local gate | `tools\validate_full.bat` passed the final engine-cleanup source in 96.5s: every CPU target, zero-warning Profile/Debug builds, zero DX12 InfoQueue errors with matching screenshots, handle smoke, and the 44,401-line varied physics baseline byte-exactly |
| Validation for current edits | Documentation-only; no repository validation required |

## Live Queue

1. `instant-prediction-velocity-chaos` is important owner-priority live work.
   It remains in `Plans/TODO/`; its execution checklist is at 1/50 preparation
   items and must not be paused, parked, moved to `WNF/`, or deprioritized
   without an explicit owner directive.
2. Renderer binding work has one fixed ownership sequence:
   `render-backend-decomposition` A2 establishes the concrete pipeline/root-
   signature owner, `shader-pipeline-modernization` P1-P3 modernizes and
   consolidates that contract, and `shadow-edge-quality` S1 extends it. Shader
   P0 inventory may run earlier because it is read-only.
3. Validation-gate V3 remains externally blocked at 5/6. The hosted CPU PR lane
   has a successful real run. Remaining work is a `merge_group` proof, required
   `main` branch protection, and trusted DX12-runner administration. Persistent
   self-hosted DX12 stays trusted-main/manual only; public-PR GPU evidence needs
   an ephemeral isolated runner.
4. The data-format plan generalizes the shipped scene policy to assets, hulls,
   and configuration. Scene documents retain required `version` v2 and their
   deterministic v1 upgrade; no scene v0 reinterpretation or competing
   migration is planned.
5. Complete the engine-cleanup aggregate closure review. Completed plans marked
   as temporary closure evidence remain only until that gate passes, then are
   deleted so git history resumes as the archive.

## Current Plan Decisions

- `Plans/TODO/` contains live implementation work.
- `Plans/WNF/` contains only owner-parked “will not do now” work and is ignored
  unless the owner explicitly restores a plan to `TODO/`.
- `instant-prediction-velocity-chaos` is explicitly live and is not a WNF plan.
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
