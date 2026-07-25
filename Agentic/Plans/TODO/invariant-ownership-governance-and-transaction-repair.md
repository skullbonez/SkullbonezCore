# Invariant Ownership Governance And Transaction Repair

Date: 2026-07-25

Owner: Governance (`AGENTS.md`) + Runtime/Scene + Runtime/App

State: READY (GV0 may run early as a documentation-only parallel lane; GV1+
sequenced after `downward-domain-bleed-remediation`)

Ledger tasks: 5 (GV0-GV4)

Branch at registration: `nightrunner-25th-JUL-26`

Impact area: repository governance rules, comment-audit skill, scene-load
orchestration, frame-loop call sites, review methodology

Priority: High. The 2026-07-25 architecture review identified a failure mode
the current rules *create*: four individually sound bans (no context bags, no
owner reach-back, no callback packs, a 12-parameter ceiling) intersect to
leave exactly one legal shape for a genuinely wide transaction — N parallel
narrow structs, wide "apply" free functions sitting just under the ceiling,
and inline arbitration helpers whose comments carry the real sequencing
invariant. The complexity is not removed; it is extruded into the only
unbanned shape, and the invariant that makes the operation correct ends up
owned by nobody. This plan amends the governance test itself, then repairs
the flagship offender (the scene-load transaction) and every other censused
offender under the amended rule.

Implementation mode: use `Agentic/Skills/orchestrator/SKILL.md`. This plan
requires one independent rubber-duck review at whole-plan closure.

## Registration

This plan is registered in `Agentic/Plans/MASTER-PLAN.md` as a five-task
active plan appended to the 2026-07-25 round-4 campaign as its fourth entry.
Registration grows the active/future denominator from 19 to 24.

Required plan-runner commit first line:

```text
Invariant Ownership Governance And Transaction Repair, TASK <DONE> / 5, <OVERALL_PERCENT>% OVERALL COMPLETE — <ACTION SUMMARY>
```

With the 24-task ledger and the three prior round-4 plans complete at 19, the
nominal task percentages after GV0-GV4 are 83%, 88%, 92%, 96%, and 100%.
GV0 is authorized to run early (see Dependencies), so compute the actual
percentage from the ledger's done-count at commit time; never reuse the
nominal values if execution interleaves.

## Problem And Measured Evidence

### The extruded shape (flagship offender)

Census of 2026-07-25 on `nightrunner-25th-JUL-26`
(`SkullbonezSource/Runtime/Scene/SceneController.h`, consumers in
`Runtime/App/Run.cpp`, `Runtime/App/RunFrame.cpp`,
`Runtime/App/InputFrameExecution.cpp`, `Runtime/Scene/SceneRequestExecution.cpp`,
`Runtime/Scene/SceneController.Load.cpp`,
`Runtime/Capture/RuntimeStressController.cpp`):

Loading a scene requires the caller to assemble and sequence, in exact order,
with no type enforcing that order:

1. Four input/participant value groups: `SceneLoadRequest`,
   `SceneLoadPolicyInputs`, `SceneLoadInteractionParticipants`,
   `SceneLoadPresentationParticipants`.
2. `SceneController::Load(...)` filling a fifth struct,
   `SceneLoadConsumerOutputs`.
3. `ApplySceneLoadRuntimeReactions(...)` — **11 parameters** — which must run
   before any external presentation.
4. `ApplySceneLoadPresentationOutputs(...)` — **8 parameters** — which must
   run only after step 3 advanced every runtime owner.
5. Three free inline arbitration helpers —
   `SceneNavigationForFollowingRequest`,
   `ScenePresentationForFollowingRequest` — whose sole job is answering
   "which copy of this value is current mid-batch," documented by comments
   warning that follow-up persistence "must not fall back to the stale
   submitted snapshot."

The sequencing and arbitration invariants live in comments and caller
discipline. No type owns them; no test fails when a caller reorders them.
`SceneRequestExecution.cpp` and the stress controller each re-encode the same
ordering by hand.

### Why the rules produced it

- The God-Object Closure Rule bans broad `*Context`/`*Services` bags.
- The UI/Runtime and owner-fanout campaigns ban owner reach-back and callback
  packs.
- The wide-signature campaigns set an owner-accepted 12-parameter ceiling and
  the parameter-bag remediation deleted aggregates that "exist mainly to
  conceal arguments."

Each rule is correct about the anti-pattern it kills. Composed, they cannot
distinguish an **authority-free bag** (data aggregated only to shorten a
signature — correctly banned) from an **invariant-owning object** (a type
whose reason to exist is enforcing ordering/arbitration/lifecycle — the thing
this transaction needs). So both are banned, and the invariant is homeless.

