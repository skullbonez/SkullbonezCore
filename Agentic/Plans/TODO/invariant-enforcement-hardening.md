# Invariant Enforcement And Assertion Hardening

Date: 2026-08-20
Status: Active; 0/8 phases complete
Impact area: repository-wide invariant enforcement, assertion policy, failure
lanes, tests, comment taxonomy, and substantial diagnostic tools
Owner: subsystem owners named by each affected path
Priority: Active final queue item; `REST_STABILITY` is complete
Commit name: `INVARIANT_HARDENING`

## Goal

Close every high-, medium-, and low-severity finding from the 2026-08-19
repository invariant audit. The result must make each audited invariant honest
about how it is enforced in Debug, Profile, and Release builds. It must add
missing runtime enforcement where violating the contract could otherwise cause
undefined behavior, memory corruption, or silently invalid state, while
avoiding ceremonial assertions that merely duplicate a checked failure path.

This is not a requirement to pair every `Invariant:` comment with an `assert`.
An invariant may instead be proved by construction, the type system, a bounded
container, a checked Lane R return, an owned `SB_FATAL`, or a focused test. The
plan is complete only when every finding has an explicit enforcement lane and
negative proof that fails when the contract is broken.

## Registration And Ordering

- The master ledger activates this plan after `REST_STABILITY` closure; IH0 is
  the binding next phase.
- The eight phases are portfolio tasks IH0 through IH7. Their progress is
  reported under commit token `INVARIANT_HARDENING`.
- Orbital forecast and rest stability are complete. Rebase the audit inventory
  on their final source before IH0 is checked.

## Enforcement Policy

Every reviewed invariant and assertion site receives exactly one primary
classification:

| Lane | Use | Required behavior |
|---|---|---|
| Construction/type proof | Invalid state cannot be represented or reached | Record the constructor, type, fixed-capacity owner, or call order that proves it and add a focused test when the proof is non-obvious. |
| Debug tripwire | Internal programmer error whose Release path is already defined and safe | Keep or add `assert`, prove the non-Debug path cannot perform an unchecked access or continue with corrupt state, and exercise the safe path. |
| Lane R | Recoverable authored, file, user, device, or external-input failure | Validate before mutation or allocation and return the repository-owned diagnostic/result value. |
| Owned fatal | Unrecoverable internal contract breach | Check in all configurations with the owning fatal mechanism and prove it in a subprocess/death test so the main test process survives. |
| Lane P | Diagnostic or test-only probe | Keep production behavior unchanged; label the probe and never present it as runtime enforcement. |

Additional rules:

- Profile and Release define `NDEBUG`; a plain `assert` is never the sole guard
  before pointer dereference, array indexing, division, state mutation, or
  capacity overflow.
- Do not add `assert(condition)` immediately beside an equivalent `SB_FATAL`
  branch. The always-on owned failure is the enforcement; duplicate checks add
  noise and can drift.
- Validate external values before construction. A constructor that divides by
  or sizes storage from an unchecked value is already too late.
- Preserve fixed-capacity and no-steady-runtime-growth policies. An assertion
  must not disguise an allocation path or grant a new replay growth privilege.
- Assertion and invariant counts are measurements, never budgets or ratchets.
  Do not add a frozen count, percentage, or spelling gate.
- Apply `Agentic/Reference/comment-style-guide.md` to every touched source file.
  A comment must name the owner and proof, not promise safety that only exists
  in Debug.

## Audit Baseline

The completed read-only audit recorded the following current measurements:

- 835 tracked source-bearing files were inventoried.
- 819 learning headers contained an `Invariants` section, with 2,686 header
  bullets; 792 nearby `Invariant:` blocks existed, 696 in production source.
- Production source contained 132 plain `assert` calls, 66 `static_assert`
  calls, and 468 `SB_FATAL` calls.
- 63 plain-assert sites had no nearby fatal, checked return, abort, or explicit
  conditional fallback and therefore require owner triage. This number is a
  worklist measurement, not a permitted maximum.
- The authority-free aggregate and glossary inventories passed. Transient
  related-path failures belonged to the active, untracked orbital-forecast
  work and are not defects assigned to this plan.

IH0 must regenerate these inventories after the two earlier plans close. The
regenerated, exact path-and-line disposition table becomes the closure source
of truth; the measurements above must not be copied into a policy threshold.

