# Core Engine Evidence-Driven Code Reduction Plan

Date: 2026-08-22
Status: Active by owner direction. 1/6 phases complete.
Impact area: `SkullbonezSource/` production code, project metadata for deleted files, and focused behavioral tests
Owner: Engine architecture and the owner of each touched subsystem
Priority: Current execution priority; CR0 through CR5 run in order.

## Owner Direction

Reduce production code by deleting code that has no live production purpose and
by consolidating behavior that is genuinely duplicated. This is not a campaign
to replace working systems, flatten subsystem ownership, or meet a predetermined
line-count target.

The work has four permitted reduction mechanisms:

1. Delete unreferenced declarations, definitions, files, and their obsolete
   tests or project entries after production reachability has been adjudicated.
2. Merge repeated condition blocks when they test the same state, preserve the
   same short-circuit and side-effect ordering, and lead to the same behavior.
3. Deduplicate repeated algorithms, projections, serializers, and state
   transitions when their units, ownership, lifetime, capacity, and failure
   semantics are equivalent.
4. Extract a shared helper only when it replaces duplicated code at two or more
   independent call sites and produces a clear net reduction in production LOC.

The plan deliberately makes no up-front LOC promise. Every accepted batch must
record its additions, deletions, and net `SkullbonezSource/` delta. A rename is
allowed to be LOC-neutral; every code-reduction batch should be deletion-positive.

The owner activated this plan on 2026-08-22. `Agentic/Plans/MASTER-PLAN.md`
registers CR0 through CR5 after the completed 80-task portfolio. Work remains on
the current `codex/cause-hierarchy-ui-first` branch by explicit owner direction;
the clean baseline below is the comparison point for every measured batch.

---

## Product Naming Decision

The built-in UI is the game UI. It includes simulation operation, scene
authoring, the level editor, diagnostics, replay tools, and presentation. It is
not a legacy implementation.

The proposed code name is **GameUI**:

- `DevelopmentUiMode::Legacy` becomes `DevelopmentUiMode::GameUI`.
- `DevelopmentUiModeShowsLegacy` becomes `DevelopmentUiModeShowsGameUI`.
- Local variables, comments, UI labels, automation labels, and validation output
  that use `legacy` specifically for this surface become `gameUi` or `Game UI`.
- The development selector becomes `--dev-ui game|imgui`, with `game` as the
  default. Update tracked callers and fixtures rather than retaining a permanent
  `legacy` compatibility alias. If an external compatibility obligation is found
  during inventory, the owner must rule on that obligation before changing the
  token.

Do not rewrite unrelated uses of `legacy`, such as old replay schemas, rendering
formats, mathematical conventions, or compatibility paths. The rename is scoped
to the built-in game UI surface only.

---

## Protected Systems And Non-Goals

- **Keep ImGui.** It remains an optional development UI while its usefulness is
  evaluated. Do not delete its owners, renderer backend, project entries,
  submodule, command-line selection, or focused tests under this plan.
- **Keep Tracy.** Do not delete its client owner, instrumentation, build flags,
  project entries, submodule, or profiler integration under this plan.
- **Keep the complete Game UI and level editor.** No UI or editor feature is a
  deletion candidate merely because ImGui contains a superficially similar
  panel.
- Do not remove replay-format compatibility, automation behavior, diagnostics,
  or authored-scene behavior merely to reduce LOC.
- Do not turn `Core/` into a shared-helper dumping ground. A helper belongs in
  the lowest honest owner permitted by the dependency rules.
- Do not introduce callback packs, service/context bags, forwarding facades,
  authority-free aggregates, retained state mirrors, or dynamic allocation to
  shorten call sites.
- Do not extract a single-use helper solely to shorten a large function. That
  moves lines without deduplicating responsibility.
- Do not refresh physics, visual, replay, performance, or other golden baselines
  to make a reduction pass.
- Tests, governance, plans, third-party source, and generated files do not count
  toward the reported production-code reduction.

---

## Evidence Standard

Every candidate must be entered in the appropriate phase ledger before editing.
The implementation handoff must leave the ledger populated with exact symbols,
files, evidence, disposition, validation, and net LOC change.

### Unreferenced-code evidence

A missing textual caller is not enough. Before deletion, review:

