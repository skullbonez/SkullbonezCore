# Governance Shape-To-Judgment Conversion

Date: 2026-07-26
Status: NOT STARTED — drafted from the 2026-07-26 from-source architecture
review of `nightrunner-26th-JUL-26` at tip `35f6de4e` (review conducted without
plan files or git history; this plan carries the dated evidence). Registered in
`MASTER-PLAN.md` on 2026-07-26 as plan 1 of the Architecture Follow-Up Campaign
Round 5. Runs first in the campaign. 0/6 phases complete.
Impact area: `AGENTS.md`, `MASTER-PLAN.md` inventory rules, `Agentic/Reference/`
style and structure guides, `Agentic/Skills/` review and audit skills, `tools/`
checkers
Owner: governance
Priority: High — this plan defines the acceptance test that the other twelve
campaign plans are judged against. Deleting today's ceremonial shapes without it
guarantees they return under new names.

## Problem And Evidence (measured 2026-07-26)

`AGENTS.md` bans aggregate *shapes* (context bags, service bags, callback packs,
parameter bags) and enforces them by name and by review. Review evidence from
this tip shows the bans are satisfied by pattern-matching while the coupling
they exist to prevent survives. Four concrete mechanisms:

1. **The ban is on the noun, so the noun multiplies.** 99 aggregate parameter
   types match `*Context|*Input|*Inputs|*Params|*Args|*Request|*Facts|*Operands`
   under `SkullbonezSource/`. Six carry exactly one member. Two of those six are
   distinct types wrapping the *same* single reference:
   `TornadoUICommandContext` (`Runtime/Interaction/OperatorCommandApplier.h:78`)
   and `PhysicsSleepPolicyUICommandContext` (`:87`) each hold only
   `SceneWorld& world`. `SceneRuntimeCreateContext`
   (`Runtime/Scene/SceneRuntimeCreate.h:35`) holds only
   `SceneController& controller` and is passed **by value** into
   `CreateSceneFromUI`, which could have taken `SceneController&`. The existing
   Invariant Ownership Rule already says an aggregate that exists "only to carry
   data to shorten a signature is an authority-free bag and remains banned" —
   these do not even shorten a signature, and no rule text catches them.

2. **The ban is on members, so authority moves to locals.** Four physics
   translation units bind parameters back to member-prefixed locals so a body
   lifted out of a god class needed no internal edits:
   `Physics/PersistentContactSolver.cpp:124-134` (11 aliases),
   `Physics/Diagnostics/SkullScope.cpp:168-183` (16),
   `Physics/SleepIslandSystem.cpp:52-54` (3),
   `Physics/PhysicsDiagnosticsSink.cpp:207-209` (3). No checker can see a local
   variable. The God-Object Closure Rule's "authority remains in `Run`" clause
   describes exactly this failure but only in terms of members and facades.

3. **The ban is on the whole surface, so the surface is sliced.**
   `Runtime/RuntimeFrameViews.h:24` states the invariant "No capability slice
   spans the complete frame surface." The same file declares four views totalling
   23 references — `Run`'s member list — and `Run::RunInputPhase`
   (`Runtime/App/Run.h:192`) takes all four. The letter holds; the intent does
   not. `RuntimeRenderBackendView` (`Runtime/Render/RuntimeRenderHost.h:150`)
   carries eleven nullable concrete `Rendering::Dx12*` pointers; the
   `concrete-parameter-bag-elimination` PB3 review already named it "the service
   bag" when it rejected it as a composer parameter
   (`../Reports/2026-07-26/concrete-parameter-bag-elimination-pb3-render-ui.md:46`)
   yet it survives as a `Run` member and inside
   `RuntimeFramePresentationView`.

4. **The Governance Review Model bans frozen counts but offers no replacement
   instrument.** Its "enforced by code review" clause is correct in principle,
   and `tools/inventory_wide_signatures.py` proves the repository already knows
   how to build a *repeatable inventory* that reports without ratcheting. No
   equivalent inventory exists for authority-free aggregates or extraction
   scars, so those two classes are reviewed only when a human notices.

5. **The review skills that are the enforcement mechanism do not mention the
   rules they enforce.** `AGENTS.md` delegates aggregate-shape enforcement to
   code review, and the orchestrator skill routes that review to
   `Agentic/Skills/rubber-duck/SKILL.md` and `Agentic/Skills/carmack-test/SKILL.md`.
   Neither skill file contains the words `bag`, `Context`, or `aggregate`
   anywhere (verified 2026-07-26): `rubber-duck` defines Purpose, Operating Mode,
   Workflow, and Output Shape with no aggregate-ownership criterion, and
   `carmack-test` has a Rubric and Hard Checks section with no row for it.
   `Agentic/Skills/comment-style-audit/skill.md` and
   `Agentic/Reference/skullbonez-core-class-structure.md:80` mention the shapes
   only in passing. So the mechanism `AGENTS.md` relies on has never been told
   what to look for; a reviewer catches these shapes only by having read
   `AGENTS.md` in the same session.