## Findings To Close

### High Severity

1. `SkullbonezSource/Physics/ColliderStore.cpp` uses assert-only shape-index
   bounds in `RebindShapeReferences` before unchecked vector indexing. Replace
   the Release-unsafe path with owner-appropriate always-on enforcement and
   cover every sphere, box, and hull branch plus corrupt-index death cases.
2. `SkullbonezSource/Rendering/PrimitiveBatchRenderer.cpp` relies on retained
   renderer/active-scope assertions in `PrimitiveBatchScope` and related draw
   paths. Establish whether scope misuse is an unrecoverable renderer contract
   or a safe no-op result, enforce it outside Debug, and test visible, shadow,
   inactive, moved, and nested/mismatched-resource cases.
3. `SkullbonezSource/Runtime/Render/RenderResourceLifecycle.cpp` asserts preview
   snapshot capacity immediately before indexing. Make capacity enforcement
   always-on or make the bounded append structurally incapable of overflow;
   prove exact-capacity and one-over-capacity behavior.
4. `SkullbonezSource/Runtime/App/Run.cpp` and its sibling runtime owners expose
   assert-only renderer, terrain, sky, world-resource, input-bridge, and
   interaction-state dereferences. Triage the complete `Run*` logical surface,
   move checks to the concrete lifecycle owner, and give Release either a safe
   Lane R result or an owned fatal before dereference.
5. `SkullbonezSource/World/Terrain.cpp` constructs terrain before validating
   `mapSize` and `stepSize`. The constructor divides by `stepSize`, and floor-
   sized post storage disagrees with the ceiling-shaped translation loops for
   non-divisible dimensions. Validate positive, divisible dimensions before
   construction through Lane R, use one checked post-count computation, and
   prove zero, negative, undersized, non-divisible, exact-minimum, and normal
   height maps without out-of-bounds writes.

### Medium Severity

1. Re-triage all 63 assert-only candidates after active work lands. Each row
   must record path, symbol, claimed invariant, Debug behavior, Profile/Release
   behavior, primary enforcement lane, owner, focused proof, and disposition.
   No row may close as “assert added” without defining non-Debug behavior.
2. Review the 45 heuristic `Invariant:` candidates that describe allocation,
   reserve, growth, or capacity policy. Split mechanical safety from runtime
   allocation policy where both are present. Confirmed starting points include
   `SkullbonezSource/Assets/TextureCollection.cpp`,
   `SkullbonezSource/Gameplay/TornadoField.h`,
   `SkullbonezSource/Runtime/Prediction/ReplayPredictionPresentation.cpp`, and
   `SkullbonezSource/Runtime/Replay/ReplayAuthoringPackets.h`.
3. For every touched fixed-capacity owner, test exact capacity and overflow.
   For every lifecycle pointer, test access before initialization, during the
   valid phase, and after teardown. For every identity/index contract, test a
   stale or corrupt value through a bounded death-test harness.

### Low Severity

1. Reclassify the worker-dispatch threshold comment near
   `SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp:187` as `Why:`;
   it explains a performance choice rather than a correctness invariant.
2. Add compliant learning headers and local ownership/hazard notes where
   warranted to these substantial tools:
   `Agentic/Skills/collapse_params.py`, `Agentic/Skills/loc_count.py`,
   `Agentic/Skills/skore-cpu-profiler/analyze_markers.py`,
   `Agentic/Skills/skore-cpu-profiler/cleanup_markers.py`,
   `Agentic/Skills/skore-render-test/analyze_perf.py`, and
   `Agentic/Skills/skore-render-test/perf_compare.py`.
3. Correct stale or vague invariant prose encountered in the exact IH0
   worklist, but do not expand this into an unrelated repository-wide prose
   rewrite. New findings are appended to the disposition table before repair.

## Required Work Products

IH0 creates and maintains, inside this plan, an exact checkbox table for every
source-bearing file selected by the regenerated assertion and invariant
inventories. Each checkbox names the file and applicable site IDs. Because this
is a targeted enforcement campaign based on a repository-wide inventory—not a
full comment-pass rewrite—files with no finding are recorded by the inventory
artifact and are not silently added to the edit checklist. Any expansion to a
subsystem/full-repository comment pass must first list every tracked source file
in that expanded scope as required by the Comment Quality Gate.