1. Current Debug and Profile decorated-symbol reachability from
   `tools/inventory_unreachable_symbols.py`.
2. CodeGraph callers and impact for the exact symbol or file.
3. Virtual dispatch, callbacks, exports, registration tables, function pointers,
   compiler-generated use, preprocessor variants, and same-translation-unit
   entry paths.
4. Test-only references: determine whether the test protects a deliberately
   exposed invariant or manufactures reachability for retired surface.
5. Project and filter ownership, documentation links, command-line routes,
   automation manifests, and serialized identifiers.

Delete a symbol only when the owner can name why it has no production invocation
mechanism. Delete obsolete rulings with the code; do not add a retain ruling to
avoid adjudication.

### Duplicate-code evidence

Textual similarity is only a candidate signal. Consolidation requires all of the
following to match or be explicitly parameterized:

- concrete owner and dependency layer;
- input and output meaning, units, coordinate spaces, and sentinels;
- mutation, error handling, logging, and short-circuit ordering;
- storage lifetime, fixed capacity, allocation policy, and thread assumptions;
- determinism, serialization ordering, and validation-sensitive behavior.

If these differ, keep separate code or extract only the genuinely common pure
operation. Do not hide policy differences behind flags in a broad helper.

### LOC accounting

For every implementation batch, record:

- `git diff --numstat -- SkullbonezSource`;
- production lines added, deleted, and net change;
- source files deleted in full;
- test/tool/project-file changes separately, outside the production total;
- the behavior or invariant that remains after consolidation.

Count physical source lines consistently. Do not count submodule contents or use
format-only churn as a reduction.

---

## Phase CR0 - Baseline And Candidate Register

**Goal:** Establish a current, reproducible inventory before selecting code for
deletion or consolidation.

### Tasks

- [x] Record the current branch, commit, dirty files, and source LOC baseline.
      Preserve all pre-existing dirty files as user-owned.
- [x] Confirm the CodeGraph index is current, then use it as the first-pass map
      for symbol callers, callees, and impact.
- [x] Produce current Debug and Profile builds required by the decorated-symbol
      reachability inventory, unless verified current artifacts already exist.
- [x] Run `tools/inventory_unreachable_symbols.py` and classify every candidate
      selected for this plan as delete, retain-owner, or separately owned repair.
- [x] Review `tools/inventory_extraction_scars.py`,
      `tools/inventory_function_complexity.py`, and
      `tools/inventory_wide_signatures.py` as candidate sources, not as proof of
      duplication.
- [x] Search for repeated multi-statement blocks and repeated predicates inside
      each subsystem. Record exact file, function, and line evidence; exclude
      generated and third-party code.
- [x] Populate the three ledgers below before implementation begins.

### CR0 baseline

- Branch: `codex/cause-hierarchy-ui-first` (owner-directed; no branch creation).
- Commit: `cc194f9aacfb4d2d8872f7399df712159717dd27`.
- Dirty files: none. The ignored work ledger and generated `TestOutput/`
  diagnostics are not source inputs.
- Production inventory: 615 tracked `.cpp`, `.h`, `.hpp`, `.inl`, and `.hlsl`
  files under `SkullbonezSource/`, totaling 207,080 physical lines.
- CodeGraph: current, 1,175 indexed files, 36,527 nodes, and 109,726 edges.
- Build evidence: current Debug, Profile, and Automation warnings-as-errors
  builds all pass.
- Structural evidence: reachability reports 93 ruled rows (54 no-reference,
  11 own-TU-only, 28 test-only) and zero blockers; extraction scars report one
  ruled retained row; wide signatures pass every triggered owner ruling.
  Function complexity identifies the playback-status edit to `RunInputPhase`
  as an exact ruling-digest refresh, not a reduction candidate.

### Unreferenced candidate ledger