The regime is strong where it is mechanical (zero `throw`, zero `virtual`, zero
`std::function`, directional include proofs, `Related:` path resolution) and
weak exactly where it is shape-descriptive.

## Goal

Every aggregate-shape rule in `AGENTS.md` states a question about ownership that
a reviewer answers with evidence, not a pattern a type can be renamed around.
Two new repeatable inventories report authority-free aggregates and extraction
scars the way `inventory_wide_signatures.py` reports parameter counts — as
current measurements with owner rulings, never as frozen budgets.

## Non-Goals

- No frozen counts, line budgets, spelling ratchets, or percentage gates. The
  existing Governance Review Model prohibition stands and this plan strengthens
  it.
- No relaxation of the four sound bans. Context bags, owner reach-back, callback
  packs, and the 12-parameter ceiling all survive; only their *test* changes.
- No source behavior change. This plan edits governance documents and adds
  checkers; the twelve sibling plans do the source work.
- No new dependency-direction rules. `check_dependency_graph.py` keeps its
  current authority; this plan adds `content_rules` rows only where the check is
  genuinely mechanical.
- No revision of the GV0 Invariant Ownership Rule's core test. GV0 got the test
  right; this plan closes the gaps GV0 did not reach.

## Phases

- [ ] **G0 — Census which rule produced which shape.**
  Produce a report that maps every current aggregate-shape offender to the
  `AGENTS.md` rule whose wording admits it. Cover all 99 matched aggregates, the
  four extraction-scar files, the four frame views, `RuntimeRenderBackendView`,
  and the `OperatorCommandApplier` 8-context/8-result/8-apply family. For each,
  record: the type or site, the rule text that permits it, and the specific
  wording change that would have caught it. Explicitly carry forward every PB0
  `Explicit Retain Ruling`
  (`../Reports/2026-07-26/concrete-parameter-bag-elimination-pb0-census.md:82-99`)
  as already-ruled and out of scope for redecision. Acceptance: the report names
  a rule gap for every offender, or classifies the offender as a legitimate
  domain value with the reason; no offender is left unattributed.

- [ ] **G1 — Convert the shape rules to ownership questions.**
  Amend `AGENTS.md`:
  - **Invariant Ownership Rule**: add the *single-member and unpacked-at-entry*
    tests. An aggregate whose member count is one, or whose sole consumer
    immediately destructures every member without enforcing an ordering,
    lifetime, or arbitration rule, is authority-free regardless of its name.
    Add the one-reference-by-value case explicitly.
  - **God-Object Closure Rule**: add the *extraction-scar* clause. Rebinding a
    parameter to a member-prefixed local, or preserving a lifted function body
    verbatim by aliasing, is a decomposition failure. The test is whether the
    extracted owner's body reads as code written for its new owner.
  - **New rule — Capability Slice Ownership**: a set of reference-carrying view
    structs is judged as one surface. If any single operation receives every
    slice, the split is nominal and the operation must be decomposed or the
    views deleted. A view whose members are all borrowed concrete owners of one
    named subsystem is legitimate; a view that is a partition of a composition
    root's member list is not.
  - **Governance Review Model**: name the two new inventories as the instrument
    for aggregate and scar review, and restate that inventory output is current
    evidence requiring rulings, never a budget.
  Acceptance: every G0 rule gap is closed by amended text, and each amendment
  states the *question* rather than the banned spelling.

- [ ] **G1b — Propagate the amended rules into the Agentic files that carry
  them.** `AGENTS.md` is the contract, but five other files are where an agent
  actually meets it. Amend each so the rule is present at the point of use:
  - `Agentic/Skills/rubber-duck/SKILL.md` — add aggregate ownership and
    extraction-scar rows to its review criteria and Output Shape, so a
    `[Blocking]` finding is the defined response to an authority-free aggregate.
  - `Agentic/Skills/carmack-test/SKILL.md` — add the same as explicit **Hard
    Checks** rows, since that section is where its non-negotiables live.
  - `Agentic/Skills/orchestrator/SKILL.md` — require the end-of-plan review pass
    to run both G2 inventories and cite their output, so the review is evidence-
    backed rather than impression-backed.
  - `Agentic/Reference/code-style-guide.md` — state the single-member and
    unpacked-at-entry tests where authors choose between a struct and a wider
    signature. Note that `Agentic/Skills/collapse_params.py` is a line-layout
    formatter only and must never be read as authority to collapse parameters
    into a type; its name invites exactly that misreading.
  - `Agentic/Reference/skullbonez-core-class-structure.md:80` — align its
    service-bag/callback-pack sentence with the amended `AGENTS.md` wording so
    the two documents cannot drift.
  Acceptance: an agent reading only the skill it was invoked with encounters the
  amended rule; no file still describes the retired shape-only test. The
  `Related:` pointers in every touched file resolve under
  `tools/check_related_paths.py`.

