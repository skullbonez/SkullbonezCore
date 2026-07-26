# New Aggregate Ruling Gate

Date: 2026-07-27
Status: NOT STARTED — registered in `MASTER-PLAN.md` on 2026-07-27 as plan 14 of
the Architecture Follow-Up Campaign Round 5, from the same-day owner review of
what the campaign does and does not close. 0/3 phases complete.
Impact area: `tools/inventory_authority_free_aggregates.py`,
`tools/aggregate_ownership_rulings.json`, `tools/validate_fast.bat`,
`tools/README.md`, `AGENTS.md`
Owner: governance
Priority: High relative to size. Roughly an hour of work that decides whether
this campaign is a cleanup or a fix.

## Problem And Evidence (measured 2026-07-27)

`governance-shape-to-judgment-conversion` G2/G3 gate two structural signals that
are decidable from a declaration alone: `single-member` and `empty`. The
"sole consumer destructures every member at entry" test was deliberately left
ungated because separating a construction from a same-named local is not
decidable lexically without a compiler database, and an unreliable gate is worse
than none.

The consequence is that the shape which caused every problem in this
repository's history — the four-to-seven-member context bag — is unguarded.
Measured directly against the shipped tool on 2026-07-27:

```
FooFrameContext: members=7 stated_invariant=False GATING=[]
```

A brand-new seven-reference `*Context` aggregate passes `validate_fast` silently.
It appears only in the informational "Review context" list, which fails nothing.

So the campaign as registered will delete the roughly 40 authority-free
aggregates that exist and leave number 41 unopposed. `ceremonial-aggregate-elimination`
CA0 censuses and rules the current 94 candidates, but nothing makes that census
permanent: once CA4 closes, a new bag is again invisible to everything except a
reviewer who happens to look.

Supporting measurement from the same tool run: of 94 candidates, **zero state a
per-type `Invariant:` block**. The property that distinguishes a legitimate
aggregate from a bag is, today, asserted by nobody anywhere in the tree.

## Concurrency Note — Read This First

`tools/inventory_authority_free_aggregates.py` was under uncommitted concurrent
edit by the `governance-shape-to-judgment-conversion` G4 runner when this plan was
registered (2026-07-27, tip `28b65d92`). That edit moves **discovery** from
suffix-matched to suffix-free: `TYPE_RE` matches every `struct` and `class`, and
the legacy suffix list is demoted to ranking review context only. It also
references a G4 closure report that did not yet exist.

Consequences for this plan, which NA0 must handle rather than assume away:

- **Every figure below is stale on arrival.** "94 candidates" and "roughly 86
  unruled" were measured against suffix-matched discovery. Under suffix-free
  discovery the discovered set is far larger and includes many types that are not
  bags at all. NA0 re-measures first and this plan's numbers are historical
  context only.
- **The ruling requirement must not follow discovery.** Requiring an owner ruling
  for every discovered `struct` would demand rows for `PhysicsBodyRecord`,
  `Vector3`-adjacent values, and every store row in the engine. That is the
  ceremony failure this campaign exists to stop, and it would bury the signal.
  Discovery stays wide for review ranking; the **gated** set stays bounded.
- Reconcile against whatever the tool actually is when NA0 starts, not against
  this document. If G4 has already added a stricter gate, NA0's job shrinks to
  whatever remains uncovered — say so and reduce the phase rather than duplicating
  it.

## Goal

Every aggregate in the bounded gated set carries an owner ruling, so a new one
cannot enter the tree without somebody writing down why it exists. The gate needs
no compiler database, no git history, and no count threshold.

## Design Decision Recorded At Registration

The owner-discussed approach was a **diff-aware** gate: detect newly added
aggregates by comparing against a merge-base. This plan deliberately does not do
that, and the reason is recorded so it is not revisited by accident.