| Symbol/file | Owner | Reachability evidence | Test-only exposure | Disposition | Expected deletion |
|---|---|---|---|---|---|
| `ApplicationExitState::HasOwnedFailure` | App exit arbitration | Current Debug/Profile objects classify it test-only; CodeGraph/text inspection finds no production caller | `TestApplicationExitState.cpp` proves owned-failure precedence | Retain-owner: narrow observation seam protects the process-exit invariant | 0 |
| `RenderDefaultsStore::PendingTypeAt` | Render defaults request queue | Current objects classify it test-only; production mutation remains behind the bounded queue | `TestOwnerRequestQueues.cpp` proves exact arbitration order | Retain-owner: deleting it would remove focused queue-order evidence | 0 |
| `RenderGraph` enum `ToString` overloads | RenderGraph diagnostics | Same-TU diagnostic dump calls are live despite the cross-TU inventory classification | No test-only manufacture; `RenderGraph.cpp` uses every overload in diagnostics | Retain-owner: live same-TU diagnostic formatting | 0 |

### Duplicate-condition candidate ledger

| Repeated condition/block | Sites | State and ordering equivalence | Intended owner | Disposition |
|---|---|---|---|---|
| Completed-fence observation and publication | `Dx12FrameOwner.cpp:870`, `:911`, `:955`, `:996` | Same fence owner, same readiness sample, same completed value, and same publication order before retirement decisions | `Dx12FrameOwner` | Accept for CR3: one private value-returning predicate helper, preserving each call site's later release policy |
| Exit-latch return block | Seven checkpoints in `Run::Execute` | Predicate and return match, but each checkpoint is a distinct phase boundary and a helper/lambda would not be deletion-positive | App composition root | Reject: retain explicit phase checkpoints |
| Disabled-recorder early return | Presentation, solver, and event recorder paths in `ReplayRecorder.cpp` | Predicate spelling matches; following mutations and concrete recorder ownership differ | Each concrete Replay recorder | Reject: retain separate owner guards |

### Duplicate-implementation candidate ledger

| Repeated operation | Sites | Semantic equivalence | Helper owner | Expected net deletion | Disposition |
|---|---|---|---|---|---|
| Replay sample/checkpoint capacity derivation | `ReplayRecorder.cpp:2556-2567` and `:3294-3305` | Same config type, clamp bounds, tick units, interval floor, and minimum capacity | Replay recorder translation unit | About 8 lines | Accept for CR4: one pure config-value helper used by both recorders |
| `FILE` close deleter | `Core/Config.cpp:65-76`, `Rendering/Text.cpp:77-88`, `World/Terrain.cpp:73-84`, `Runtime/Capture/CaptureSystem.cpp:197-208` | All are cold `FILE*` RAII with the same `fclose` behavior; `unique_ptr` already suppresses null deletion | Each file-I/O owner using the standard-library deleter | About 36 lines | Accept for CR4: replace local structs with `decltype(&fclose)` deleters; do not add a Core helper |
| Resource-lifecycle log forwarding | `ReflectionPass::LogResourceLifecycleStep` and `ShadowPass::LogResourceLifecycleStep` | Both synchronously forward unchanged strings to their borrowed `RenderResourceLifecycleLog` | Each render pass keeps its existing log borrow | About 8 lines | Accept for CR4: call the existing log owner directly and delete the forwarding declarations/definitions |
| Moving-volume collision blocks | `BoundingBox.cpp` and `BoundingSphere.cpp` repeated quadratic fragments | Text is similar, but shape dispatch, contact sentinels, and volume-specific entry contracts require a dedicated physics design review | Physics collision owners | 0 | Reject from this campaign: similarity is not sufficient semantic proof |

### Acceptance

- No production behavior has changed.
- Every selected candidate has exact source evidence and an owner.
- No LOC forecast is presented as committed savings before the candidate design
  identifies the code that will be deleted and the code, if any, that replaces it.

---

## Phase CR1 - Correct The Game UI Surface Name

**Goal:** Correct the misleading name without changing which surface is selected,
its default, its behavior, or its feature set.

### Tasks

- [ ] Inventory every UI-surface use of `Legacy`, including identifiers, comments,
      logs, menu labels, startup parsing, tests, scripts, manuals, and automation.
- [ ] Rename the enum member and predicates to `GameUI`/`ShowsGameUI`.
- [ ] Rename surface-specific locals and fields, such as
      `legacyDevelopmentUiActive`, without touching unrelated compatibility terms.
- [ ] Change user-facing labels from `Legacy UI` to `Game UI`.
- [ ] Change the tracked command-line selector and fixtures from
      `--dev-ui legacy` to `--dev-ui game`, subject to the external-compatibility
      ruling described above.
