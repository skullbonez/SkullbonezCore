# Comment Vocabulary And Banner Convention Audit - CV1 Rulings

Date: 2026-08-03
Branch: `nightrunner-3rd-AUG-26`
Status: CV1 complete; reconciliation remains in CV2-CV4
Impact area: tracked source comments and comment-style governance

## Outcome

The repository will use one spelling for each reusable comment job without
flattening useful domain vocabulary:

- `Summary:` owns file-level orientation;
- `Concept:` owns local plain-language explanation;
- `Invariant:` owns rules that callers or owners must preserve;
- `Runtime allocation policy:` is the canonical allocation-policy heading;
- `CATTO REF` and `ENGINE-SPECIFIC` remain a paired, non-duplicate citation
  convention;
- `Lane R`, `Lane F`, and `Lane P` remain a result/failure/proof taxonomy; and
- precise domain headings remain legal when their noun is the information a
  reader is looking for, rather than an alias for a standard tag.

The 35 legacy banners do not survive as a repository convention. CV2 still
inspects every banner before removal so unique decisions are merged into the
modern header or a nearby structured comment rather than discarded.

CV1 changes no source comment. It records the binding decisions and assigns
their bounded implementation to CV2-CV4. Repository validation and a touched
source comment audit are therefore not required for this documentation-only
phase.

## Rulings

| Census stratum | Ruling | Reason | Reconciliation owner |
|---|---|---|---|
| `Mental model:` after a learning-header `Summary:` | Fold into `Summary:`; do not retain a second header-orientation section. | Both sections orient a reader before detail. A useful mental model strengthens the summary instead of competing with it. | CV3, during the complete Summary inspection. |
| `LAYMAN VERSION:`, `Layman version:`, `Layman physics map:`, and `Plain-language version:` | Fold into `Concept:`. | All four explain a local mechanism in approachable terms; `Concept:` is the documented spelling for that job. | Banner-contained instances in CV2; remaining instances in CV4. |
| `Plain-language rule:` | Fold into `Invariant:`. | The two current comments classify persistent versus transient descriptor ownership. They state rules to preserve, not alternate explanations. | CV4. |
| Legacy `/* -- Name -- */` banners | Retire the format after per-file inspection. Delete duplicate prose, merge unique decisions, and retain no banner solely for visual identity. | Every banner file already has the modern header. The banner is duplicate presentation syntax, but its content is evidence until inspected. | CV2, with an explicit disposition for all 35 files. |
| `CATTO REF` | Retain exactly. | It names the external algorithm or equation that supports the adjacent implementation. | Document in CV4. |
| `ENGINE-SPECIFIC` | Retain exactly. | It marks a deliberate local policy or geometry decision that the external citation does not support. It can stand alone or immediately qualify a `CATTO REF`. | Document in CV4. |
| `Lane R` | Retain exactly. | It classifies recoverable external-input or environment failure represented by an owner/message result. It is orthogonal to `Why:` and may be paired with a structured tag. | Document in CV4; glossary entry already exists. |
| `Lane F` | Retain exactly. | It classifies fatal should-never-happen owned engine state. `Hazard:` explains risk; Lane F states the handling lane. | Document in CV4; glossary entry already exists. |
| `Lane P` | Retain exactly. | It classifies a bounded validation/probe result rather than production error handling. | Document in CV4 and add the missing exact glossary entry. |
| `Docs:` | Retain exactly as a Rendering-local official-API citation marker. | It attaches an authoritative API link to a nearby implementation choice. File-level `Related:` instead provides repository navigation, so the jobs differ. | Document in CV4. |
| `Runtime allocation policy:` and `Allocation policy:` | Use `Runtime allocation policy:` for all current sites. | Every six short-form site describes a cold/startup/developer allocation boundary governed by the same runtime no-growth policy. The explicit spelling tells the reader which repository policy applies. | Normalize the six short-form sites and document the heading in CV4. |
| `Pass contract:` | Retain exactly. | It groups a render pass's resource inputs, outputs, and state transitions. That domain noun is more navigable than a run of generic invariants. | Document as an allowed precise domain heading in CV4. |
| `Caller contract:` | Retain exactly. | It identifies obligations at the call boundary rather than implementation-owned invariants. | Document as an allowed precise domain heading in CV4. |
| Generic `Contract:` | Fold into `Invariant:` at the three current sites. | The current comments state fixed shader/resource layout rules. The generic label adds no domain or boundary information. | CV4. |
| `Compatibility:`, `Capability:`, `Units:`, `Cold boundary:`, `Fallback:`, `Owner:`, `Phase:`, `Precondition:`, `Release/Profile:`, and `Terminal drain:` | Retain their exact spellings as precise local headings. | Each names the category a reader must locate: historical behavior, authority, lifecycle, units, fallback policy, or build configuration. None is a second spelling for one standard structured tag across the current sites. | Document the domain-heading rule in CV4; no bulk rewrite. |
| Governance review dialect | Keep out of source comments. | CV0 found zero leakage. Review-only terms describe audit findings, not engine concepts. | Restate the boundary in CV4. |
| Glossary definitions | No convention change. | The strict inventory has 993 unique single-file definitions, zero multi-file terms, and zero drift. | Add only the missing exact `Lane P` entry in CV4. |

## Domain-Heading Boundary

The surviving custom headings are not an invitation to invent aliases. A
custom heading is justified when its exact noun is a stable search target or
domain category, such as `Pass contract:`, `Units:`, or `Docs:`. When the label
only means explanation, rule, reason, lifetime, or risk, the standard
`Concept:`, `Invariant:`, `Why:`, `Lifetime:`, or `Hazard:` spelling applies.

This distinction preserves reader navigation while making new spelling drift
reviewable without a frozen count, ratio, or allowlist.

## Citation Adjacency

`CATTO REF` and `ENGINE-SPECIFIC` are deliberately retained as two labels:

1. `CATTO REF` identifies what the cited source owns.
2. An adjacent `ENGINE-SPECIFIC` block identifies what SkullbonezCore changes or
   decides locally.
3. `ENGINE-SPECIFIC` may appear without a citation when the entire nearby rule
   is local engine policy.

Neither label implies correctness by itself. The implementation and its tests
remain the proof; the labels make the source/decision boundary visible.

## Reconciliation Map

CV2 owns the complete 35-file banner checklist and all orientation aliases
embedded in those banners. CV3 owns all 587 current `Summary:` fields (the plan
seed count of 327 was historical) and folds the 21 `Mental model:` sections into
honest summaries. CV4 normalizes the bounded non-banner aliases, the two
plain-language rules, six short allocation headings, and three generic contract
headings while updating the guide, audit skill, and exact `Lane P` glossary
entry.

This routing is phase ownership, not permission to skip an occurrence. Final
closure must repeat the tracked inventory and show that no retired alias or
banner spelling remains.

## Validation Decision

CV1 is documentation-only. No source-bearing file changed, so no repository
validation command or comment-style audit is required. CV4 retains the plan's
final `tools\validate_fast.bat`, strict glossary inventory, comment-only diff
proof, and independent review obligations.

