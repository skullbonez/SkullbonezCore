# Math Fatal Survey Restoration — Commit The Missing Call-Site Evidence

Date: 2026-07-15
Status: Active — 0/3 tasks complete
Impact area: documentation/evidence primarily; `Maths` callers only if the
regenerated survey finds an unmigrated reachable-degenerate site
Owner: maths

## Problem And Evidence

The round-4 `math-fatal-removal` plan required its T2 classification table
(file, call, classification, fallback) to be recorded in the plan before the
migration commit. That never happened:

1. Commit `bbf7fdb0` deleted the plan file containing only the original
   94-line text — the survey table was never added to it or to any report.
2. The commit body asserts "Classify every Vector3 normalization and division
   site," but no committed artifact backs that universal claim. Under this
   repository's own evidence rules (MASTER inventory rule 3: a checkbox
   closes only with its acceptance evidence), T2's closure is currently an
   article of faith.
3. The migrations that were spot-checked in the 2026-07-15 adversarial review
   (`PhysicsBodyStore.cpp` impulse paths, camera/terrain/geometric sites) are
   correct; the gap is completeness proof, not known wrongness.

2026-07-15 adversarial review of the round-4 claims, finding 4 (reopens the
`math-fatal-removal` T2 closure).

## Goal

A committed, dated survey report enumerates every `Normalise`/`TryNormalise`/
vector-divide call site in engine source with its classification
(invariant-safe on the plain API vs reachable-degenerate on a Try-API) and
the fallback each migrated site uses. Any site the regenerated survey shows
as reachable-degenerate but still on the plain API is migrated or given a
recorded keep-reason.

## Non-Goals

- No API changes: the Try-API surface from round 4 stands.
- No re-litigation of correctly migrated sites; the report records them.
- No baseline refresh; if migration gaps are found and fixed, the valid-path
  math must remain bit-identical exactly as in round 4.

## Tasks

- [ ] T1 — Regenerate the survey mechanically: enumerate every call to
      `Normalise(`, `TryNormalise`, `TryNormalised`, `TryDivided`, and
      Vector3 `operator/` / `operator/=` across `SkullbonezSource/`, with
      file:line, and classify each row. Commit it as
      `Agentic/Reports/2026-07-15/math-fatal-call-site-survey.md` (follow the
      dated-directory convention the round-4 signature inventory missed).
- [ ] T2 — Close gaps. For every row classified reachable-degenerate but
      still calling a plain (assert-only) API: migrate it to the Try-API with
      an explicit deterministic fallback, or record a per-row keep-reason that
      names why degeneracy is unreachable. Distinct reasons per row — no
      boilerplate blessing.
- [ ] T3 — Final gates. If T2 changed no source: documentation-only, no
      validation, with the diff as proof. If T2 migrated any site:
      `tools\validate_tests.bat` then `tools\validate_physics.bat` byte-exact
      with no baseline refresh.

## Dependencies And Decisions

- Reopens `math-fatal-removal` T2 (round 4); this plan is the remediation row.
- Runs after `fp-envelope-hardening` only if both end up touching the same
  Maths files; otherwise independent.

## Acceptance

- The survey report exists, is complete against a fresh mechanical
  enumeration, and every row has a classification and disposition.
- Zero reachable-degenerate rows remain on plain APIs without a recorded,
  row-specific reason.
- Gates per T3 pass with zero baseline changes.

## Validation

- Documentation-only if no source changes (diff is the proof); otherwise
  `tools\validate_tests.bat` then `tools\validate_physics.bat`, output pasted
  at closure.