### Known additional suspects (GV1 census must rule each)

- The scene-load *reaction* fan-out re-encoded at multiple call sites
  (`Run.cpp`, `RunFrame.cpp`, `InputFrameExecution.cpp`,
  `RuntimeStressController.cpp`) — same ordering duplicated by hand.
- Any other family of ≥3 sibling structs feeding one operation plus a wide
  free "apply" function plus ordering comments. The census sweep below finds
  them mechanically; no other offender is pre-judged here.
- Explicitly ruled non-offenders unless new evidence appears: the `Run` frame
  phase-result values (stack-only, ruled in `run-execute-frame-phase`
  closure), `ReplayWorkspaceFrameInput` at `TickWorkspace` (retained by the
  B3 owner ruling), `SceneDefaultsSaveView` (single synchronous borrow with
  lifetime comment), and `RuntimeRenderer::FrameEntryContext` (one-frame
  submission record). Prior owner rulings are re-litigated only with new
  census evidence, per standing campaign rules.

## Goal

1. Amend the governance so reviews test for **invariant ownership**, not
   shape: an aggregate type is legitimate if and only if it names and
   enforces an invariant; an aggregate that only carries data remains banned.
2. Repair the scene-load transaction under the amended rule: one
   `SceneLoadTransaction` owner that enforces phase order and owns the
   which-value-wins arbitration, deleting the free-function ceremony.
3. Census and repair every other offender exhibiting the extruded shape, or
   record an explicit retain ruling.
4. Install the safeguards that keep both failure modes out: the old one
   (authority-free bags) and the new one (homeless invariants).

## Non-Goals

- No repeal of the context-bag, callback-pack, reach-back, or forwarding
  bans. The amendment sharpens the test; it does not weaken it.
- No change to the owner-accepted 12-parameter ceiling.
- No behavioral change to scene loading, request batching, reset, create,
  or defaults-save: identical observable sequencing, identical results,
  identical logs and lifecycle events. This is an ownership repair.
- No baseline, golden, screenshot, replay artifact, scene, config, or
  physics CSV refresh. Divergence is reverted, never normalized.
- No new retained cross-frame state: the transaction owner is stack-scoped
  per batch execution.
- No re-litigation of prior owner rulings without new census evidence.

## Permanent Invariants

1. An aggregate type (struct/class grouping unrelated-owner data or
   orchestrating multi-owner work) is legitimate only when its header names
   the invariant it owns with an `Invariant:` comment and a focused test
   exercises that invariant. An aggregate without an owned invariant is a
   bag and remains banned.
2. An invariant-owning transaction type must not become a service bag: it
   stores values and a phase cursor, never long-lived owner pointers;
   borrowed owners enter through phase-method parameters and are released
   when the call returns.
3. Multi-step operations whose correctness depends on call order must
   enforce that order in a type (phase cursor + lane-F fatal on violation),
   not in comments or caller discipline.
4. When a review finds ≥3 sibling structs plus wide apply free functions
   plus ordering/arbitration comments around one operation, that is the
   extrusion signal: the finding is "this operation needs an invariant
   owner," and a parameter reshuffle is not an accepted remediation.
5. The comment audit fails an aggregate/transaction type whose header lacks
   its named owned invariant.

## Ledger