- [ ] Update comments to state that GameUI owns the built-in game and level-editor
      presentation while ImGui is an optional development surface.
- [ ] Verify that the omitted selector still chooses GameUI and that exclusive
      focus, input capture, visibility, and switching behavior are unchanged.

### Acceptance

- A scoped search finds no remaining use of `legacy` to describe GameUI.
- Unrelated replay, rendering, maths, and compatibility uses remain intact.
- Default launch, explicit GameUI launch, explicit ImGui launch, and both switch
  directions retain their established behavior.
- The phase is reported as a naming correction, not LOC reduction.

---

## Phase CR2 - Delete Proven Unreferenced Code

**Goal:** Remove declarations, definitions, files, tests, and build metadata that
have no live production purpose.

### Tasks

- [ ] Work in small owner-aligned batches from the approved CR0 ledger.
- [ ] For each symbol, perform the full reachability review immediately before
      deletion so evidence cannot go stale.
- [ ] Delete the declaration, definition, private helpers that become unreachable,
      obsolete includes, project/filter entries, and comments that promise the
      removed surface.
- [ ] Remove or rewrite tests only when their sole subject was the retired code.
      Preserve behavioral coverage through the surviving production entry point.
- [ ] Rerun the reachability inventory after each owner batch and remove stale
      reachability rulings for symbols that no longer exist.
- [ ] Record exact production additions/deletions and focused validation beside
      each ledger row.

### Acceptance

- Every deleted symbol has a recorded no-production-invocation ruling.
- No virtual, callback, registration, export, serialized, automation, or
  configuration route was mistaken for dead code.
- Project metadata and documentation contain no references to deleted files.
- Each batch is net deletion-positive in `SkullbonezSource/`.

---

## Phase CR3 - Consolidate Duplicate Conditions And Branch Bodies

**Goal:** Remove repeated conditional logic while preserving evaluation order,
side effects, and owner authority.

### Tasks

- [ ] Start with ledger entries where the same owner repeatedly evaluates the
      same predicates over the same state and performs the same branch body.
- [ ] Prefer merging adjacent branches or calculating one clearly named local
      decision once when the decision is frame-local.
- [ ] Use an owner-level predicate helper only when it expresses a stable domain
      question at two or more independent sites.
- [ ] Preserve short-circuit behavior, null guards, floating-point comparisons,
      feature-flag compilation, logging order, mutation order, and command
      priority.
- [ ] Do not replace readable conditions with opaque boolean parameter packs or a
      helper whose flags reconstruct the original branches.
- [ ] Add or strengthen focused branch-matrix tests before consolidation when
      existing coverage cannot distinguish the paths being merged.
- [ ] Record the removed blocks and net production LOC for each ledger row.

### Acceptance

- Each consolidation has explicit state and ordering equivalence evidence.
- Branch-matrix tests cover true, false, boundary, and relevant mixed cases.
- No command precedence, input capture, lifecycle ordering, or deterministic
  result changes.
- Each batch removes more production lines than it adds.

---

## Phase CR4 - Deduplicate Implementations Through Owned Helpers

**Goal:** Replace genuinely repeated operations with small, correctly placed,
independently testable helpers.

### Tasks

- [ ] Process only approved CR0 ledger entries with at least two independent
      call sites and demonstrated semantic equivalence.
- [ ] Choose the lowest honest owner. Keep UI policy in UI, Runtime orchestration
      in its Runtime package, Rendering values in Rendering, Physics algorithms
      in Physics, and infrastructure-only operations in Core.
- [ ] Prefer pure value transformations or owner methods over shared mutable
      state, service bags, callback packs, and forwarding facades.
- [ ] Give parameters domain names and preserve units, coordinate spaces,
      capacities, sentinels, failure behavior, and allocation policy.
- [ ] Replace all approved duplicate sites in the same batch. Do not leave a new
      helper beside an equivalent hand-written copy without a recorded reason.
- [ ] Delete extraction scars, obsolete aliases, redundant includes, and unused
      local wrappers exposed by the consolidation.
- [ ] Apply the source comment guide to every touched source-bearing file and run
      the required comment-style audit before closure.
- [ ] Record helper LOC, call-site deletions, and net production reduction.

