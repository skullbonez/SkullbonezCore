# MASTER PLAN

Date: 2026-08-17
Status: Two active plans; 11/24 tasks complete

## Owner Direction

- Causal Event Inspection is complete. Its synchronized exact-frame transport,
  dedicated camera follow, bounded manifold presentation, and four-row solver
  panel closed with focused host/policy tests, every ownership inventory green,
  and an independent ownership review finding no second retained transport,
  camera, pivot, input, or diagnostics owner.

- Physics-baseline mismatches in Determinism or Catto phases do not stop this
  orchestration run. Preserve every gate executable under a plan-and-phase name,
  record the mismatch and artifact path, and continue without refreshing a
  golden; the owner will review the candidate behaviors afterward.

- Determinism Envelope Tier-2 Hardening is active. It removes
  implementation-defined transcendentals from physics-visible paths, gates their
  return, adds a portable CPU test target, and adds the cross-machine byte
  comparison that would observe a tier-2 envelope violation. T3 and T4 change
  physics output bits and are blocked on an explicit owner baseline decision;
  every other task is unblocked. T0 proved the linked static UCRT selects FMA3
  or non-FMA `sinf`, `cosf`, and `acosf` bodies from processor features, making
  the tier-2 exposure live rather than merely latent.
- Source Modernization Sweep is complete. No further work remains.
- Dense Pile Sleep Resolution is complete by owner direction. No further work
  will be performed and no additional baseline or solver change is requested.
- Broadphase Dense Dedup Restoration is complete. The dense pair-dedup bitset
  is retained because its roughly 4 MiB maximum memory cost avoids the measured
  CandidatePairs CPU regression.
- Look Lab Random Style Authoring is closed. No further work remains.
- Catto Divergence Repairs is registered as live by owner direction on
  2026-08-15. CD0 is complete by the 2026-08-16 owner rulings: R1, R5, R6, R3,
  R2 stage (a), and the local partial-TOI R4 repair are approved in that order.
  R2 stages (b)-(d) and speculative contacts remain outside the active ledger.

Completed plan files were deleted; git history is the archive.

## Active Plans

| Plan | Commit name | Tasks | Done | File |
|---|---|---|---|---|
| Determinism Envelope Tier-2 Hardening | `TIER2_DETERMINISM` | 9 | 1 | `TODO/determinism-envelope-tier2-hardening.md` |
| Catto Divergence Repairs | `CATTO_REPAIRS` | 6 | 1 | `TODO/catto-divergence-repairs.md` |

## Parked, Backlog, And Completed Plans

These plan files exist in the repository but are not active work. They are
recorded here because a plan file the ledger never names is invisible to the
governance this document owns: a reader cannot tell whether it was parked
deliberately or dropped by accident. None of the rows below is selectable. A
plan runner may not begin one, and moving a row into the Active table above is
an owner decision, not a run decision.

| Plan | Status | Phases | File |
|---|---|---|---|
| Contact Stack Stability Techniques | Owner-parked 2026-08-02 | 0/7 | `WNF/contact-stack-stability-techniques.md` |
| DX12 Frame Path Comment-Rot Sweep | Owner-parked 2026-07-12 | 0/3 | `WNF/dx12-frame-path-comment-rot-sweep.md` |
| Reversible GPU Fracture Replay | Backlog; blocked | 0/7 | `WNF/fracture-replay-feature.md` |
| ImGui + Tracy E17 Comment Audit | Complete | 96/96, 0 deferred | `DONE/imgui-tracy-e17-comment-audit.md` |

Two details in that table are inconsistent with the conventions this ledger
states elsewhere, and both are recorded rather than silently corrected:

- `WNF/fracture-replay-feature.md` sits in `WNF/` but its own header reads
  `Status: Backlog`, not the owner-parked wording the other two WNF plans use.
  Parked and backlog are different dispositions, so the file and this row
  disagree with the directory. An owner ruling should settle which it is.
- The Owner Direction note above states that completed plan files were deleted
  and git history is the archive. `DONE/imgui-tracy-e17-comment-audit.md`
  survives that rule as the sole retained completed plan. Either the rule has an
  unstated exception or the file is a leftover; deleting it is an owner call
  because it is the only record of that audit outside git history.

## Binding Order

1. `TIER2_DETERMINISM` T1, T2 — add the deterministic transcendental owner and
   the math policy gate. Causal C2's
   presentation-only slerp becomes an explicit `retain-owner` site under T2; it
   does not block completing Causal first because it is not physics-reachable.
2. `TIER2_DETERMINISM` T5 through T8 — portable target, test split, Linux
   sanitizer lane, and hosted cross-machine evidence.
3. `TIER2_DETERMINISM` T3 and T4 — adopt deterministic rotation and `acos` after
   the earlier evidence isolates their behavior transitions. Preserve each
   baseline-failing executable and continue; acceptance remains an owner review.
4. `CATTO_REPAIRS` CD1 through CD5 — CD0 is complete. Execute the approved order:
   R1; R5 then R6 as separate commits; R3; R2 stage (a) only; and the local R4
   interval-consistency repair that preserves partial-TOI advancement.

`CAUSAL_INSPECT` no longer re-steps an old frame to regenerate solver detail, so
it does not consume the tier-2 determinism guarantee and has no ordering
dependency on `TIER2_DETERMINISM`. Its true-slerp camera path is presentation-
only and must be classified by the later T2 gate without moving it into a
physics-reachable Maths owner.

`CATTO_REPAIRS` changes the solver values the causal panel may display, but the
panel reads exact retained values rather than freezing expected numbers. Its
tests must pin field mapping and availability, not pre-Catto impulse values.
`future_physics.md` remains intentionally absent from this ledger.

## Portfolio Progress

11/24 tasks complete across two active plans. Causal C0-C8, Determinism T0, and
Catto CD0 are complete; Determinism T1 is the next selectable task.