The plan also retains these reviewable artifacts under the normal validation
output location rather than committing generated reports:

- regenerated invariant-comment and assertion-site inventories;
- the exact 63-candidate successor disposition table;
- negative-control output for each new always-on guard;
- subprocess/death-test results for owned fatal paths;
- before/after assertion classification summary with no count target;
- independent ownership and false-pass review notes.

## Phases

- [ ] **IH0 — Rebase the exact inventory and freeze the worklist.** After
  `REST_STABILITY` closes, use `git ls-files` to regenerate tracked source
  scope, enumerate plain assertions and invariant claims, remove false
  positives only with written reasons, and append the exact per-file checkbox
  and per-site disposition tables to this plan. Record overlapping active-plan
  edits as reconciliations, not omissions. No production edit begins until
  every high/medium/low finding above has a site ID, owner, enforcement lane,
  and intended proof.

- [ ] **IH1 — Repair terrain construction and sizing first.** Move validation
  ahead of every terrain factory construction; centralize checked post/cell
  count arithmetic; reject invalid/non-divisible dimensions through Lane R
  without partial mutation; add boundary, malformed-input, overflow, and
  normal-map tests. Run sanitizers or the repository's equivalent memory-safety
  proof against the formerly mismatched `mapSize = 5`, `stepSize = 2` shape.

- [ ] **IH2 — Harden Core, Maths, Assets, and Gameplay findings.** Adjudicate
  every IH0 site in these layers, preserve dependency direction, distinguish
  authored-input Lane R failures from internal owned fatals, and prove fixed-
  capacity texture/tornado boundaries. Do not introduce an upward include,
  service bag, callback pack, or new retained state owner.

- [ ] **IH3 — Harden Physics findings.** Repair all `ColliderStore` index,
  identity, compaction, and body-binding assert-only paths identified by IH0;
  review the remaining Physics assertions by complete owner rather than line in
  isolation. Add positive branch coverage and subprocess negative tests while
  preserving hot-store layout, deterministic ordering, and the no-new-body-
  field rule.

- [ ] **IH4 — Harden Rendering, DX12, World, and Scene findings.** Repair
  primitive-batch scope/lifetime checks, render-resource preview capacity, and
  all other selected sites in these layers. Exercise construction, valid use,
  move/teardown, exact-capacity, and misuse paths. Preserve feature-neutral
  Rendering vocabulary and dependency boundaries; do not hide an error behind
  a silent renderer state mutation.

- [ ] **IH5 — Harden Runtime, Input, Interaction, Replay, Planning, and UI
  findings.** Review `Run` as a logical surface with its sibling owners, move
  lifecycle enforcement to the concrete owner, and close every selected
  runtime assertion without creating a second retained state owner or a broad
  context/capability bag. Recheck Replay growth privileges and Runtime package
  direction. Coordinate with the post-forecast source layout captured by IH0.

- [ ] **IH6 — Correct invariant taxonomy and tool documentation.** Complete
  the 45-candidate allocation-policy review, reclassify the dispatch-threshold
  `Why:`, add the six tool learning headers, resolve every checked-file comment
  audit finding, run glossary and related-path checks, and reconcile the exact
  checklist. Comment-only edits remain separate from behavioral fixes where
  practical so review can distinguish prose from enforcement.

- [ ] **IH7 — Integrated proof, independent review, and closure.** Run the
  validation map, repeat the assertion/invariant inventories, reconcile every
  disposition and file checkbox, and obtain independent reviews for ownership,
  dependency direction, fatal-test false passes, and Release safety. Reopen the
  owning phase for any unchecked dereference/index/division, duplicate
  enforcement, stale comment, unproved failure lane, or asserted invariant
  whose non-Debug behavior remains undefined. Report checked/deferred counts;
  closure requires zero unexplained and zero silently deferred findings.

## Test And Failure-Proof Matrix