- [ ] **G2 — Build the two repeatable inventories.**
  Add `tools/inventory_authority_free_aggregates.py`, modelled on
  `tools/inventory_wide_signatures.py`: it reports every aggregate under the
  configured source roots with member count, construction sites, consumer sites,
  and whether the sole consumer destructures without retaining. Add
  `tools/inventory_extraction_scars.py`: it reports local declarations whose
  identifier matches the member-naming convention (`m_*`) and any local that
  aliases a parameter without transforming it. Both take a JSON ruling file
  (`tools/aggregate_ownership_rulings.json`) carrying owner, verdict
  (`retain` / `retain-prior` / `repair`), and reason per row, seeded from PB0's
  retain rulings and G0's findings. Both support `--self-test` against planted
  positive and negative fixtures. Acceptance: `--self-test` passes; a fresh run
  reproduces G0's census exactly; an unruled row is reported as unruled rather
  than as a failure count.

- [ ] **G3 — Wire the inventories into the gates.**
  `tools/validate_fast.bat` runs both `--self-test` invocations and both
  repository scans; an *unruled* row fails the gate, a *ruled* row does not.
  Add the mechanical subset that genuinely belongs in
  `tools/check_dependency_graph.py` `content_rules`: member-prefixed locals and
  single-member aggregates, each with planted positive/negative fixtures per the
  existing validator contract. Update `tools/README.md`, the `AGENTS.md`
  **After Editing** table, and the **File To Validation Mapping** table with
  rows for `tools/inventory_authority_free_aggregates.py`,
  `tools/inventory_extraction_scars.py`, and
  `tools/aggregate_ownership_rulings.json`. Acceptance:
  `tools\validate_dependency_graph.bat` and `tools\validate_fast.bat` pass with
  the new fixtures; deleting a fixture's guard makes the matching gate fail.

- [ ] **G4 — Reconcile, review, and hand off.**
  Rerun both inventories at final source. Confirm every row carries a ruling and
  that the twelve sibling plans' scopes together cover every `repair` row — a
  `repair` row with no owning plan is a registration defect and must be added to
  the correct sibling plan in this commit. Obtain one independent governance
  review answering: can each amended rule be satisfied by renaming? If yes for
  any rule, the amendment is inadequate and G1 reopens. Complete the comment
  audit for every touched tool, header, and Agentic file. Acceptance: independent review
  records zero rename-satisfiable rules; `tools\validate_fast.bat` and
  `tools\validate_all_cpu_tests.bat` pass; the closure report lists the final
  ruling table and the plan-to-row coverage map.

## Dependencies And Decisions

- Runs first in the round-5 campaign. Every sibling plan's closure review cites
  the G1 rule text as its acceptance test.
- G2's ruling file is the shared artifact the sibling plans update as they close
  rows. A sibling plan that deletes a `repair` row removes it from the ruling
  file in the same commit.
- Owner decision taken at registration: `MAX_SCENE_OBJECTS` stays as an absolute
  ceiling (see `scene-sized-store-capacity`), so the Runtime Static Allocation
  Policy amendment for scene-sized runtime capacity belongs to that plan's SC1,
  not to G1. G1 must not pre-empt it.
- Open decision for the owner, recorded not assumed: whether the
  Capability Slice Ownership rule should also apply to `Rendering` and `UI`
  view structs, or only to Runtime composition-root slices. G0 reports the
  affected sites in both layers; G1 implements the narrower Runtime-only rule
  unless the owner widens it.

## Acceptance

- Every G0-identified rule gap has amended `AGENTS.md` text stating an ownership
  question.
- Both inventories are repeatable, self-tested, gate-wired, and documented.
- Both independent-review skills and the orchestrator skill state the aggregate
  and extraction-scar criteria explicitly; no Agentic file retains the shape-only
  test.
- No frozen count, budget, or ratchet is introduced anywhere.
- Independent review confirms no amended rule can be satisfied by renaming.
- Every `repair` row is owned by exactly one sibling plan.

## Validation

Documentation and tool changes only; no engine source behavior changes.

- `tools\validate_fast.bat` (includes `validate_tests`) — required, because
  `tools/*` changed.
- `python tools\inventory_authority_free_aggregates.py --self-test`
- `python tools\inventory_extraction_scars.py --self-test`
- `tools\validate_dependency_graph.bat` — required, because
  `check_dependency_graph.py` and its rule data changed.
- `tools\validate_all_cpu_tests.bat` — required at the closure gate.
