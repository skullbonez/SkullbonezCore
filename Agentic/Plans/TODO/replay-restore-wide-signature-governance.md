# Replay Restore And Wide-Signature Governance

Date: 2026-07-28
Status: TODO — 3/4 phases complete
Impact area: Replay restore ownership, signature governance, review tooling
Owner: Replay + repository governance
Priority: High

## Problem And Evidence

The bounded 2026-07-28 response deletes the same-class
`RestoreV2ArtifactTargetState` → `RestoreV2ArtifactTargetStateImpl` pure
forwarder. The surviving restore operation still has 12 parameters, and the
wide-signature inventory contains a visible cluster at the mandatory review
trigger. A numeric ceiling could become a target: signatures could be managed
to 12 even when the operation still needs ownership decomposition.

## Goal

Give Replay restore a concrete owner-shaped operation with no forwarding facade
or authority-free argument bag, and make the inventory support qualitative
ownership review rather than treating “at the trigger” as an accepted endpoint.

## Owner Rulings

1. Replace the hard 12-parameter pass/fail ceiling with a mandatory qualitative
   ownership-review trigger. Twelve remains visible inventory evidence, not an
   accepted allowance or an automatic defect.
2. Reopen every current exact-12 row for a fresh owner review. A prior `Keep`
   disposition does not satisfy RG0.

## Phases

- [x] **RG0 — Reproduce the current inventory and classify exact-trigger
  rows.** Record real parameter ownership, forwarders, capability slices,
  immediate destructuring, and operations whose ordering belongs in an
  invariant owner. Do not add a lower count budget. Census:
  `Agentic/Reports/2026-07-28/replay-wide-signature-rg0-census.md`.
- [x] **RG1 — Decide and document the governance instrument.** Apply the owner
  answers above consistently in `AGENTS.md`, both independent-review skills,
  inventory output, fixtures, and validation. Counts remain measurements, not
  allowances. Governance evidence:
  `Agentic/Reports/2026-07-28/replay-wide-signature-rg1-governance.md`.
- [x] **RG2 — Decompose Replay restore by responsibility.** Keep
  `ReplayRestoreTransaction` only for the phase/arbitration rule it actually
  enforces; move topology rebuild, artifact selection, and diagnostics to their
  concrete owners or focused operations. No courier struct, callback pack,
  reach-back pointer, or renamed `*Context`. Decomposition evidence:
  `Agentic/Reports/2026-07-28/replay-wide-signature-rg2-decomposition.md`.
- [ ] **RG3 — Close review and Replay gates.** Re-run all three ownership
  inventories, answer the five mandatory ownership questions, complete the
  touched-file comment audit, and prove one-invocation Replay visual fidelity
  plus broad validation without refresh.

## Acceptance

Replay restore has no pure forwarder and no same-authority wrapper. Every
surviving exact-trigger signature has a current qualitative owner ruling under
the chosen policy, and the tooling cannot report “Keep” merely because the
number is within a limit.

## Validation

Inventory self-tests and repository scans, `tools\validate_fast.bat`,
`tools\validate_tests.bat`, `tools\validate_replay_visual_fidelity.bat`, and
`tools\validate_full.bat`.