- [ ] **GV0 — Amend the governance.**

  Documentation-only. Amend `AGENTS.md` and the affected skill docs; no
  source changes.

  Implementation notes for the implementing agent:

  1. In `AGENTS.md`, add a new standing section **"Invariant Ownership
     Rule"** immediately after the God-Object Closure Rule, containing
     permanent invariants 1-4 above, the legitimacy test, and both worked
     examples:
     - *Banned (authority-free bag):* a struct whose fields are consumed by
       immediate unpacking and whose deletion would only widen a signature —
       cite the rejected `RenderModelPassInput` precedent from the 2026-07-23
       parameter-bag remediation.
     - *Legitimate (invariant owner):* a transaction type whose phase cursor
       makes an out-of-order call a lane-F fatal, whose arbitration methods
       replace free helper functions, and whose header `Invariant:` block
       names exactly what it enforces.
     The section must state explicitly: this rule does not relax the
     context-bag, callback, reach-back, or forwarding bans, and it does not
     change the 12-parameter ceiling; an invariant owner passed as one
     parameter is not a ceiling evasion because its legitimacy is
     independently tested by this rule.
  2. In the God-Object Closure Rule and Migration Cleanup Review Rule, add
     one cross-reference sentence each: "distinguish banned bags from
     invariant owners per the Invariant Ownership Rule."
  3. Add the extrusion signal to the Reviews section of `AGENTS.md`:
     reviewers must report the ≥3-sibling-structs + wide-apply +
     ordering-comments pattern as a design finding with a proposed owner,
     and must not accept a parameter reshuffle as its remediation.
  4. Extend `Agentic/Skills/comment-style-audit/skill.md`: for any type that
     aggregates unrelated-owner data or orchestrates multi-owner sequencing,
     the audit requires a header `Invariant:` block naming the owned
     invariant; absence is an audit failure (permanent invariant 5).
  5. Record the ratified ruling in `MASTER-PLAN.md` under Binding Decisions:
     the invariant-ownership test is the standing arbiter between bags and
     owners; the extrusion signal is a mandatory review output.
  6. Do not add any counting, threshold, or spelling ratchet. The rule is a
     qualitative test with named required artifacts (Invariant: block +
     focused test), enforceable by review and comment audit.
  7. **Coordination:** `header-claim-staleness-remediation` HC2 also amends
     `Agentic/Skills/comment-style-audit/skill.md`, adding a
     claim-verification step in a separate section. Whichever task lands
     second rebases onto the other rather than overwriting the file. Note
     also that HC0 corrects `SceneController.h:337-338`, which currently
     cites completed task "C1" as live context — GV1's census must read the
     corrected header, not the stale claim.

  Acceptance:

  - `AGENTS.md` carries the new rule, both worked examples, the two
    cross-references, and the extrusion-signal review requirement.
  - The comment-audit skill carries the aggregate-invariant check.
  - `MASTER-PLAN.md` Binding Decisions carries the ruling.
  - Diff is documentation-only; no repository validation required.

- [ ] **GV1 — Ratify the offender census.**

  From the implementation tip, sweep for the extruded shape and produce a
  ruled disposition table (repair / retain-with-ruling / out-of-scope) with
  file:line evidence per row.

  Implementation notes:

  1. Mechanical sweeps (starting points, not the complete method):
     - free functions and static methods with ≥8 parameters:
       `rg -nU '\w+\s*\([^;{)]*(,[^;{)]*){7,}\)\s*[;{]' SkullbonezSource --type-add 'src:*.{h,cpp}' -t src`
       then hand-filter declarations;
     - sibling struct families feeding one operation: `rg -n 'struct \w+(Inputs|Participants|Outputs|Reactions)' SkullbonezSource`;
     - homeless arbitration: `rg -n 'inline .*ForFollowingRequest|must (not )?(be called|run) (before|after|only)' SkullbonezSource`;
     - ordering carried by comments: `rg -n 'Call after|Call only after|before any external|must observe' SkullbonezSource`.
  2. For each candidate, answer three questions in the table: what invariant
     makes this operation correct; which type currently owns it (usually
     "none — comments"); did a prior owner ruling already retain this shape
     (link the ruling; retain unless new evidence).
  3. The scene-load transaction is pre-ruled **repair** (GV2). The reaction
     fan-out duplication across `Run.cpp`, `RunFrame.cpp`,
     `InputFrameExecution.cpp`, and `RuntimeStressController.cpp` is part of
     that repair's scope, not a separate row.
  4. Explicitly re-verify the ruled non-offenders listed in the evidence
     section and record "retained under prior ruling, no new evidence" rows
     so the review can check them off rather than re-discover them.
  5. Cap GV3's scope honestly: every repair-now row gets an owner design
     sketch (one paragraph: the invariant, the type that will own it, the
     call sites that simplify). If the census finds more than three
     repair-now rows beyond scene load, rank them and propose the tail as a
     registered follow-up rather than silently growing this plan.

  Acceptance:

  - The disposition table covers every sweep hit; no candidate is dropped
    without a written reason.
  - Every repair-now row has an invariant statement and an owner sketch.
  - Prior rulings are honored or explicitly challenged with new evidence.
  - No source behavior changes in GV1.