A diff gate needs reliable history at gate time. `validate_fast` runs in hosted
CI where shallow clones and merge-group refs make a merge-base either absent or
wrong, and `check_staged_file_sizes.py` already carries a
`SKORE_SIZE_DIFF_BASE` workaround for exactly that fragility. A gate whose
correctness depends on clone depth will eventually pass something it should have
blocked, silently.

**The ruling file is the baseline instead.** Every member of the bounded gated
set (NA0 defines it) must have a row in
`tools/aggregate_ownership_rulings.json`. A gated name with no row fails, whether
it is new or merely uncensused. This needs no history, protects from the moment it
is armed, and reuses the census work CA0 must do anyway rather than duplicating
it.

## Non-Goals

- No frozen count, ratio, member budget, or "no more than N aggregates" gate. The
  Governance Review Model prohibition stands. A ruling row records a judgement;
  it is never an allowance and never a threshold.
- No gating of the destructured-at-entry test. That limitation is unchanged and
  correct: it stays an `AGENTS.md` review question, and the lexical site counts
  stay review context.
- No widening of the **gated** set to every discovered `struct`. Discovery may be
  suffix-free and wide; the ruling requirement must stay bounded, or the ruling
  file becomes a second copy of the type system and the signal is buried. That
  would be the ceremony failure this campaign exists to stop.
- No behavior change in engine source. This plan touches tooling, rule data, and
  governance documents only.
- No relaxation of the existing `single-member`/`empty` gate. This plan widens
  what must be ruled, never what may pass.

## Phases

- [ ] **NA0 — Define the gated set, then add strict mode and the bounded
  transition verdict.**
  Re-measure first, against the tool as it actually exists (see Concurrency Note).
  Then define the **gated set** explicitly in the tool and in its header, bounded
  so it cannot expand to every data record: a discovered type is gated when it
  carries a legacy suffix family name **and** states no invariant of its own.
  Suffix-free discovery continues to feed the wider review-context list, which
  gates nothing.

  State the residual honestly in the tool header and in NA1's rule text: a
  name-scoped gate is evadable by naming a new bag `FooFrameData`. This plan
  closes the named hole and shrinks the evasion surface; it does not eliminate it,
  and the review question in `AGENTS.md` remains the only thing that catches a
  deliberately renamed bag. Do not claim otherwise in the closure report.

  Then, within the gated set: require **every** member to carry a ruling row, not
  only those raising a structural signal. Keep the existing signal reporting so a
  single-member aggregate is still called out specifically.

  Seed every gated row that lacks a ruling (roughly 86 under the pre-G4
  suffix-matched measurement; re-count before starting). Rows whose
  verdict is already established by a named earlier plan take `retain-prior`
  with that plan cited — PB0's Explicit Retain Rulings and GV1's census cover a
  substantial share and must be derived, not re-decided. The genuine remainder
  takes a new transitional verdict `pre-existing-unreviewed`, which **passes the
  gate but is counted and printed on every run**.

  That verdict is an allowance shape, and this plan states it as one rather than
  disguising it, per the Migration Cleanup Review Rule:
  - **Owner:** `ceremonial-aggregate-elimination` CA0.
  - **Reason:** arming the gate must not break the build for 86 rows whose
    census is already scheduled.
  - **Deletion condition:** CA0 replaces every `pre-existing-unreviewed` row
    with a real verdict; NA2 then removes the verdict from the accepted set so it
    cannot be reused.
  - **Evidence:** the printed count, which must fall monotonically and reach
    zero.

  Anti-gaming: every `pre-existing-unreviewed` row must carry the `site` field,
  and strict mode verifies that the recorded declaration site still resolves to
  that aggregate. Adding a new bag under this verdict therefore requires a
  fabricated site that a reviewer can see. State plainly in the tool header that
  this is a visibility measure, not a proof.

  Acceptance: `--self-test` gains fixtures proving that an unruled multi-member
  candidate fails strict mode, that a `retain`/`retain-prior` row passes, that a
  `pre-existing-unreviewed` row passes while incrementing the reported count, and
  that a `pre-existing-unreviewed` row whose site no longer resolves fails.
  Removing any fixture guard must fail the self-test.