### Rejection criteria

Reject or narrow the extraction when:

- the helper has only one real caller;
- parameters are mostly authority or capability forwarding;
- callers differ in units, ordering, lifetime, capacity, or failure behavior;
- placement would create an upward dependency or feature vocabulary in a lower
  layer;
- flags make the helper a collection of the original separate implementations;
- the result merely shortens a ruled complex function without moving or deleting
  duplicated responsibility.

### Acceptance

- Every helper owns one coherent operation and has at least two justified callers.
- Focused tests target the shared operation and caller-specific policy remains at
  the call sites.
- Allocation, determinism, serialization, physics, and presentation invariants
  relevant to the touched owner remain unchanged.
- Each batch is a measured net reduction in `SkullbonezSource/`.

---

## Phase CR5 - Terminal Closure

**Goal:** Prove the campaign removed code without hiding behavior changes,
ownership regressions, or stale references.

### Tasks

- [ ] Re-run all seven structural inventories named in `AGENTS.md` and adjudicate
      new or stale rows caused by the final tree.
- [ ] Run the dependency graph and project/filter consistency gates.
- [ ] Run focused tests for every touched owner, followed by the repository's
      terminal plan-completion validation.
- [ ] Run graphics, replay, physics, automation, performance, or visual gates only
      where the implemented candidate batches touched those behaviors, plus any
      gates required by the terminal validation contract.
- [ ] Reconcile every candidate ledger row with the final source. Remove rejected
      candidates from the claimed reduction and explain why they stayed separate.
- [ ] Report production LOC added, deleted, and net change by phase and in total.
- [ ] Confirm ImGui, Tracy, GameUI, and the level editor remain present and usable.
- [ ] Confirm no golden baseline was refreshed merely to make the work pass.

### Final report table

| Phase | Production lines added | Production lines deleted | Net reduction | Validation evidence |
|---|---:|---:|---:|---|
| CR1 - GameUI rename | | | | |
| CR2 - Unreferenced deletion | | | | |
| CR3 - Duplicate conditions | | | | |
| CR4 - Owned helpers | | | | |
| **Total** | | | | |

### Completion criteria

- The candidate ledgers contain no unresolved implementation rows.
- Every claimed deletion is visible in the final diff and attributed to a named
  owner or duplicated operation.
- Every new helper demonstrates real reuse and net deletion.
- The final production LOC reduction excludes tests, plans, tools, generated
  output, and third-party source.
- Required validation is green with no unauthorized baseline changes.

---

## Validation Map

Intermediate batches use `tools\validate_fast.bat` plus the smallest focused
gate that owns the touched behavior. Heavy suites are reserved for CR5 unless a
specific high-risk batch requires earlier proof.

| Change area | Required focused evidence |
|---|---|
| GameUI naming and selection | Startup parsing tests, UI surface-selection tests, `tools\validate_ui_stress.bat`, project filters |
| Unreferenced symbol deletion | Current reachability inventory, owning unit tests, link/build for affected configurations |
| Input or UI condition consolidation | Input/UI focused tests and deterministic UI stress |
| Automation deduplication | Automation parser/execution/report tests and unchanged recorded-interaction behavior |
| Replay or serialization deduplication | Replay compatibility, byte/determinism, restore, and allocation tests |
| Rendering deduplication | DX12 renderer validation and focused visual/stress evidence |
| Physics deduplication | Focused physics tests, deterministic worker matrix, allocation and performance evidence; no baseline refresh without owner approval |
| Build/project deletion | Build-configuration, dependency-graph, and project-filter validation |
| Final closure | `tools\agent_validate.bat --plan-completion` and all owner-required terminal gates |

## Stop Conditions

Stop the affected batch and return it to owner review if:

- reachability is uncertain because of dispatch, registration, export, or stale
  build evidence;
- two candidate blocks differ in ownership or observable semantics;
- consolidation requires a new upward dependency or retained authority object;
- the helper adds as much or more production code than it removes without a
  separately approved correctness benefit;
- an existing test or recording demonstrates intentional behavior that the
  deletion would remove;
- validation requires changing a golden baseline rather than preserving behavior;
- implementation would remove or disable ImGui, Tracy, GameUI, or editor features.