- [ ] **GV2 — Repair the scene-load transaction.**

  Introduce `SceneLoadTransaction` in `Runtime/Scene` as the invariant owner
  for one request-batch execution, and delete the extruded ceremony.

  Implementation notes:

  1. **Extract the phase cursor as a testable value type first.** Add
     `SceneLoadPhaseCursor` (header-only value type in `Runtime/Scene`):
     an enum phase sequence
     `Idle → Load → RuntimeReactions → Presentation → Complete`
     (refine names from the actual code paths during implementation; the
     census in `SceneController.Load.cpp` decides whether GpuDrain/Clear
     phases are externally visible or internal to Load) plus
     `TryAdvance(Phase next) -> bool` with the legality table inside.
     Unit-test the cursor exhaustively as a pure value (legal walk, every
     illegal jump, restart-after-failure semantics). This makes the ordering
     invariant testable without process-level fatal harnesses.
  2. **`SceneLoadTransaction` owns cursor + values, never owners.** Members:
     the request, `SceneLoadConsumerOutputs` (moved inside; it stops being a
     caller-visible output parameter), the lifecycle packet reference or
     copy per existing semantics, and the cursor. It must not store
     `Run`-owned object references — borrowed owners arrive as parameters to
     each phase method and expire at return (permanent invariant 2). Wrong
     phase order is `SB_FATAL` (lane F) in the transaction, proven legal by
     the cursor's unit tests.
  3. **Phase methods.** Shape (adjust to real code, keep the contract):
     - `Load(policy, interaction, presentation)` — wraps the existing
       `SceneController::Load`; cursor `Idle → Load`.
     - `ApplyRuntimeReactions(...)` — absorbs today's 11-parameter free
       function. Prefer decomposing its body into per-owner apply steps
       where the owner already has a lifecycle seam (the reactive owners
       already consume `SceneLifecyclePacket`); where no seam exists, the
       method may keep the bounded parameter list under the 12 ceiling.
       Do not build a `SceneLoadReactionOwners` struct merely to shorten
       this signature — that is the banned bag, and the census-visible test
       is that any such struct would carry no invariant.
     - `ApplyPresentationOutputs(...)` — absorbs the 8-parameter free
       function; cursor enforces it cannot run before reactions.
     - `NavigationForFollowingRequest()` /
       `PresentationForFollowingRequest()` — the two free arbitration
       helpers become const methods that read the internal outputs plus
       lifecycle state; the "stale submitted snapshot" rule moves from a
       comment into the method body, and gains a focused test for the
       load-then-follow-up-request batch case.
  4. **Call-site migration.** `SceneController::ExecutePending` and the
     external sites (`Run.cpp`, `RunFrame.cpp`, `InputFrameExecution.cpp`,
     `SceneRequestExecution.cpp`, `RuntimeStressController.cpp`) construct
     one transaction per batch execution and call phases in order. The
     duplicated hand-sequencing at each site collapses; if two sites need
     different presentation policy (stress controller vs interactive), that
     policy enters as a phase-method argument, not as a divergent call
     order.
  5. **Deletions (symbol-level, aliases do not satisfy):** free
     `ApplySceneLoadRuntimeReactions`, free
     `ApplySceneLoadPresentationOutputs`, inline
     `SceneNavigationForFollowingRequest`, inline
     `ScenePresentationForFollowingRequest`. The four input/participant
     structs may survive where they still group same-owner values with real
     lifetime comments; delete any that the migration leaves as pure
     pass-throughs.
  6. **Header comment.** The transaction's learning header must name the
     owned invariant explicitly (phase order + mid-batch value arbitration)
     — it is the governance rule's first worked example in live code.
  7. **Behavior freeze proof.** Scene load/reset/create/defaults-save flows
     must be observably identical: run the focused scene lifecycle doctests,
     then the mapped gates. Any lifecycle event reorder visible in logs is a
     defect.

  Acceptance:

  - The four deleted symbols return zero `rg` rows; no renamed or wrapped
    equivalent exists.
  - `SceneLoadPhaseCursor` has exhaustive unit coverage; the
    follow-up-request arbitration has a focused test.
  - The transaction stores no owner pointers/references beyond values,
    outputs, and cursor (independent review checks this explicitly).
  - All previous call sites compile through the transaction; no site
    hand-sequences reactions/presentation anymore.
  - `tools\validate_full.bat` passes (Run*/Runtime/* mapping) with no
    baseline motion; focused lifecycle tests pass.

- [ ] **GV3 — Repair the remaining repair-now census rows.**

  Apply the same treatment to every GV1 repair-now row: name the invariant,
  build or assign its owner under permanent invariants 1-2, delete the
  extruded helpers/free functions, migrate call sites, add the invariant
  test, and keep behavior frozen.

  Implementation notes:

  1. Work row-by-row with one commit per row (or per tightly coupled row
     group); each commit names the invariant moved and the symbols deleted.
  2. Reuse `SceneLoadPhaseCursor`'s pattern (value-type cursor + owning
     type + lane-F on violation) where the offender is ordering-shaped;
     reuse the arbitration-method pattern where it is which-value-wins
     shaped. Do not invent a generic "TransactionBase" — each owner is
     concrete, per the no-inheritance policy.
  3. If a row's repair would cross into a package another live plan owns
     (Replay partition, domain bleed), record the dependency and defer that
     row to run after the owning plan instead of creating a merge conflict;
     the deferral is recorded in the plan, not silently dropped.
  4. Validation follows the standing file-to-gate mapping per touched area;
     each row's commit names its gate evidence.

  Acceptance:

  - Every repair-now row is closed with its deletions proven and its
    invariant test added, or carries an explicit recorded deferral with
    owner and condition.
  - No new aggregate lacking a named invariant was introduced anywhere in
    the diff.
  - All mapped gates for touched areas pass with zero refresh.

- [ ] **GV4 — Close governance adoption, behavior, and documentation.**

  Re-run the GV1 sweeps from the final tip. Audit every touched
  source-bearing file against the (GV0-extended) comment-style guide. Run
  one independent rubber-duck review with a two-sided hostile mandate:
  (a) find any surviving homeless invariant in the repaired paths, and
  (b) find any new type that uses "invariant owner" as camouflage for a
  service bag — stored owner pointers, cross-frame retention, or an
  `Invariant:` block that names nothing enforceable. Either finding reopens
  its owning task.

  Acceptance:

  - Final sweeps show no unruled extrusion-shape candidates.
  - Comment audit passes including the new aggregate-invariant check.
  - One independent review with no unresolved finding.
  - `tools\validate_full.bat` passes from final source; zero refresh of any
    baseline, golden, artifact, scene, or config.
  - Closure evidence under `Agentic/Reports/<date>/`; plan deleted under
    inventory rule 4; `MASTER-PLAN.md` and `Agentic/SessionState.md` record
    the handoff.

## Dependencies And Decisions

- **GV0 is an authorized early parallel documentation lane**: it is
  documentation-only, does not touch source, and its amended review rules
  benefit the in-flight round-4 implementation reviews. It may execute any
  time after registration. GV1-GV4 wait for
  `downward-domain-bleed-remediation` to close so this plan's source repairs
  do not collide with the partition/bleed file moves.
- GV2 must not weaken any boundary the round-4 plans establish: the
  transaction stays inside `Runtime/Scene` + call-site packages already
  allowed by the standing Runtime package table; no new package edge is
  needed. If implementation discovers otherwise, stop and request an owner
  ruling.
- The 12-parameter ceiling stays binding. The transaction is not a ceiling
  evasion: its legitimacy is separately proven by the invariant test, and
  any aggregate introduced *without* an owned invariant remains a review
  failure exactly as before.
- Prior owner rulings (frame phase-results, `ReplayWorkspaceFrameInput`,
  `SceneDefaultsSaveView`, `FrameEntryContext`) stand unless GV1 records new
  evidence; the default is retain.
- This plan deliberately performs no validator/regex enforcement for the
  invariant rule: it is qualitative and belongs to review + comment audit.
  Do not add a counting gate.

## Static Closure Proofs

```powershell
rg -n 'ApplySceneLoadRuntimeReactions|ApplySceneLoadPresentationOutputs' SkullbonezSource
rg -n 'SceneNavigationForFollowingRequest|ScenePresentationForFollowingRequest' SkullbonezSource
rg -n 'Invariant Ownership Rule' AGENTS.md
rg -n 'Invariant:' SkullbonezSource/Runtime/Scene/SceneLoadTransaction.h
```

Commands 1-2 must return no rows at closure; commands 3-4 must each return
at least one row (rule present; transaction header names its invariant —
adjust the filename to the implemented spelling).

## Validation Map

| Phase | Iteration evidence | Pre-commit/closure gates |
|---|---|---|
| GV0 | Governance diff review | Documentation-only; no repository validation |
| GV1 | Sweep outputs, disposition table | Documentation-only; no repository validation |
| GV2 | Cursor/arbitration unit tests, lifecycle doctests | `tools\validate_tests.bat`, `tools\validate_full.bat` (Run*/Runtime/* mapping) |
| GV3 | Per-row deletion proofs and invariant tests | Standing file-to-gate mapping per touched row; name each gate in the row's commit |
| GV4 | Final sweeps, comment audit, review record | `tools\validate_full.bat`; plus any row-specific gate re-touched by remediation |

## Closure Evidence Requirements

The closure report must contain:

- the final `AGENTS.md`/skill governance diffs and the Binding Decisions
  entry;
- the complete GV1 disposition table with per-row rulings and evidence;
- the scene-load before/after: deleted symbols, call-site diff summary,
  cursor test inventory, arbitration test inventory;
- per-row GV3 evidence or recorded deferrals;
- gate outputs for every touched area with zero-refresh confirmation;
- touched-source comment-audit inventory including the new
  aggregate-invariant check results;
- the independent review verdict on both hostile mandates and any
  remediation.