- [ ] **NA1 — Arm the gate and state the rule.**
  Wire strict mode into `tools/validate_fast.bat` step `[4/8]`, replacing the
  current non-strict repository scan. Update `tools/README.md` and the
  `AGENTS.md` File To Validation Mapping row. Add the rule to the
  Invariant Ownership Rule's "The Test Is Ownership, Not Spelling" subsection: an
  aggregate in a candidate suffix family requires a ruling row before it can
  land, and the row must give a reason a reviewer can disagree with — "carries
  data for the frame packet" is not one.

  Prove the gate blocks the real case: add the exact seven-member
  `FooFrameContext` from the Problem section to a scratch header, confirm
  `validate_fast` fails, then remove it. Record the command and the failure text
  in the closure evidence — a gate nobody has watched fail is not known to work.

  Acceptance: `validate_fast` passes at final source with the
  `pre-existing-unreviewed` count printed; the planted seven-member aggregate
  fails the gate with a message naming the aggregate and the required action;
  `python tools\inventory_authority_free_aggregates.py --self-test` passes.

- [ ] **NA2 — Retire the transition and hand off.**
  Runs after `ceremonial-aggregate-elimination` CA4. Confirm the
  `pre-existing-unreviewed` count is zero, then remove the verdict from the
  accepted set in the tool and from the schema documentation in the ruling file,
  so an unruled aggregate has no passing verdict available. Re-verify that
  planting a new aggregate fails.

  Obtain one independent review asking: can a new aggregate still enter the tree
  without a ruling; is any surviving ruling reason a restatement of the type's
  own name rather than a rule a reviewer could dispute; and did the gate acquire
  a count threshold anywhere. Acceptance: review clear; zero
  `pre-existing-unreviewed` rows; the verdict is unusable;
  `tools\validate_fast.bat` and `tools\validate_all_cpu_tests.bat` pass.

## Dependencies And Decisions

- NA0 and NA1 have no dependency and can run immediately, including before
  `ceremonial-aggregate-elimination` starts. That is the point: the gate should
  protect during the campaign, not after it.
- NA2 depends on `ceremonial-aggregate-elimination` CA4. If CA4 has not closed,
  NA2 stays unchecked with the reason recorded and the plan sits at 2/3. Do not
  retire the transitional verdict early and do not force the count to zero by
  editing verdicts.
- Sequencing note for the plan runner: `ceremonial-aggregate-elimination` CA0
  should read this plan before censusing, because CA0's output is now the input
  to a permanent gate rather than a one-time report. CA0 rows therefore need
  reasons written for a future reader, not for the immediate deletion.
- Owner-overridable default, agent does not stop: whether the candidate suffix
  families should also include `View`. `RuntimeRenderBackendView` and the four
  frame views are the campaign's two largest slice findings and neither would be
  caught by the current families. Default is **not** to add `View` in this plan,
  because plans 5 and 6 delete those specific types and a family that matches
  every legitimate read-only value view would flood the ruling file. Revisit once
  those two plans close.

## Acceptance

- Every member of the bounded gated set carries a ruling row; a gated name with
  no row fails `validate_fast`.
- The gated set is defined in one place, stated in the tool header, and its
  name-scoping residual is recorded rather than overclaimed.
- The gate has been observed failing on a planted seven-member aggregate, with
  the command and output recorded.
- No count, ratio, or budget was introduced.
- The transitional verdict is deleted and unusable, with the count proven zero
  from tool output rather than asserted.

## Validation

Tooling and governance documents only; no engine source changes.

- `python tools\inventory_authority_free_aggregates.py --self-test`
- `python tools\inventory_authority_free_aggregates.py --repo .` in strict mode
- `tools\validate_fast.bat` — required, because `tools/*` changed; includes
  `validate_tests`
- `tools\validate_all_cpu_tests.bat` — required at the NA2 closure gate
