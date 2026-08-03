# Comment Vocabulary And Banner Convention Audit

Date: 2026-08-02
Status: IN PROGRESS — 4/5 phases complete
Impact area: Source comments across every subsystem, comment style guide, glossary reference
Owner: Comment Quality Gate
Priority: Third

## Problem And Evidence

The comment standard is working. All 587 tracked source-bearing files under
`SkullbonezSource/` carry a learning header, and the structured tag vocabulary is
applied at real density:

| Tag | Occurrences |
|---|---|
| `Invariant:` | 584 |
| `Glossary:` | 416 |
| `Why:` | 397 |
| `Concept:` | 322 |
| `Lifetime:` | 261 |
| `Hazard:` | 111 |
| `Lane F` | 73 |

One thing the audit should confirm rather than assume: the governance *review*
dialect has not leaked into source. `extraction scar`, `capability slice`,
`courier`, and `closure failure` occur zero to two times across the entire tree.
That vocabulary lives in `AGENTS.md` and in reviews, which is where it belongs.
This plan must not import it, and a finding that recommends importing it is
out of scope.

The actual problem is that several conventions compete for the same job, and a
reader cannot predict which one a file will use:

1. **Three names for the same section.** `Mental model:` appears 21 times,
   `LAYMAN VERSION:` twice, and neither is part of the standard
   Purpose/Summary/Glossary/Invariants/Related section set. All three do the same
   work — orienting a reader before the detail — under three spellings, one of
   which appears in only two files.

2. **A pre-standard banner stratum.** 35 files still open with the
   `/* -- Name ----...---- */` banner, concentrated in `Maths/`, `Core/Log.h`,
   `Core/Profiler.h`, `Core/Timer.h`, and older `Physics/Bounding*`. Several carry
   both the banner and a modern learning header, so the file announces its purpose
   twice in two formats. `Maths/Vector3.h` is the clearest instance: a modern
   header with a full Invariants block, followed by a banner whose entire content
   is "Represents a 3D vector, no encapsulation required for this class."

3. **A high-value convention that is invisible until you trip over it.**
   `CATTO REF` (23) and `ENGINE-SPECIFIC` (34) mark where the physics
   implementation follows a cited source and where it deliberately diverges. This
   is the single most valuable comment convention in the repository and it is
   documented nowhere a reader would look before opening `PersistentContactSolver.cpp`
   or `ObjectContactManifold.cpp`. `LAYMAN VERSION` is a fourth spelling in the
   same neighbourhood.

4. **587 `Summary:` lines of unmeasured quality.** The Comment Quality Gate
   requires every `Summary:` to add ownership, decision, or flow information
   beyond the filename, and states plainly that a filename restatement is not a
   summary. Nothing measures compliance. Spot-reading suggests most are genuine,
   which makes the exceptions worth finding rather than worth assuming away.

5. **893 `Related:` entries pointing into `Agentic/`.** Zero currently point at
   `Plans/TODO/`, so the deletion-bound-plan rule is being followed correctly. The
   durable half — report citations under `Agentic/Reports/<date>/` — is now large
   enough that link rot is a systemic risk rather than an incidental one, and the
   ratio of report links to source-file links is itself worth knowing.

## Goal

One documented convention per job. Every convention a reader will encounter is
described in `Agentic/Reference/comment-style-guide.md` before they meet it in a
file, the pre-standard banner stratum is reconciled, and `Summary:` and `Related:`
quality is measured rather than assumed.

## Non-Goals

- No mass comment rewrite. This is a census, a set of rulings, and bounded
  reconciliation of the specific strata found.
- No removal or dilution of the structured tag vocabulary. It is working and the
  density above is evidence, not a problem to solve.
- No import of governance review dialect into source comments.
- No new mechanical count budget, ratio, or ratchet on comments of any kind. The
  Governance Review Model already bans that shape and this plan does not get an
  exception for being about comments.
- No behavior change. Every edit in this plan is comment-only and must remain so.
- No change to the seven ownership inventories or to `inventory_glossary_terms.py`
  policy. This plan may report what those tools show; it does not re-legislate
  them.

## Phases

- [x] **CV0 — Census every comment convention in use.** The tracked inventory is
  587 files (260 `.cpp`, 327 `.h`), all with exact modern core header fields.
  Current structured-tag counts, six plain-language spellings across 29 files,
  the 35/35 banner-plus-modern-header stratum, the three-file 23/35 physics
  citation split, execution/proof lanes, recurring custom headings, zero source
  governance-dialect leakage, and the clean 993-term glossary inventory are
  recorded in
  `../../Reports/2026-08-03/comment-vocabulary-audit-cv0-census.md`.

  Original work order: Using `git ls-files` for
  the inventory, not `rg`, produce the complete list of conventions actually
  present: section-header spellings and their counts, banner-style files and
  whether each also carries a modern header, the `CATTO REF`/`ENGINE-SPECIFIC`/
  `LAYMAN VERSION` family and its file distribution, and any convention not
  described in the current style guide. For each, record how many files use it,
  which subsystems, and whether it duplicates another convention's job. Record the
  current `inventory_glossary_terms.py` output as context for multi-file term
  definitions. The census is the phase deliverable; do not begin editing comments
  from a reading impression.

