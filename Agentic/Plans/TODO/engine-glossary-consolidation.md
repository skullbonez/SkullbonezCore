# Engine Glossary Consolidation

Date: 2026-07-31
Status: IN PROGRESS — 1/4 phases complete
Impact area: Comment standard, shared engine vocabulary, per-file learning headers
Owner: Documentation standard
Priority: Medium

## Problem And Evidence

The comment standard is enforced by shape, and shape is the part that can be
satisfied without insight. 570 of 576 tracked first-party source files carry a
`Glossary:` block. Measured on 2026-07-31, the most-repeated entries are:

| Term | Files defining it |
|---|---:|
| Draw command | 46 |
| Hit box | 44 |
| Descriptor | 32 |
| Back buffer | 32 |
| Narrowphase | 30 |
| Broadphase | 30 |
| Manifold | 26 |
| Lane R result | 25 |
| DXR (DirectX Raytracing) | 18 |
| SkullScope | 14 |

A definition repeated in 46 files teaches nothing about any of those 46 files.
It is a dictionary entry stored in the wrong place, and it has the maintenance
property that changing one definition is a 46-file edit — which means in
practice it will never be changed and the copies will drift apart silently.

Separately, 77 files carry the template sentence
`"As a public header, keep edits anchored on ..."` and 73 carry a `Summary:`
that restates the filename. `SkullbonezSource/UI/UIState.h:6` is the clearest
example: *"UIState.h implements UI State widgets, layout, drawing, or UI state
for the in-engine controls."* That is a tautology occupying a required field.

`AGENTS.md` already predicts this failure — *"Do not treat 'file has a learning
header' as full compliance"* — and the measurement shows it happened anyway.
The gap is that nothing distinguishes a header someone had something to say in
from a header that had to have something in the box.

## Goal

One authoritative definition per shared engine term, cited rather than copied,
with per-file glossaries reduced to genuinely file-local vocabulary. The
`Summary:` field is retained and required to carry information the filename does
not already give.

## Owner Ruling

**Keep the `Summary:` field.** The original review proposed dropping the
mandatory Summary and Glossary block entirely; the owner retains Summary. The
change is narrower: `File`, `Purpose`, `Summary`, `Invariants`, and `Related`
stay exactly as they are. Only `Glossary` handling changes, plus repair of
Summary lines that restate the filename.

**Placement.** The shared glossary is `Agentic/Reference/engine-glossary.md`,
cited from `Related:`. It is deliberately not `Core/Common.h`: that header
states an invariant against regaining domain content, is included very widely,
and having `Core` define Rendering, Physics, and Replay vocabulary would invert
the dependency-direction rule the repository enforces mechanically. `Related:`
paths already point into `Agentic/Reference/` throughout the tree and
`validate_fast` already enforces that those paths resolve, so a citation is
mechanically checked to exist.

**The split rule.** A term defined in more than one tracked source file belongs
in the shared glossary. A term defined in exactly one file is file-local
vocabulary and stays where it is. This is mechanically decidable, which is what
makes it enforceable without a reviewer's memory. `Model row hint` (7 files) and
`Published prefix` (7 files) are judgement calls the ruling must resolve
explicitly rather than by count alone; the count identifies candidates, it does
not decide them.

## Non-Goals

- No source behavior change. This plan is documentation-only throughout and must
  prove it: no `.cpp`/`.h` diff may alter a token outside a comment.
- No removal of the `Invariant:` / `Hazard:` / `Lifetime:` / `Concept:` / `Why:`
  local comment vocabulary. Those carry information that cannot be faked and are
  explicitly retained.
- No removal of the `CATTO REF:` / `ENGINE-SPECIFIC:` convention. It is the
  highest-value documentation idea in the codebase and should be promoted, not
  disturbed.
- No frozen count, ratio, or budget on glossary entries per file. The inventory
  reports current structure; it never becomes a ceiling.

## Phases

