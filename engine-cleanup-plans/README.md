# Engine Cleanup Plans

Date: 2026-07-08 (consolidated 2026-07-09)
Status: In Progress
Owner: Architecture cleanup

> **Start here: [`Agentic/Plans/MASTER-PLAN.md`](../Agentic/Plans/MASTER-PLAN.md)**
> — the authoritative inventory of every remaining plan in the repository with
> percent-complete. The working protocol for this campaign lives in
> [`00-EXECUTION-GUIDE.md`](00-EXECUTION-GUIDE.md).

These plans come from an adversarial architecture audit of SkullbonezCore
(~145K lines first-party C++). The through-line the audit found: **the codebase
invests heavily in *policing* architecture (a multi-thousand-line boundary
linter, hundreds of docs, formal gates) while the architecture itself carries
classic god-objects, a documented policy its code contradicts, and almost no
behavioral tests.** These plans target substance, not ceremony.

Every plan follows two rules learned from the facade-retirement review:

1. **Acceptance is structural and measurable** (types deleted, function line
   counts down, tests exist) — never "the word is gone" or "a comment changed."
2. **No plan adds boundary-checker rules as its enforcement.** Plan `03`
   *removes* that apparatus entirely — the regex linter and every frozen
   `MAX_*` ratchet are deleted, not trimmed.

## Lifecycle

- Remaining plans and their status:
  [`Agentic/Plans/MASTER-PLAN.md`](../Agentic/Plans/MASTER-PLAN.md).
- **Completed plans are deleted, not archived.** Git history is the archive.
  Do not recreate `DONE/`, `Done/`, `Failed/`, `Rejected/`, `To_Eval/`, or
  `In_Progress/` folders. Consolidated active plans live in
  `Agentic/Plans/TODO/`.
- Handoffs: keep only load-bearing ones (owner decisions, the latest slice
  handoff per active plan). Delete slice records once superseded — the
  evidence lives in commit messages.
- Owner steering on 2026-07-09 approved the Plan 03, Plan 07, Plan 11, and
  FAC-005 decision gates. Use the recorded decisions in
  [`HANDOFF-2026-07-09-OWNER-DECISIONS.md`](HANDOFF-2026-07-09-OWNER-DECISIONS.md)
  instead of asking again.

## Notes

- All files here are documentation. Creating or editing them requires no
  repository validation. Each plan names the validation its *implementation*
  needs.