- [x] **CV1 — Rule on the competing conventions.** `Summary:` owns file
  orientation, `Concept:` owns local plain-language explanation, `Invariant:`
  owns rules, and `Runtime allocation policy:` is the canonical allocation
  heading. The Catto/engine citation pair, result/failure/proof lanes, and
  precise domain headings remain because their jobs differ. Legacy banners are
  retired only after CV2's per-file content review. Exact reasons and the
  CV2-CV4 reconciliation map are recorded in
  `../../Reports/2026-08-03/comment-vocabulary-audit-cv1-rulings.md`.

  Original work order: For each duplicate found in
  CV0, choose one spelling and record the reason. Expected decisions, each of
  which may be overturned by evidence from the census: fold `LAYMAN VERSION` into
  whichever of `Mental model:` or `Concept:` survives; keep `CATTO REF` and
  `ENGINE-SPECIFIC` unchanged and instead document them, because a citation
  convention that names its source and its deliberate divergences is the most
  valuable thing in the comment set and its only defect is being undocumented.
  A ruling may legitimately be "retain both, they do different jobs" — but it must
  say what the different jobs are.

- [x] **CV2 — Reconcile the banner stratum.** The exact 35-file tracked scope
  contained 62 banner blocks and now contains zero. Informative blocks became
  standard `Concept:` comments, unique facts moved into modern headers, and
  duplicate/stale prose was removed. The touched-source comment audit is 35/35
  with zero deferred, every scoped Related path resolves, and a comment-stripped
  comparison proves zero code-token changes. Per-file dispositions and claim
  verification are recorded in
  `../../Reports/2026-08-03/comment-vocabulary-audit-cv2-banners.md`.

  Original work order: For each of the 35 banner files,
  decide per file: delete the banner when a modern header already states its
  content, merge banner content into the header when the banner says something the
  header does not, or retain the banner with a recorded reason. `Maths/Vector3.h`,
  `Core/Log.h`, `Core/Profiler.h`, and `Core/Timer.h` are the anchor cases and
  each should be resolved first so the rest follow a worked example. Preserve any
  content that is genuinely informative; deleting a banner is not automatically an
  improvement, and a banner that carries the only statement of a design decision
  gets merged, never dropped.

  **Tracked CV2 banner checklist (`git ls-files`, 35 files):**

  - [x] `SkullbonezSource/Core/Log.h`
  - [x] `SkullbonezSource/Core/Profiler.h`
  - [x] `SkullbonezSource/Core/Timer.h`
  - [x] `SkullbonezSource/Maths/GeometricMath.h`
  - [x] `SkullbonezSource/Maths/GeometricStructures.h`
  - [x] `SkullbonezSource/Maths/Matrix4.h`
  - [x] `SkullbonezSource/Maths/Quaternion.h`
  - [x] `SkullbonezSource/Maths/RotationMatrix.h`
  - [x] `SkullbonezSource/Maths/Vector3.h`
  - [x] `SkullbonezSource/Physics/BoundingBox.h`
  - [x] `SkullbonezSource/Physics/BoundingSphere.h`
  - [x] `SkullbonezSource/Physics/CollisionShape.h`
  - [x] `SkullbonezSource/Physics/SpatialGrid.h`
  - [x] `SkullbonezSource/Rendering/DX12/BLASDX12.h`
  - [x] `SkullbonezSource/Rendering/DX12/FramebufferDX12.h`
  - [x] `SkullbonezSource/Rendering/DX12/MeshDX12.h`
  - [x] `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.h`
  - [x] `SkullbonezSource/Rendering/DX12/SBTDX12.h`
  - [x] `SkullbonezSource/Rendering/DX12/ShaderDX12.h`
  - [x] `SkullbonezSource/Rendering/DX12/TLASDX12.h`
  - [x] `SkullbonezSource/Rendering/PrimitiveBatchRenderer.h`
  - [x] `SkullbonezSource/Rendering/PrimitiveMeshBuilder.h`
  - [x] `SkullbonezSource/Rendering/RenderGraph.h`
  - [x] `SkullbonezSource/Rendering/Text.h`
  - [x] `SkullbonezSource/Runtime/App/Run.h`
  - [x] `SkullbonezSource/Runtime/App/Window.h`
  - [x] `SkullbonezSource/Runtime/Camera/CameraCollection.h`
  - [x] `SkullbonezSource/Runtime/Debug/BroadphaseVisualizer.h`
  - [x] `SkullbonezSource/Runtime/Debug/CollisionVisualizer.h`
  - [x] `SkullbonezSource/Runtime/Input/Input.h`
  - [x] `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h`
  - [x] `SkullbonezSource/Scene/AuthoredScene.h`
  - [x] `SkullbonezSource/World/SkyBox.h`
  - [x] `SkullbonezSource/World/Terrain.h`
  - [x] `SkullbonezSource/World/WorldEnvironment.h`