- [x] **GC0 — Inventory shared versus local vocabulary and build the file
  checklist.** Produce the complete term → defining-files inventory across every
  tracked `.cpp`/`.h`/`.hpp`/`.inl`/`.hlsl` file. Classify each multi-file term
  as shared vocabulary, or as a term that is legitimately local in each site and
  must stay. Detect and report definitions of the same term whose wording has
  already drifted between files — those are the concrete evidence that copies
  rot, and each needs an adjudicated canonical wording. Per the `AGENTS.md`
  Comment Quality Gate, create the explicit per-file checklist for this pass
  under `Agentic/Plans/` from `git ls-files`, not `rg`, with one checkbox per
  tracked source-bearing file in scope. Evidence:
  `Agentic/Reports/2026-07-31/engine-glossary-consolidation-gc0-inventory.md`.
  Completed with the corrected 575-file source scope: the provisional 576
  included `SkullbonezSource/AGENTS.md`. The complete inventory contains 2,172
  definitions for 1,285 terms: 321 shared, 964 local, and 264 shared terms with
  wording drift. The 575-row checklist is
  `Agentic/Plans/engine-glossary-consolidation-comment-checklist.md`.

- [ ] **GC1 — Author the shared glossary and update the standard.** Write
  `Agentic/Reference/engine-glossary.md` with one adjudicated definition per
  shared term, resolving every drift GC0 found. Update
  `Agentic/Reference/comment-style-guide.md` to state the split rule, retain
  `Summary:`, and require that a `Summary:` say something the filename does not.
  Because `AGENTS.md` requires that a delegated rule be stated in the file the
  reviewer actually reads, update `Agentic/Skills/comment-style-audit/skill.md`
  and the `Agentic/Skills/rubber-duck/SKILL.md` review questions in the same
  task. Add `tools/inventory_glossary_terms.py` following the established
  repeatable-inventory contract: it reports multi-file term definitions and
  wording drift, uses the same unruled-fails/ruled-passes shape as the existing
  five inventories, and introduces no threshold. Register it in
  `tools/README.md` and the `AGENTS.md` file-to-validation mapping. Evidence:
  `Agentic/Reports/2026-07-31/engine-glossary-consolidation-gc1-standard.md`.

- [ ] **GC2 — Execute the per-file consolidation pass.** Working the GC0
  checklist, remove shared-term definitions from per-file glossaries and add the
  `Agentic/Reference/engine-glossary.md` citation to `Related:`. Leave genuinely
  local vocabulary in place. Also delete the 77 instances of the template
  sentence `"As a public header, keep edits anchored on ..."` and its
  implementation-unit variant, which carry no information in any file. Tick a
  checklist item only after the file has been inspected against the updated
  guide; leave intentionally deferred files unchecked with a stated reason and
  never silently skip one. Evidence:
  `Agentic/Reports/2026-07-31/engine-glossary-consolidation-gc2-pass.md`.

- [ ] **GC3 — Repair tautological summaries and reconcile.** For each of the 73
  files whose `Summary:` restates the filename, write a summary that states what
  the file owns or decides. Where a file genuinely has nothing to say beyond its
  name, that is itself a finding about the file, not a licence to keep the
  tautology — record it. Rerun the scoped `git ls-files` inventory, reconcile it
  against the checklist, and report the checklist path, checked count, deferred
  count, and any file still unchecked. Rerun `tools/inventory_glossary_terms.py`
  and confirm no unruled multi-file term remains. Evidence:
  `Agentic/Reports/2026-07-31/engine-glossary-consolidation-closure.md`.

## Acceptance

Every shared engine term has exactly one definition, cited from the files that
use it. Per-file glossaries contain only vocabulary local to that file. No
`Summary:` restates its filename. The template filler sentence is gone. A new
multi-file term definition is caught by an inventory rather than by whichever
reviewer happens to look. The diff is provably comments and documentation only.

## Validation

Documentation-only for the Markdown and comment work; the diff must be proven to
contain no non-comment source token change. `tools/inventory_glossary_terms.py`
is new tooling, so GC1 and GC3 each run `tools\validate_fast.bat` and then the
script's own self-test and repository scan directly, per the `AGENTS.md`
`tools/*` mapping.
