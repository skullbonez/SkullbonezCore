# Comment Vocabulary And Banner Convention Audit Closure

Date: 2026-08-03
Branch: `nightrunner-3rd-AUG-26`
Plan result: CV0-CV4 complete, 5/5
Impact: comments, documentation, and exact governance metadata only

## Result

The audit closes over all 587 tracked C++ source files (260 `.cpp`, 327 `.h`).
Every file was inspected against the comment guide. The execution checklist
closed at 587/587 checked with zero deferred, missing, extra, or duplicate rows;
the deletion-bound checklist was then removed with the completed plan.

CV0 measured the complete convention surface. CV1 assigned one owner to each
reusable comment job. CV2 removed all 62 legacy banner blocks from the exact
35-file scope. CV3 repaired 43 tautological summaries and two stale ownership
claims, folded 20 `Mental model:` aliases into the standard header, removed two
duplicate `Related:` rows, and proved all paths resolved. CV4 made those rulings
authoritative in both the style guide and the audit skill.

## CV4 Reconciliation

The guide and audit skill now describe these conventions before a reviewer meets
them:

- `Summary:` orients the file; `Concept:` explains local vocabulary;
  `Invariant:` records rules; `Runtime allocation policy:` records the canonical
  growth contract.
- Precise domain headings remain qualitative tools, not an allowlist.
- Lane R, Lane F, and Lane P retain their distinct execution/proof meanings.
- `CATTO REF` and `ENGINE-SPECIFIC` retain their citation roles.
- Legacy identity banners and orientation aliases are retired.
- Governance vocabulary remains in review artifacts, not production source.
- Repository-relative `Related:` entries cite permanent reports, never live TODO
  plans.

The final source reconciliation normalized 11 body labels: two `LAYMAN` labels
became `Concept:`, six `Allocation` labels became
`Runtime allocation policy:`, and three `Contract` labels became `Invariant:`.
The remaining Core identity banner was removed. The local Lane P definition was
consolidated into the engine glossary, leaving 992 unique source-local glossary
definitions with zero multi-file terms or drift. One inaccurate RotationMatrix
summary found by independent review was corrected to describe only its actual
orthogonal-basis operations.

The post-change tree contains 2,037 repository-relative `Related:` paths. Every
path resolves, no source cites a deletion-bound TODO plan, and the existing
fail-closed resolver remains the maintenance mechanism; no count budget was
introduced.

## Behavioral And Governance Proof

Fifteen touched source files passed the mandatory comment audit. A
comment-stripped comparison reported 15/15 exact matches, proving zero code-token
changes. The only non-documentation records changed are exact-current-source
governance metadata: 16 aggregate-ruling site coordinates moved with comment
line shifts, and the existing `Run::RunInputPhase` complexity ruling received
the digest of its comment-only body. No qualitative ruling, signature, control
flow, numeric expression, or runtime policy changed.

Independent review first identified the missed body aliases, Core banner, local
Lane P duplication, and the inaccurate RotationMatrix summary. Each finding was
repaired. The final narrow review returned clean with no remaining finding.

## Validation

- Comment-stripped proof: PASS, 15/15 touched source files, zero mismatches.
- `tools\validate_format.bat`: PASS; 587 source files and all 2,037 Related
  paths are valid.
- `python tools/inventory_glossary_terms.py --repo . --strict`: PASS; 992
  definitions, 992 unique terms, zero multi-file terms, zero drift.
- Strict authority-free aggregate inventory: PASS; 88/88 gated rows ruled.
- Strict function-complexity inventory: PASS; 6,342 functions, 40/40 triggered
  bodies ruled.
- Strict reachability inventory after the Automation refresh: PASS.
- `tools\validate_fast.bat`: PASS in 389.2 seconds, including formatting,
  project filters, dependency proof, all ownership inventories, Profile build,
  unit tests, and current Debug/Profile/Automation reachability evidence.

No baseline moved and no baseline-refresh authority was used.

## Phase Evidence

- `comment-vocabulary-audit-cv0-census.md`
- `comment-vocabulary-audit-cv1-rulings.md`
- `comment-vocabulary-audit-cv2-banners.md`
- `comment-vocabulary-audit-cv3-summary-related.md`