- [x] **CV3 — Measure `Summary:` honesty and `Related:` durability.** The complete
  587-file inspection repaired 43 Purpose/filename restatements, folded all 20
  remaining `Mental model:` blocks into Summary, corrected two stale ownership
  claims, and removed two duplicate Related rows. The final tree has zero exact
  Purpose/Summary copies and zero retired orientation labels. All 2,036
  repository-relative Related paths resolve: 1,102 target source, 778 Reference,
  114 permanent Reports, 20 tests, 14 tools, and eight other repository paths.
  No TODO plan is cited; the maximum Related block is eight focused entries and
  no block is an index. Report-link volume is managed by permanent paths and the
  existing fail-closed checker, not a new count budget. The 587/587 tracked
  ledger, 67/67 touched-source audit, risk ruling, and zero-code-token proof are
  recorded in
  `../../Reports/2026-08-03/comment-vocabulary-audit-cv3-summary-related.md`.

  Original work order: Inspect all 587 `Summary:` lines against the rule that a
  filename restatement is not a summary, and repair the ones that fail. Inspect
  all current `Related:` entries:
  confirm every path resolves, report the split between source-file citations and
  `Agentic/Reports/` citations, and identify any file whose `Related:` block is
  long enough that it has stopped being navigation and become an index. Report
  whether report-link volume is now a maintenance risk and, if so, propose a
  strategy — the proposal is the deliverable; adopting it is an owner decision,
  not something this phase implements.

  The required tracked-source inspection ledger is
  `comment-vocabulary-summary-related-checklist.md` (587 rows from
  `git ls-files`). A row is checked only after both fields and the complete file
  comment contract have been inspected.

- [ ] **CV4 — Update the style guide and close.** Fold every CV1 ruling and the
  CV2 outcome into `Agentic/Reference/comment-style-guide.md` so each surviving
  convention is described before a reader meets it, including the
  `CATTO REF`/`ENGINE-SPECIFIC` citation family, which currently exists only in
  practice. Update `Agentic/Skills/comment-style-audit/skill.md` if a ruling
  changes what an auditor checks — per the `AGENTS.md` requirement that a rule
  delegated to a review must be stated in the skill file the reviewer actually
  reads. Prove the complete diff is comments and documentation only. Run
  `tools\validate_fast.bat` for the mechanical `Related:` path-resolution gate, and
  the glossary inventory gate if any glossary term moved.

## Dependencies And Decisions

- Runs after the two physics plans and the render graph plan because those add
  source and therefore comments; auditing conventions before that work lands would
  audit a tree that is about to change.
- Feeds `source-modernization-sweep.md`. CV2's banner rulings and CV1's convention
  rulings determine what that plan's comment-adjacent edits may touch, so CV1 and
  CV2 must close before that plan's corresponding phase begins.
- Comment-only source edits count as documentation-only for repository validation
  provided the diff is strictly comments and docs. If any edit changes behavior,
  stop and switch to the mapped validation for the touched files. `Maths/Vector3.h`
  is included by 61 files and `Core/Common.h` by 63, so an accidental behavioral
  edit there is a `validate_full` event, not a comment cleanup.
- This plan does not create a comment metric. If a later reader wants one, that is
  a separate owner decision that has to argue past the Governance Review Model's
  ban on frozen budgets.

## Acceptance

The plan closes when the convention census is complete, every competing convention
has a recorded ruling, all 35 banner files are resolved per file with reasons,
every `Summary:` that restates its filename is repaired, every `Related:` path
resolves and the report-link risk is characterized with a proposal, the style
guide describes every surviving convention including the citation family, the
audit skill matches the rulings, and the diff is provably comments and docs only.

## Validation

- `git ls-files` scoped inventory reconciled against the CV0 census before closing
- `tools\validate_fast.bat` — includes mechanical `Related:` path resolution
- `python tools\inventory_glossary_terms.py --self-test` and `--repo . --strict`
  if any glossary term definition moved
- Proof that the complete diff is comments and documentation only
- Independent read-only review of the rulings and the banner reconciliation

## Related

- `../../Reference/comment-style-guide.md`
- `../../Reference/engine-glossary.md`
- `../../Skills/comment-style-audit/skill.md`
- `../../../SkullbonezSource/Maths/Vector3.h`
- `../../../SkullbonezSource/Physics/PersistentContactSolver.cpp`
- `source-modernization-sweep.md`