| Contract | Positive proof | Required negative proof |
|---|---|---|
| Terrain dimensions | Valid exact-divisible maps build identical topology | Zero/negative step, undersized/non-divisible map, arithmetic overflow, and former 5/2 mismatch return Lane R before construction |
| Collider shape rebinding | Sphere, box, and hull identities rebind after legal copy/move/compaction | Corrupt or stale index terminates through the owned fatal in a subprocess before indexing |
| Primitive batch scope | Visible and shadow scopes draw through valid retained resources | Inactive, moved-from, wrong-mode, and mismatched-resource use follows the documented always-on lane |
| Preview snapshot capacity | Empty, partial, and exact-capacity snapshots publish correctly | One-over-capacity cannot index storage and produces the selected owned failure |
| Runtime lifecycle pointers | Access during the declared initialized phase succeeds | Before-init and after-teardown access returns Lane R or terminates before dereference |
| Fixed-capacity/runtime allocation policy | Reserve/build phase reaches exact capacity without steady growth | Overflow or forbidden steady growth is rejected by the named owner; no new Replay privilege appears |
| Comment taxonomy | Each `Invariant:`, `Why:`, and runtime-allocation note matches its semantic role | Audit fixture or manual negative control demonstrates that misleading classifications are detected |

Fatal-path tests must launch a dedicated child executable or subprocess and
assert the exit/status/diagnostic contract. A test that invokes `SB_FATAL` in
the main suite process is not acceptable. Each negative control must be run
once with the guard intentionally disabled or the old implementation retained
in a test fixture, proving that the test would have caught the defect.

## Validation Map

Do not run repository validation scripts while iterating. Select focused unit
or subprocess tests per phase and defer the repository gates until the work is
PR-bound. IH7 runs, at minimum:

```powershell
tools\validate_fast.bat
tools\validate_cpu.bat
tools\validate_full.bat
tools\validate_hosted.bat
tools\validate_dependency_graph.bat
python tools/inventory_authority_free_aggregates.py --check
python tools/inventory_glossary_terms.py --check
python tools/inventory_wide_signatures.py --check
python tools/inventory_extraction_scars.py --check
python tools/inventory_function_complexity.py --check
```

Add the owner-specific Physics, Rendering/DX12, Runtime, and tool-test commands
selected during IH0. A pre-existing baseline mismatch is reported with its
artifact and owner; it is not hidden by refreshing a golden. Any behavior
change introduced by this plan requires the applicable validation even if the
associated comment edit is documentation-only.

## Acceptance Criteria

- Every high-, medium-, and low-severity finding has a checked site/file row,
  named owner, enforcement lane, and focused positive/negative proof.
- No plain assertion is the only barrier before an unsafe operation in Profile
  or Release among the regenerated candidate set.
- Terrain rejects invalid dimensions before construction and its allocation and
  traversal counts use one proved formula.
- Collider rebinding, primitive-batch scope, preview capacity, and `Run`-
  surface lifecycle failures are enforced outside Debug.
- Allocation/growth comments use the runtime-allocation taxonomy, correctness
  claims remain `Invariant:`, and the dispatch threshold is `Why:`.
- The six named tool scripts satisfy the comment standard without claiming
  production invariants they do not own.
- Dependency, growth-privilege, hot-store-field, aggregate, signature,
  extraction-scar, complexity, reachability, glossary, and related-path rules
  remain green or have an already-authorized owner disposition.
- The final report names this plan path, checked count, deferred count, and all
  remaining unchecked rows. Completion requires zero unexplained/deferred
  audit findings and an independent review with no blocking ownership finding.

## Non-Goals

- Adding one assert for every invariant comment.
- Replacing recoverable diagnostics with process termination.
- Converting assertions into exceptions or inventing a second result type.
- Creating frozen assertion/comment counts, historical-debt budgets, or a new
  regex governance checker.
- Broad code cleanup, owner extraction, formatting, or comment rewriting not
  required to enforce an audited contract.
- Adding a Replay reserve registration, Physics hot-store field, Runtime
  package, compatibility alias, forwarding header, callback pack, or service
  bag to make enforcement convenient.

## References

- `AGENTS.md`
- `Agentic/Reference/comment-style-guide.md`
- `Agentic/Reference/code-style-guide.md`
- `Agentic/Skills/comment-style-audit/skill.md`
- `Agentic/Plans/MASTER-PLAN.md`
- `tools/inventory_authority_free_aggregates.py`
- `tools/inventory_glossary_terms.py`
- `tools/inventory_wide_signatures.py`
- `tools/inventory_extraction_scars.py`
- `tools/inventory_function_complexity.py`
